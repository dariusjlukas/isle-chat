#pragma once
#include <App.h>
#include <nlohmann/json.hpp>
#include <unordered_set>
#include "db/database.h"
#include "ws/ws_handler.h"

using json = nlohmann::json;

template <bool SSL>
struct ChannelHandler {
    Database& db;
    WsHandler<SSL>& ws;

    void register_routes(uWS::TemplatedApp<SSL>& app) {
        app.get("/api/channels", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            auto user = db.find_user_by_id(user_id);
            bool is_server_admin = user && user->role == "admin";

            auto channels = db.list_user_channels(user_id);

            // Server admins see all non-DM channels
            if (is_server_admin) {
                auto all_channels = db.list_all_channels();
                std::unordered_set<std::string> existing_ids;
                for (const auto& ch : channels) existing_ids.insert(ch.id);
                for (const auto& ch : all_channels) {
                    if (existing_ids.find(ch.id) == existing_ids.end()) {
                        channels.push_back(ch);
                    }
                }
            }

            json arr = json::array();
            for (const auto& ch : channels) {
                json members = json::array();
                auto member_list = db.get_channel_members_with_roles(ch.id);
                for (const auto& m : member_list) {
                    members.push_back({{"id", m.user_id}, {"username", m.username},
                                       {"display_name", m.display_name},
                                       {"is_online", m.is_online}, {"last_seen", m.last_seen}, {"role", m.role}});
                }

                std::string my_role = db.get_effective_role(ch.id, user_id);

                json ch_json = {{"id", ch.id}, {"name", ch.name},
                               {"description", ch.description},
                               {"is_direct", ch.is_direct},
                               {"is_public", ch.is_public},
                               {"default_role", ch.default_role},
                               {"created_at", ch.created_at},
                               {"my_role", my_role},
                               {"members", members}};
                if (!ch.space_id.empty()) ch_json["space_id"] = ch.space_id;
                if (!ch.conversation_name.empty()) ch_json["conversation_name"] = ch.conversation_name;
                arr.push_back(ch_json);
            }
            res->writeHeader("Content-Type", "application/json")->end(arr.dump());
        });

        app.post("/api/channels", [this](auto* res, auto* req) {
            auto user_id_copy = get_user_id(res, req);
            std::string body;
            res->onData([this, res, user_id = std::move(user_id_copy), body = std::move(body)](
                std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                if (user_id.empty()) return;
                handle_create_channel(res, body, user_id);
            });
            res->onAborted([]() {});
        });

