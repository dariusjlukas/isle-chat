#pragma once
#include <App.h>
#include <nlohmann/json.hpp>
#include "db/database.h"
#include "ws/ws_handler.h"

using json = nlohmann::json;

template <bool SSL>
struct SpaceHandler {
    Database& db;
    WsHandler<SSL>& ws;

    void register_routes(uWS::TemplatedApp<SSL>& app) {
        // List user's spaces
        app.get("/api/spaces", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            auto user = db.find_user_by_id(user_id);
            bool is_server_admin = user && user->role == "admin";

            auto spaces = db.list_user_spaces(user_id);

            // Server admins see all spaces
            if (is_server_admin) {
                auto all = db.list_all_spaces();
                std::unordered_set<std::string> existing_ids;
                for (const auto& sp : spaces) existing_ids.insert(sp.id);
                for (const auto& sp : all) {
                    if (existing_ids.find(sp.id) == existing_ids.end()) {
                        spaces.push_back(sp);
                    }
                }
            }

            json arr = json::array();
            for (const auto& sp : spaces) {
                auto members = db.get_space_members_with_roles(sp.id);
                json members_arr = json::array();
                for (const auto& m : members) {
                    members_arr.push_back({{"id", m.user_id}, {"username", m.username},
                                           {"display_name", m.display_name},
                                           {"is_online", m.is_online}, {"last_seen", m.last_seen}, {"role", m.role}});
                }
                std::string my_role = db.get_space_member_role(sp.id, user_id);
                if (my_role.empty() && is_server_admin) my_role = "admin";

                arr.push_back({{"id", sp.id}, {"name", sp.name},
                               {"description", sp.description}, {"icon", sp.icon},
                               {"is_public", sp.is_public},
                               {"default_role", sp.default_role},
                               {"created_at", sp.created_at},
                               {"my_role", my_role},
                               {"members", members_arr}});
            }
            res->writeHeader("Content-Type", "application/json")
                ->writeHeader("Access-Control-Allow-Origin", "*")
                ->end(arr.dump());
        });