        // Register /public BEFORE /:id routes to avoid route collision
        app.get("/api/channels/public", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            std::string search(req->getQuery("search"));
            auto channels = db.list_public_channels(user_id, search);
            json arr = json::array();
            for (const auto& ch : channels) {
                arr.push_back({{"id", ch.id}, {"name", ch.name},
                               {"description", ch.description},
                               {"is_public", ch.is_public},
                               {"default_role", ch.default_role},
                               {"created_at", ch.created_at}});
            }
            res->writeHeader("Content-Type", "application/json")->end(arr.dump());
        });

        app.get("/api/channels/:id/messages", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            std::string channel_id(req->getParameter("id"));
            std::string before(req->getQuery("before"));
            std::string limit_str(req->getQuery("limit"));
            int limit = limit_str.empty() ? 50 : std::stoi(limit_str);

            // Server admins can view any channel's messages
            std::string role = db.get_effective_role(channel_id, user_id);
            if (role.empty()) {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Not a member of this channel"})");
                return;
            }

            auto messages = db.get_messages(channel_id, limit, before);
            json arr = json::array();
            for (const auto& msg : messages) {
                json m = {{"id", msg.id}, {"channel_id", msg.channel_id},
                          {"user_id", msg.user_id}, {"username", msg.username},
                          {"content", msg.content}, {"created_at", msg.created_at},
                          {"is_deleted", msg.is_deleted}};
                if (!msg.edited_at.empty()) m["edited_at"] = msg.edited_at;
                if (!msg.file_id.empty()) {
                    m["file_id"] = msg.file_id;
                    m["file_name"] = msg.file_name;
                    m["file_size"] = msg.file_size;
                    m["file_type"] = msg.file_type;
                }
                arr.push_back(m);
            }
            res->writeHeader("Content-Type", "application/json")->end(arr.dump());
        });

        app.get("/api/channels/:id/read-receipts", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            std::string channel_id(req->getParameter("id"));

            std::string role = db.get_effective_role(channel_id, user_id);
            if (role.empty()) {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Not a member of this channel"})");
                return;
            }

            auto receipts = db.get_channel_read_receipts(channel_id);
            json arr = json::array();
            for (const auto& r : receipts) {
                arr.push_back({{"user_id", r.user_id}, {"username", r.username},
                               {"last_read_message_id", r.last_read_message_id},
                               {"last_read_at", r.last_read_at}});
            }
            res->writeHeader("Content-Type", "application/json")->end(arr.dump());
        });

        app.post("/api/channels/dm", [this](auto* res, auto* req) {
            auto user_id_copy = get_user_id(res, req);
            std::string body;
            res->onData([this, res, user_id = std::move(user_id_copy), body = std::move(body)](
                std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                if (user_id.empty()) return;
                handle_create_dm(res, body, user_id);
            });
            res->onAborted([]() {});
        });

        app.post("/api/channels/:id/join", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;
            std::string channel_id(req->getParameter("id"));

            auto ch = db.find_channel_by_id(channel_id);
            if (!ch) {
                res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Channel not found"})");
                return;
            }
            if (ch->is_direct) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Cannot join a DM channel"})");
                return;
            }
            if (!ch->is_public) {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"This is a private channel. You need an invite."})");
                return;
            }

            db.add_channel_member(channel_id, user_id, ch->default_role);
            ws.subscribe_user_to_channel(user_id, channel_id);
            res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
        });

        // Invite user to channel
        app.post("/api/channels/:id/members", [this](auto* res, auto* req) {
            auto user_id_copy = get_user_id(res, req);
            std::string channel_id(req->getParameter("id"));
            std::string body;
            res->onData([this, res, user_id = std::move(user_id_copy),
                         channel_id = std::move(channel_id), body = std::move(body)](
                std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                if (user_id.empty()) return;

                std::string role = db.get_effective_role(channel_id, user_id);
                if (role != "admin") {
                    res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Admin permission required"})");
                    return;
                }

                try {
                    auto j = json::parse(body);
                    std::string target_user_id = j.at("user_id");
                    std::string member_role = j.value("role", "");

                    if (member_role.empty()) {
                        auto ch = db.find_channel_by_id(channel_id);
                        member_role = ch ? ch->default_role : "write";
                    }

                    db.add_channel_member(channel_id, target_user_id, member_role);
                    ws.subscribe_user_to_channel(target_user_id, channel_id);

                    // Build channel data for notification
                    auto ch = db.find_channel_by_id(channel_id);
                    if (ch) {
                        json members = json::array();
                        auto member_list = db.get_channel_members_with_roles(channel_id);
                        for (const auto& m : member_list) {
                            members.push_back({{"id", m.user_id}, {"username", m.username},
                                               {"display_name", m.display_name},
                                               {"is_online", m.is_online}, {"last_seen", m.last_seen}, {"role", m.role}});
                        }
                        json channel_data = {{"id", ch->id}, {"name", ch->name},
                                             {"description", ch->description},
                                             {"is_direct", ch->is_direct},
                                             {"is_public", ch->is_public},
                                             {"default_role", ch->default_role},
                                             {"created_at", ch->created_at},
                                             {"my_role", member_role},
                                             {"members", members}};
                        if (!ch->space_id.empty()) channel_data["space_id"] = ch->space_id;
                        json notify = {{"type", "channel_added"}, {"channel", channel_data}};
                        ws.send_to_user(target_user_id, notify.dump());
                    }

                    res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
                } catch (const std::exception& e) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->end(json({{"error", e.what()}}).dump());
                }
            });
            res->onAborted([]() {});
        });

        // Kick user from channel
        app.del("/api/channels/:id/members/:userId", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            std::string channel_id(req->getParameter(0));
            std::string target_user_id(req->getParameter(1));

            std::string role = db.get_effective_role(channel_id, user_id);
            if (role != "admin") {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Admin permission required"})");
                return;
            }

            if (target_user_id == user_id) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Cannot remove yourself"})");
                return;
            }

            db.remove_channel_member(channel_id, target_user_id);
            ws.unsubscribe_user_from_channel(target_user_id, channel_id);

            json notify = {{"type", "channel_removed"}, {"channel_id", channel_id}};
            ws.send_to_user(target_user_id, notify.dump());

            res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
        });

        // Change member role
        app.put("/api/channels/:id/members/:userId", [this](auto* res, auto* req) {
            auto user_id_copy = get_user_id(res, req);
            std::string channel_id(req->getParameter(0));
            std::string target_user_id(req->getParameter(1));
            std::string body;
            res->onData([this, res, user_id = std::move(user_id_copy),
                         channel_id = std::move(channel_id),
                         target_user_id = std::move(target_user_id),
                         body = std::move(body)](
                std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                if (user_id.empty()) return;

                std::string role = db.get_effective_role(channel_id, user_id);
                if (role != "admin") {
                    res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Admin permission required"})");
                    return;
                }

                try {
                    auto j = json::parse(body);
                    std::string new_role = j.at("role");
                    if (new_role != "admin" && new_role != "write" && new_role != "read") {
                        res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                            ->end(R"({"error":"Invalid role. Must be admin, write, or read"})");
                        return;
                    }

                    db.update_member_role(channel_id, target_user_id, new_role);

                    json notify = {{"type", "role_changed"}, {"channel_id", channel_id}, {"role", new_role}};
                    ws.send_to_user(target_user_id, notify.dump());

                    res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
                } catch (const std::exception& e) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->end(json({{"error", e.what()}}).dump());
                }
            });
            res->onAborted([]() {});
        });

        // Update channel settings
        app.put("/api/channels/:id", [this](auto* res, auto* req) {
            auto user_id_copy = get_user_id(res, req);
            std::string channel_id(req->getParameter("id"));
            std::string body;
            res->onData([this, res, user_id = std::move(user_id_copy),
                         channel_id = std::move(channel_id), body = std::move(body)](
                std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                if (user_id.empty()) return;

                std::string role = db.get_effective_role(channel_id, user_id);
                if (role != "admin") {
                    res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Admin permission required"})");
                    return;
                }

                try {
                    auto current = db.find_channel_by_id(channel_id);
                    if (!current) {
                        res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                            ->end(R"({"error":"Channel not found"})");
                        return;
                    }

                    auto j = json::parse(body);
                    std::string name = j.value("name", current->name);
                    std::string description = j.value("description", current->description);
                    bool is_public = j.value("is_public", current->is_public);
                    std::string default_role = j.value("default_role", current->default_role);

                    auto updated = db.update_channel(channel_id, name, description, is_public, default_role);

                    json resp = {{"id", updated.id}, {"name", updated.name},
                                 {"description", updated.description},
                                 {"is_public", updated.is_public},
                                 {"default_role", updated.default_role}};

                    // Broadcast update to channel members
                    json broadcast = {{"type", "channel_updated"}, {"channel", resp}};
                    ws.broadcast_to_channel(channel_id, broadcast.dump());

                    res->writeHeader("Content-Type", "application/json")->end(resp.dump());
                } catch (const std::exception& e) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->end(json({{"error", e.what()}}).dump());
                }
            });
            res->onAborted([]() {});
        });
    }