        // Browse public spaces
        app.get("/api/spaces/public", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;
            std::string search(req->getQuery("search"));
            auto spaces = db.list_public_spaces(user_id, search);
            json arr = json::array();
            for (const auto& sp : spaces) {
                arr.push_back({{"id", sp.id}, {"name", sp.name},
                               {"description", sp.description}, {"icon", sp.icon},
                               {"is_public", sp.is_public},
                               {"default_role", sp.default_role},
                               {"created_at", sp.created_at}});
            }
            res->writeHeader("Content-Type", "application/json")
                ->writeHeader("Access-Control-Allow-Origin", "*")
                ->end(arr.dump());
        });

        // Create space
        app.post("/api/spaces", [this](auto* res, auto* req) {
            auto user_id_copy = get_user_id(res, req);
            std::string body;
            res->onData([this, res, user_id = std::move(user_id_copy), body = std::move(body)](
                std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                if (user_id.empty()) return;
                try {
                    auto j = json::parse(body);
                    std::string name = j.at("name");
                    std::string description = j.value("description", "");
                    std::string icon = j.value("icon", "");
                    bool is_public = j.value("is_public", true);
                    std::string default_role = j.value("default_role", "write");

                    auto sp = db.create_space(name, description, icon, is_public, user_id, default_role);

                    auto members = db.get_space_members_with_roles(sp.id);
                    json members_arr = json::array();
                    for (const auto& m : members) {
                        members_arr.push_back({{"id", m.user_id}, {"username", m.username},
                                               {"display_name", m.display_name},
                                               {"is_online", m.is_online}, {"last_seen", m.last_seen}, {"role", m.role}});
                    }

                    json resp = {{"id", sp.id}, {"name", sp.name},
                                 {"description", sp.description}, {"icon", sp.icon},
                                 {"is_public", sp.is_public},
                                 {"default_role", sp.default_role},
                                 {"created_at", sp.created_at},
                                 {"my_role", "admin"},
                                 {"members", members_arr}};

                    // Notify admins about new space
                    ws.subscribe_admins_to_space(db, sp.id);

                    res->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(resp.dump());
                } catch (const std::exception& e) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(json({{"error", e.what()}}).dump());
                }
            });
            res->onAborted([]() {});
        });

        // Get space details
        app.get("/api/spaces/:id", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;
            std::string space_id(req->getParameter("id"));

            auto sp = db.find_space_by_id(space_id);
            if (!sp) {
                res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(R"({"error":"Space not found"})");
                return;
            }

            auto members = db.get_space_members_with_roles(space_id);
            json members_arr = json::array();
            for (const auto& m : members) {
                members_arr.push_back({{"id", m.user_id}, {"username", m.username},
                                       {"display_name", m.display_name},
                                       {"is_online", m.is_online}, {"last_seen", m.last_seen}, {"role", m.role}});
            }
            std::string my_role = db.get_space_member_role(space_id, user_id);
            auto user = db.find_user_by_id(user_id);
            if (my_role.empty() && user && user->role == "admin") my_role = "admin";

            json resp = {{"id", sp->id}, {"name", sp->name},
                         {"description", sp->description}, {"icon", sp->icon},
                         {"is_public", sp->is_public},
                         {"default_role", sp->default_role},
                         {"created_at", sp->created_at},
                         {"my_role", my_role},
                         {"members", members_arr}};

            res->writeHeader("Content-Type", "application/json")
                ->writeHeader("Access-Control-Allow-Origin", "*")
                ->end(resp.dump());
        });

        // Update space settings
        app.put("/api/spaces/:id", [this](auto* res, auto* req) {
            auto user_id_copy = get_user_id(res, req);
            std::string space_id(req->getParameter("id"));
            std::string body;
            res->onData([this, res, user_id = std::move(user_id_copy),
                         space_id = std::move(space_id), body = std::move(body)](
                std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                if (user_id.empty()) return;

                std::string role = db.get_space_member_role(space_id, user_id);
                auto user = db.find_user_by_id(user_id);
                if (role != "admin" && !(user && user->role == "admin")) {
                    res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(R"({"error":"Admin permission required"})");
                    return;
                }

                try {
                    auto current = db.find_space_by_id(space_id);
                    if (!current) {
                        res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                            ->writeHeader("Access-Control-Allow-Origin", "*")
                            ->end(R"({"error":"Space not found"})");
                        return;
                    }

                    auto j = json::parse(body);
                    std::string name = j.value("name", current->name);
                    std::string description = j.value("description", current->description);
                    std::string icon = j.value("icon", current->icon);
                    bool is_public = j.value("is_public", current->is_public);
                    std::string default_role = j.value("default_role", current->default_role);

                    auto updated = db.update_space(space_id, name, description, icon, is_public, default_role);

                    json resp = {{"id", updated.id}, {"name", updated.name},
                                 {"description", updated.description}, {"icon", updated.icon},
                                 {"is_public", updated.is_public},
                                 {"default_role", updated.default_role}};

                    // Broadcast to space members
                    json broadcast = {{"type", "space_updated"}, {"space", resp}};
                    ws.broadcast_to_space(space_id, broadcast.dump());

                    res->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(resp.dump());
                } catch (const std::exception& e) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(json({{"error", e.what()}}).dump());
                }
            });
            res->onAborted([]() {});
        });

        // Join public space
        app.post("/api/spaces/:id/join", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;
            std::string space_id(req->getParameter("id"));

            auto sp = db.find_space_by_id(space_id);
            if (!sp) {
                res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(R"({"error":"Space not found"})");
                return;
            }
            if (!sp->is_public) {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(R"({"error":"This is a private space. You need an invite."})");
                return;
            }

            db.add_space_member(space_id, user_id, sp->default_role);
            ws.subscribe_user_to_space(user_id, space_id);

            res->writeHeader("Content-Type", "application/json")
                ->writeHeader("Access-Control-Allow-Origin", "*")
                ->end(R"({"ok":true})");
        });

        // Add member to space
        app.post("/api/spaces/:id/members", [this](auto* res, auto* req) {
            auto user_id_copy = get_user_id(res, req);
            std::string space_id(req->getParameter("id"));
            std::string body;
            res->onData([this, res, user_id = std::move(user_id_copy),
                         space_id = std::move(space_id), body = std::move(body)](
                std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                if (user_id.empty()) return;

                std::string role = db.get_space_member_role(space_id, user_id);
                auto user = db.find_user_by_id(user_id);
                if (role != "admin" && !(user && user->role == "admin")) {
                    res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(R"({"error":"Admin permission required"})");
                    return;
                }

                try {
                    auto j = json::parse(body);
                    std::string target_user_id = j.at("user_id");
                    auto sp = db.find_space_by_id(space_id);
                    std::string member_role = j.value("role", sp ? sp->default_role : "write");

                    db.add_space_member(space_id, target_user_id, member_role);
                    ws.subscribe_user_to_space(target_user_id, space_id);

                    // Notify the added user
                    if (sp) {
                        auto members = db.get_space_members_with_roles(space_id);
                        json members_arr = json::array();
                        for (const auto& m : members) {
                            members_arr.push_back({{"id", m.user_id}, {"username", m.username},
                                                   {"display_name", m.display_name},
                                                   {"is_online", m.is_online}, {"last_seen", m.last_seen}, {"role", m.role}});
                        }
                        json space_data = {{"id", sp->id}, {"name", sp->name},
                                           {"description", sp->description}, {"icon", sp->icon},
                                           {"is_public", sp->is_public},
                                           {"default_role", sp->default_role},
                                           {"created_at", sp->created_at},
                                           {"my_role", member_role},
                                           {"members", members_arr}};
                        json notify = {{"type", "space_added"}, {"space", space_data}};
                        ws.send_to_user(target_user_id, notify.dump());
                    }

                    res->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(R"({"ok":true})");
                } catch (const std::exception& e) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(json({{"error", e.what()}}).dump());
                }
            });
            res->onAborted([]() {});
        });

        // Remove member from space
        app.del("/api/spaces/:id/members/:userId", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;
            std::string space_id(req->getParameter(0));
            std::string target_user_id(req->getParameter(1));

            std::string role = db.get_space_member_role(space_id, user_id);
            auto user = db.find_user_by_id(user_id);
            if (role != "admin" && !(user && user->role == "admin")) {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(R"({"error":"Admin permission required"})");
                return;
            }

            db.remove_space_member(space_id, target_user_id);
            ws.unsubscribe_user_from_space(target_user_id, space_id);

            json notify = {{"type", "space_removed"}, {"space_id", space_id}};
            ws.send_to_user(target_user_id, notify.dump());

            res->writeHeader("Content-Type", "application/json")
                ->writeHeader("Access-Control-Allow-Origin", "*")
                ->end(R"({"ok":true})");
        });

        // Change space member role
        app.put("/api/spaces/:id/members/:userId", [this](auto* res, auto* req) {
            auto user_id_copy = get_user_id(res, req);
            std::string space_id(req->getParameter(0));
            std::string target_user_id(req->getParameter(1));
            std::string body;
            res->onData([this, res, user_id = std::move(user_id_copy),
                         space_id = std::move(space_id),
                         target_user_id = std::move(target_user_id),
                         body = std::move(body)](
                std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                if (user_id.empty()) return;

                std::string role = db.get_space_member_role(space_id, user_id);
                auto user = db.find_user_by_id(user_id);
                if (role != "admin" && !(user && user->role == "admin")) {
                    res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(R"({"error":"Admin permission required"})");
                    return;
                }

                try {
                    auto j = json::parse(body);
                    std::string new_role = j.at("role");
                    if (new_role != "admin" && new_role != "write" && new_role != "read") {
                        res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                            ->writeHeader("Access-Control-Allow-Origin", "*")
                            ->end(R"({"error":"Invalid role"})");
                        return;
                    }
                    db.update_space_member_role(space_id, target_user_id, new_role);
                    res->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(R"({"ok":true})");
                } catch (const std::exception& e) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(json({{"error", e.what()}}).dump());
                }
            });
            res->onAborted([]() {});
        });

        // List channels in space
        app.get("/api/spaces/:id/channels", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;
            std::string space_id(req->getParameter("id"));

            auto channels = db.list_user_channels(user_id);
            json arr = json::array();
            for (const auto& ch : channels) {
                if (ch.space_id != space_id) continue;
                json members = json::array();
                auto member_list = db.get_channel_members_with_roles(ch.id);
                for (const auto& m : member_list) {
                    members.push_back({{"id", m.user_id}, {"username", m.username},
                                       {"display_name", m.display_name},
                                       {"is_online", m.is_online}, {"last_seen", m.last_seen}, {"role", m.role}});
                }
                std::string my_role = db.get_effective_role(ch.id, user_id);
                arr.push_back({{"id", ch.id}, {"name", ch.name},
                               {"description", ch.description},
                               {"is_direct", ch.is_direct},
                               {"is_public", ch.is_public},
                               {"default_role", ch.default_role},
                               {"space_id", ch.space_id},
                               {"created_at", ch.created_at},
                               {"my_role", my_role},
                               {"members", members}});
            }
            res->writeHeader("Content-Type", "application/json")
                ->writeHeader("Access-Control-Allow-Origin", "*")
                ->end(arr.dump());
        });

        // Create channel in space
        app.post("/api/spaces/:id/channels", [this](auto* res, auto* req) {
            auto user_id_copy = get_user_id(res, req);
            std::string space_id(req->getParameter("id"));
            std::string body;
            res->onData([this, res, user_id = std::move(user_id_copy),
                         space_id = std::move(space_id), body = std::move(body)](
                std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                if (user_id.empty()) return;

                // Must be a space member with write+ permissions
                auto u = db.find_user_by_id(user_id);
                bool is_server_admin = u && u->role == "admin";
                if (!is_server_admin) {
                    std::string space_role = db.get_space_member_role(space_id, user_id);
                    if (space_role.empty()) {
                        res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                            ->writeHeader("Access-Control-Allow-Origin", "*")
                            ->end(R"({"error":"Must be a space member"})");
                        return;
                    }
                    if (space_role == "read") {
                        res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                            ->writeHeader("Access-Control-Allow-Origin", "*")
                            ->end(R"({"error":"Insufficient permissions to create channels in this space"})");
                        return;
                    }
                }

                try {
                    auto j = json::parse(body);
                    std::string name = j.at("name");
                    std::string description = j.value("description", "");
                    bool is_public = j.value("is_public", true);
                    std::string default_role = j.value("default_role", "write");

                    std::vector<std::string> member_ids = {user_id};
                    if (j.contains("member_ids")) {
                        for (const auto& mid : j["member_ids"]) {
                            std::string id = mid.get<std::string>();
                            if (id != user_id) member_ids.push_back(id);
                        }
                    }

                    auto ch = db.create_channel(name, description, false, user_id, member_ids,
                                                 is_public, default_role, space_id);

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
                                         {"space_id", ch.space_id},
                                         {"created_at", ch.created_at},
                                         {"members", members}};

                    // For public space channels, auto-add all space members
                    if (is_public) {
                        auto space_members = db.get_space_members_with_roles(space_id);
                        for (const auto& sm : space_members) {
                            if (!db.is_channel_member(ch.id, sm.user_id)) {
                                db.add_channel_member(ch.id, sm.user_id, ch.default_role);
                            }
                        }
                        // Refresh members list for the response
                        auto updated_members = db.get_channel_members_with_roles(ch.id);
                        members = json::array();
                        for (const auto& m : updated_members) {
                            members.push_back({{"id", m.user_id}, {"username", m.username},
                                               {"display_name", m.display_name},
                                               {"is_online", m.is_online}, {"last_seen", m.last_seen}, {"role", m.role}});
                        }
                        channel_data["members"] = members;
                    }

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

                    ws.subscribe_admins_to_channel(db, ch.id);

                    json resp = channel_data;
                    resp["my_role"] = "admin";
                    res->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(resp.dump());
                } catch (const std::exception& e) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(json({{"error", e.what()}}).dump());
                }
            });
            res->onAborted([]() {});
        });

        // --- Conversation routes ---

        // List conversations
        app.get("/api/conversations", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;
            auto conversations = db.list_user_conversations(user_id);
            json arr = json::array();
            for (const auto& ch : conversations) {
                json members = json::array();
                auto member_list = db.get_channel_members_with_roles(ch.id);
                for (const auto& m : member_list) {
                    members.push_back({{"id", m.user_id}, {"username", m.username},
                                       {"display_name", m.display_name},
                                       {"is_online", m.is_online}, {"last_seen", m.last_seen}, {"role", m.role}});
                }
                arr.push_back({{"id", ch.id}, {"name", ch.conversation_name},
                               {"is_direct", true},
                               {"created_at", ch.created_at},
                               {"my_role", "write"},
                               {"members", members}});
            }
            res->writeHeader("Content-Type", "application/json")
                ->writeHeader("Access-Control-Allow-Origin", "*")
                ->end(arr.dump());
        });

        // Create conversation
        app.post("/api/conversations", [this](auto* res, auto* req) {
            auto user_id_copy = get_user_id(res, req);
            std::string body;
            res->onData([this, res, user_id = std::move(user_id_copy), body = std::move(body)](
                std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                if (user_id.empty()) return;

                try {
                    auto j = json::parse(body);
                    std::string name = j.value("name", "");
                    std::vector<std::string> member_ids = {user_id};
                    if (j.contains("member_ids")) {
                        for (const auto& mid : j["member_ids"]) {
                            std::string id = mid.get<std::string>();
                            if (id != user_id) member_ids.push_back(id);
                        }
                    }

                    // For 1:1, check if existing conversation exists
                    if (member_ids.size() == 2 && name.empty()) {
                        auto existing = db.find_dm_channel(user_id, member_ids[1]);
                        if (existing) {
                            json members = json::array();
                            auto ml = db.get_channel_members_with_roles(existing->id);
                            for (const auto& m : ml) {
                                members.push_back({{"id", m.user_id}, {"username", m.username},
                                                   {"display_name", m.display_name},
                                                   {"is_online", m.is_online}, {"last_seen", m.last_seen}, {"role", m.role}});
                            }
                            json resp = {{"id", existing->id}, {"name", existing->conversation_name},
                                         {"is_direct", true},
                                         {"created_at", existing->created_at},
                                         {"my_role", "write"},
                                         {"members", members}};
                            res->writeHeader("Content-Type", "application/json")
                                ->writeHeader("Access-Control-Allow-Origin", "*")
                                ->end(resp.dump());
                            return;
                        }
                    }

                    auto ch = db.create_conversation(user_id, member_ids, name);

                    json members = json::array();
                    auto member_list = db.get_channel_members_with_roles(ch.id);
                    for (const auto& m : member_list) {
                        members.push_back({{"id", m.user_id}, {"username", m.username},
                                           {"display_name", m.display_name},
                                           {"is_online", m.is_online}, {"last_seen", m.last_seen}, {"role", m.role}});
                    }

                    json channel_data = {{"id", ch.id}, {"name", ch.conversation_name},
                                         {"description", ""}, {"is_direct", true},
                                         {"is_public", false}, {"default_role", "write"},
                                         {"created_at", ch.created_at}, {"my_role", "write"},
                                         {"members", members}};

                    // Notify and subscribe all members
                    for (const auto& mid : member_ids) {
                        ws.subscribe_user_to_channel(mid, ch.id);
                        if (mid != user_id) {
                            json notify = {{"type", "channel_added"}, {"channel", channel_data}};
                            ws.send_to_user(mid, notify.dump());
                        }
                    }

                    res->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(channel_data.dump());
                } catch (const std::exception& e) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(json({{"error", e.what()}}).dump());
                }
            });
            res->onAborted([]() {});
        });

        // Add member to conversation
        app.post("/api/conversations/:id/members", [this](auto* res, auto* req) {
            auto user_id_copy = get_user_id(res, req);
            std::string channel_id(req->getParameter("id"));
            std::string body;
            res->onData([this, res, user_id = std::move(user_id_copy),
                         channel_id = std::move(channel_id), body = std::move(body)](
                std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                if (user_id.empty()) return;

                // Must be a member of the conversation
                if (!db.is_channel_member(channel_id, user_id)) {
                    res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(R"({"error":"Not a member of this conversation"})");
                    return;
                }

                try {
                    auto j = json::parse(body);
                    std::string target_user_id = j.at("user_id");
                    db.add_conversation_member(channel_id, target_user_id);
                    ws.subscribe_user_to_channel(target_user_id, channel_id);

                    // Build notification
                    auto ch = db.find_channel_by_id(channel_id);
                    if (ch) {
                        json members = json::array();
                        auto ml = db.get_channel_members_with_roles(channel_id);
                        for (const auto& m : ml) {
                            members.push_back({{"id", m.user_id}, {"username", m.username},
                                               {"display_name", m.display_name},
                                               {"is_online", m.is_online}, {"last_seen", m.last_seen}, {"role", m.role}});
                        }
                        json channel_data = {{"id", ch->id}, {"name", ch->conversation_name},
                                             {"description", ""}, {"is_direct", true},
                                             {"is_public", false}, {"default_role", "write"},
                                             {"created_at", ch->created_at}, {"my_role", "write"},
                                             {"members", members}};
                        json notify = {{"type", "channel_added"}, {"channel", channel_data}};
                        ws.send_to_user(target_user_id, notify.dump());

                        // Notify existing members about new member
                        json member_notify = {{"type", "conversation_member_added"},
                                              {"channel_id", channel_id},
                                              {"members", members}};
                        ws.broadcast_to_channel(channel_id, member_notify.dump());
                    }

                    res->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(R"({"ok":true})");
                } catch (const std::exception& e) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(json({{"error", e.what()}}).dump());
                }
            });
            res->onAborted([]() {});
        });

        // Rename conversation
        app.put("/api/conversations/:id", [this](auto* res, auto* req) {
            auto user_id_copy = get_user_id(res, req);
            std::string channel_id(req->getParameter("id"));
            std::string body;
            res->onData([this, res, user_id = std::move(user_id_copy),
                         channel_id = std::move(channel_id), body = std::move(body)](
                std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                if (user_id.empty()) return;

                if (!db.is_channel_member(channel_id, user_id)) {
                    res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(R"({"error":"Not a member"})");
                    return;
                }

                try {
                    auto j = json::parse(body);
                    std::string name = j.value("name", "");
                    db.rename_conversation(channel_id, name);

                    json broadcast = {{"type", "conversation_renamed"},
                                      {"channel_id", channel_id},
                                      {"name", name}};
                    ws.broadcast_to_channel(channel_id, broadcast.dump());

                    res->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(R"({"ok":true})");
                } catch (const std::exception& e) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
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
                ->writeHeader("Access-Control-Allow-Origin", "*")
                ->end(R"({"error":"Unauthorized"})");
            return "";
        }
        return *user_id;
    }
};