private:
    std::string get_user_id(uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req) {
        std::string token(req->getHeader("authorization"));
        if (token.rfind("Bearer ", 0) == 0) token = token.substr(7);
        auto user_id = db.validate_session(token);
        if (!user_id) {
            res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Unauthorized"})");
            return "";
        }
        return *user_id;
    }

    void handle_create_channel(uWS::HttpResponse<SSL>* res, const std::string& body,
                                const std::string& user_id) {
        try {
            auto j = json::parse(body);
            std::string name = j.at("name");
            std::string description = j.value("description", "");
            bool is_public = j.value("is_public", true);
            std::string default_role = j.value("default_role", "write");

            if (default_role != "admin" && default_role != "write" && default_role != "read") {
                default_role = "write";
            }

            std::vector<std::string> member_ids = {user_id};
            if (j.contains("member_ids")) {
                for (const auto& mid : j["member_ids"]) {
                    std::string id = mid.get<std::string>();
                    if (id != user_id) member_ids.push_back(id);
                }
            }

            auto ch = db.create_channel(name, description, false, user_id, member_ids,
                                         is_public, default_role);

            // Build member list for notification
            json members = json::array();
            auto member_list = db.get_channel_members_with_roles(ch.id);
            for (const auto& m : member_list) {
                members.push_back({{"id", m.user_id}, {"username", m.username},
                                   {"display_name", m.display_name},
                                   {"is_online", m.is_online}, {"last_seen", m.last_seen}, {"role", m.role}});
            }

            json channel_data = {{"id", ch.id}, {"name", ch.name},
                                 {"description", ch.description},
                                 {"is_direct", ch.is_direct},
                                 {"is_public", ch.is_public},
                                 {"default_role", ch.default_role},
                                 {"created_at", ch.created_at},
                                 {"members", members}};
            if (!ch.space_id.empty()) channel_data["space_id"] = ch.space_id;

            // Notify and subscribe all members
            auto actual_member_ids = db.get_channel_member_ids(ch.id);
            for (const auto& mid : actual_member_ids) {
                ws.subscribe_user_to_channel(mid, ch.id);
                std::string my_role = db.get_effective_role(ch.id, mid);
                json notify_data = channel_data;
                notify_data["my_role"] = my_role;
                if (mid != user_id) {
                    json notify = {{"type", "channel_added"}, {"channel", notify_data}};
                    ws.send_to_user(mid, notify.dump());
                }
            }

            // Subscribe server admins who aren't already members
            ws.subscribe_admins_to_channel(db, ch.id);

            json resp = channel_data;
            resp["my_role"] = "admin";
            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        } catch (const std::exception& e) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    }

    void handle_create_dm(uWS::HttpResponse<SSL>* res, const std::string& body,
                           const std::string& user_id) {
        try {
            auto j = json::parse(body);
            std::string other_user_id = j.at("user_id");

            // Check if DM already exists
            auto existing = db.find_dm_channel(user_id, other_user_id);
            if (existing) {
                json resp = {{"id", existing->id}, {"name", existing->name},
                             {"is_direct", true}, {"created_at", existing->created_at}};
                res->writeHeader("Content-Type", "application/json")->end(resp.dump());
                return;
            }

            auto ch = db.create_channel("", "", true, user_id, {user_id, other_user_id});

            // Build member list for the notification
            json members = json::array();
            auto member_ids = db.get_channel_member_ids(ch.id);
            for (const auto& mid : member_ids) {
                auto u = db.find_user_by_id(mid);
                if (u) {
                    members.push_back({{"id", u->id}, {"username", u->username},
                                       {"display_name", u->display_name},
                                       {"is_online", u->is_online}, {"last_seen", u->last_seen}});
                }
            }

            json channel_data = {{"id", ch.id}, {"name", ch.name},
                                 {"description", ""}, {"is_direct", true},
                                 {"is_public", false}, {"default_role", "write"},
                                 {"created_at", ch.created_at}, {"my_role", "write"},
                                 {"members", members}};

            // Notify all members via WebSocket and subscribe them to the channel
            for (const auto& mid : member_ids) {
                ws.subscribe_user_to_channel(mid, ch.id);
                if (mid != user_id) {
                    json notify = {{"type", "channel_added"}, {"channel", channel_data}};
                    ws.send_to_user(mid, notify.dump());
                }
            }

            json resp = {{"id", ch.id}, {"name", ch.name},
                         {"is_direct", true}, {"created_at", ch.created_at}};
            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        } catch (const std::exception& e) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    }
};
