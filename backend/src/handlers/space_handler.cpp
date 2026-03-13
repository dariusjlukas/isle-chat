#include "handlers/space_handler.h"
#include "handlers/format_utils.h"

using json = nlohmann::json;

template <bool SSL>
void SpaceHandler<SSL>::register_routes(uWS::TemplatedApp<SSL>& app) {
        // List user's spaces
        app.get("/api/spaces", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            auto user = db.find_user_by_id(user_id);
            bool is_server_admin = user && (user->role == "admin" || user->role == "owner");

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

                auto tools = db.get_space_tools(sp.id);
                json tools_arr = json::array();
                for (const auto& t : tools) tools_arr.push_back(t);

                arr.push_back({{"id", sp.id}, {"name", sp.name},
                               {"description", sp.description},
                               {"is_public", sp.is_public},
                               {"default_role", sp.default_role},
                               {"created_at", sp.created_at},
                               {"is_archived", sp.is_archived},
                               {"avatar_file_id", sp.avatar_file_id},
                               {"profile_color", sp.profile_color},
                               {"my_role", my_role},
                               {"members", members_arr},
                               {"enabled_tools", tools_arr}});
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
                               {"description", sp.description},
                               {"is_public", sp.is_public},
                               {"default_role", sp.default_role},
                               {"is_archived", sp.is_archived},
                               {"avatar_file_id", sp.avatar_file_id},
                               {"profile_color", sp.profile_color},
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
                    // Only server admins/owners can create spaces
                    auto creator = db.find_user_by_id(user_id);
                    if (!creator || (creator->role != "admin" && creator->role != "owner")) {
                        res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                            ->end(R"({"error":"Only server admins can create spaces"})");
                        return;
                    }

                    auto j = json::parse(body);
                    std::string name = j.at("name");
                    std::string description = j.value("description", "");
                    bool is_public = j.value("is_public", true);
                    std::string default_role = j.value("default_role", "write");

                    auto sp = db.create_space(name, description, is_public, user_id, default_role);

                    auto members = db.get_space_members_with_roles(sp.id);
                    json members_arr = json::array();
                    for (const auto& m : members) {
                        members_arr.push_back({{"id", m.user_id}, {"username", m.username},
                                               {"display_name", m.display_name},
                                               {"is_online", m.is_online}, {"last_seen", m.last_seen}, {"role", m.role}});
                    }

                    json resp = {{"id", sp.id}, {"name", sp.name},
                                 {"description", sp.description},
                                 {"is_public", sp.is_public},
                                 {"default_role", sp.default_role},
                                 {"created_at", sp.created_at},
                                 {"is_archived", sp.is_archived},
                                 {"avatar_file_id", sp.avatar_file_id},
                               {"profile_color", sp.profile_color},
                                 {"my_role", "owner"},
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
            if (my_role.empty() && user && (user->role == "admin" || user->role == "owner")) my_role = "admin";

            json resp = {{"id", sp->id}, {"name", sp->name},
                         {"description", sp->description},
                         {"is_public", sp->is_public},
                         {"default_role", sp->default_role},
                         {"created_at", sp->created_at},
                         {"is_archived", sp->is_archived},
                         {"avatar_file_id", sp->avatar_file_id},
                         {"profile_color", sp->profile_color},
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
                if (role != "admin" && role != "owner" && !(user && (user->role == "admin" || user->role == "owner"))) {
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
                    bool is_public = j.value("is_public", current->is_public);
                    std::string default_role = j.value("default_role", current->default_role);
                    std::string profile_color = j.value("profile_color", current->profile_color);

                    auto updated = db.update_space(space_id, name, description, is_public, default_role, profile_color);

                    json resp = {{"id", updated.id}, {"name", updated.name},
                                 {"description", updated.description},
                                 {"is_public", updated.is_public},
                                 {"default_role", updated.default_role},
                                 {"avatar_file_id", updated.avatar_file_id},
                                 {"profile_color", updated.profile_color}};

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

            // Auto-join default channels
            auto default_channels = db.get_default_join_channels(space_id);
            for (const auto& ch : default_channels) {
                if (!db.is_channel_member(ch.id, user_id)) {
                    db.add_channel_member(ch.id, user_id, ch.default_role);
                    ws.subscribe_user_to_channel(user_id, ch.id);

                    // Send channel_added to the new member
                    auto member_list = db.get_channel_members_with_roles(ch.id);
                    json members = json::array();
                    for (const auto& m : member_list) {
                        members.push_back({{"id", m.user_id}, {"username", m.username},
                                           {"display_name", m.display_name},
                                           {"is_online", m.is_online}, {"last_seen", m.last_seen}, {"role", m.role}});
                    }
                    std::string my_role = db.get_effective_role(ch.id, user_id);
                    json ch_data = {{"id", ch.id}, {"name", ch.name},
                                    {"description", ch.description},
                                    {"is_direct", ch.is_direct},
                                    {"is_public", ch.is_public},
                                    {"default_role", ch.default_role},
                                    {"default_join", ch.default_join},
                                    {"space_id", ch.space_id},
                                    {"is_archived", ch.is_archived},
                                    {"created_at", ch.created_at},
                                    {"my_role", my_role},
                                    {"members", members}};
                    json notify = {{"type", "channel_added"}, {"channel", ch_data}};
                    ws.send_to_user(user_id, notify.dump());
                }
            }

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
                if (role != "admin" && role != "owner" && !(user && (user->role == "admin" || user->role == "owner"))) {
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

                    if (db.is_space_member(space_id, target_user_id)) {
                        res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                            ->writeHeader("Access-Control-Allow-Origin", "*")
                            ->end(R"({"error":"User is already a member of this space"})");
                        return;
                    }
                    if (db.has_pending_space_invite(space_id, target_user_id)) {
                        res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                            ->writeHeader("Access-Control-Allow-Origin", "*")
                            ->end(R"({"error":"An invite is already pending for this user"})");
                        return;
                    }

                    auto invite_id = db.create_space_invite(space_id, target_user_id, user_id, member_role);
                    auto inviter = db.find_user_by_id(user_id);

                    // Send invite notification to target user
                    json invite_data = {{"id", invite_id}, {"space_id", space_id},
                                        {"space_name", sp ? sp->name : ""},
                                        {"invited_by_username", inviter ? inviter->username : ""},
                                        {"role", member_role},
                                        {"created_at", ""}};
                    json notify = {{"type", "space_invite"}, {"invite", invite_data}};
                    ws.send_to_user(target_user_id, notify.dump());

                    // Create bell notification for the invite
                    std::string notif_content = "Invited you to " + (sp ? sp->name : "a space");
                    auto nid = db.create_notification(target_user_id, "space_invite",
                        user_id, "", "", notif_content);
                    json notif = {{"type", "new_notification"}, {"notification", {
                        {"id", nid}, {"user_id", target_user_id}, {"type", "space_invite"},
                        {"source_user_id", user_id}, {"source_username", inviter ? inviter->username : ""},
                        {"channel_id", ""}, {"channel_name", ""},
                        {"message_id", ""}, {"space_id", space_id},
                        {"content", notif_content}, {"created_at", ""}, {"is_read", false}
                    }}};
                    ws.send_to_user(target_user_id, notif.dump());

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
            if (role != "admin" && role != "owner" && !(user && (user->role == "admin" || user->role == "owner"))) {
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
                if (role != "admin" && role != "owner" && !(user && (user->role == "admin" || user->role == "owner"))) {
                    res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(R"({"error":"Admin permission required"})");
                    return;
                }

                try {
                    auto j = json::parse(body);
                    std::string new_role = j.at("role");
                    if (new_role != "owner" && new_role != "admin" && new_role != "write" && new_role != "read") {
                        res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                            ->writeHeader("Access-Control-Allow-Origin", "*")
                            ->end(R"({"error":"Invalid role"})");
                        return;
                    }

                    // Actor's effective rank is the higher of space role and server role
                    int actor_space_rank = space_role_rank(role);
                    if (user && user->role == "owner") actor_space_rank = space_role_rank("owner");
                    else if (user && user->role == "admin" && actor_space_rank < space_role_rank("admin"))
                        actor_space_rank = space_role_rank("admin");

                    std::string current_role = db.get_space_member_role(space_id, target_user_id);
                    int target_rank = space_role_rank(current_role);
                    int new_rank = space_role_rank(new_role);

                    // Cannot promote anyone to a rank above your own
                    if (new_rank > actor_space_rank) {
                        res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                            ->writeHeader("Access-Control-Allow-Origin", "*")
                            ->end(R"({"error":"Cannot promote above your own rank"})");
                        return;
                    }

                    // Cannot demote someone of equal or higher rank (unless self-demotion)
                    if (new_rank < target_rank && target_rank >= actor_space_rank && user_id != target_user_id) {
                        res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                            ->writeHeader("Access-Control-Allow-Origin", "*")
                            ->end(R"({"error":"Cannot demote a user of equal or higher rank"})");
                        return;
                    }
                    if (current_role == "owner" && new_role != "owner") {
                        int owner_count = db.count_space_members_with_role(space_id, "owner");
                        if (owner_count <= 1) {
                            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                                ->writeHeader("Access-Control-Allow-Origin", "*")
                                ->end(R"({"error":"Cannot demote last owner","last_owner":true})");
                            return;
                        }
                    }

                    db.update_space_member_role(space_id, target_user_id, new_role);

                    // Notify the target user of their new space role
                    json notify = {{"type", "space_role_changed"}, {"space_id", space_id}, {"role", new_role}};
                    ws.send_to_user(target_user_id, notify.dump());

                    // Broadcast to all space members so they can update their member lists
                    json broadcast = {{"type", "space_member_role_changed"}, {"space_id", space_id}, {"user_id", target_user_id}, {"role", new_role}};
                    ws.broadcast_to_space(space_id, broadcast.dump());

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
                               {"default_join", ch.default_join},
                               {"space_id", ch.space_id},
                               {"is_archived", ch.is_archived},
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

                // Must be a space admin/owner or server admin/owner
                auto u = db.find_user_by_id(user_id);
                bool is_server_admin = u && (u->role == "admin" || u->role == "owner");
                if (!is_server_admin) {
                    std::string space_role = db.get_space_member_role(space_id, user_id);
                    if (space_role != "admin" && space_role != "owner") {
                        res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                            ->writeHeader("Access-Control-Allow-Origin", "*")
                            ->end(R"({"error":"Only space admins and owners can create channels"})");
                        return;
                    }
                }

                try {
                    auto j = json::parse(body);
                    std::string name = j.at("name");
                    std::string description = j.value("description", "");
                    bool is_public = j.value("is_public", true);
                    std::string default_role = j.value("default_role", "write");
                    bool default_join = j.value("default_join", false);

                    std::vector<std::string> member_ids = {user_id};
                    if (j.contains("member_ids")) {
                        for (const auto& mid : j["member_ids"]) {
                            std::string id = mid.get<std::string>();
                            if (id != user_id) member_ids.push_back(id);
                        }
                    }

                    auto ch = db.create_channel(name, description, false, user_id, member_ids,
                                                 is_public, default_role, space_id, default_join);

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
                                         {"default_join", ch.default_join},
                                         {"space_id", ch.space_id},
                                         {"is_archived", ch.is_archived},
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

        // Leave space
        app.post("/api/spaces/:id/leave", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;
            std::string space_id(req->getParameter("id"));

            if (!db.is_space_member(space_id, user_id)) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(R"({"error":"Not a member"})");
                return;
            }

            std::string role = db.get_space_member_role(space_id, user_id);
            auto sp = db.find_space_by_id(space_id);
            if (role == "owner" && sp && !sp->is_archived) {
                int owner_count = db.count_space_members_with_role(space_id, "owner");
                if (owner_count <= 1) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(R"({"error":"You are the last owner. Assign a new owner or archive the space.","last_owner":true})");
                    return;
                }
            }

            // Remove from all space channels
            auto channels = db.list_user_channels(user_id);
            for (const auto& ch : channels) {
                if (ch.space_id == space_id) {
                    db.remove_channel_member(ch.id, user_id);
                    ws.unsubscribe_user_from_channel(user_id, ch.id);
                    json left_notify = {{"type", "member_left"},
                                        {"channel_id", ch.id}, {"user_id", user_id}};
                    ws.broadcast_to_channel(ch.id, left_notify.dump());
                }
            }

            db.remove_space_member(space_id, user_id);
            ws.unsubscribe_user_from_space(user_id, space_id);

            json left_notify = {{"type", "space_member_left"},
                                {"space_id", space_id}, {"user_id", user_id}};
            ws.broadcast_to_space(space_id, left_notify.dump());

            json removed = {{"type", "space_removed"}, {"space_id", space_id}};
            ws.send_to_user(user_id, removed.dump());

            res->writeHeader("Content-Type", "application/json")
                ->writeHeader("Access-Control-Allow-Origin", "*")
                ->end(R"({"ok":true})");
        });

        // Archive space
        app.post("/api/spaces/:id/archive", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;
            std::string space_id(req->getParameter("id"));

            std::string role = db.get_space_member_role(space_id, user_id);
            auto user = db.find_user_by_id(user_id);
            bool is_server_privileged = user && (user->role == "admin" || user->role == "owner");

            if (role != "owner" && !is_server_privileged) {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(R"({"error":"Space owner or server admin required"})");
                return;
            }

            db.archive_space(space_id);

            json notify = {{"type", "space_updated"},
                           {"space", {{"id", space_id}, {"is_archived", true}}}};
            ws.broadcast_to_space(space_id, notify.dump());

            res->writeHeader("Content-Type", "application/json")
                ->writeHeader("Access-Control-Allow-Origin", "*")
                ->end(R"({"ok":true})");
        });

        // Unarchive space
        app.post("/api/spaces/:id/unarchive", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;
            std::string space_id(req->getParameter("id"));

            std::string role = db.get_space_member_role(space_id, user_id);
            auto user = db.find_user_by_id(user_id);
            bool is_server_privileged = user && (user->role == "admin" || user->role == "owner");

            if (role != "owner" && !is_server_privileged) {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(R"({"error":"Space owner or server admin required"})");
                return;
            }

            // Reject if server is archived
            if (db.is_server_archived()) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(R"({"error":"Cannot unarchive: server is archived"})");
                return;
            }

            db.unarchive_space(space_id);

            json notify = {{"type", "space_updated"},
                           {"space", {{"id", space_id}, {"is_archived", false}}}};
            ws.broadcast_to_space(space_id, notify.dump());

            res->writeHeader("Content-Type", "application/json")
                ->writeHeader("Access-Control-Allow-Origin", "*")
                ->end(R"({"ok":true})");
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
                               {"is_archived", ch.is_archived},
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

        // List pending space invites for the authenticated user
        app.get("/api/space-invites", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            auto invites = db.list_pending_space_invites(user_id);
            json arr = json::array();
            for (const auto& inv : invites) {
                arr.push_back({{"id", inv.id}, {"space_id", inv.space_id},
                               {"space_name", inv.space_name},
                               {"invited_by_username", inv.invited_by_username},
                               {"role", inv.role}, {"created_at", inv.created_at}});
            }
            res->writeHeader("Content-Type", "application/json")
                ->writeHeader("Access-Control-Allow-Origin", "*")
                ->end(arr.dump());
        });

        // Accept space invite
        app.post("/api/space-invites/:id/accept", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;
            std::string invite_id(req->getParameter("id"));

            auto invite = db.get_space_invite(invite_id);
            if (!invite || invite->invited_user_id != user_id) {
                res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(R"({"error":"Invite not found"})");
                return;
            }
            if (invite->status != "pending") {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(R"({"error":"Invite is no longer pending"})");
                return;
            }

            db.update_space_invite_status(invite_id, "accepted");
            db.add_space_member(invite->space_id, user_id, invite->role);
            ws.subscribe_user_to_space(user_id, invite->space_id);

            // Auto-join default channels
            auto default_channels = db.get_default_join_channels(invite->space_id);
            for (const auto& ch : default_channels) {
                if (!db.is_channel_member(ch.id, user_id)) {
                    db.add_channel_member(ch.id, user_id, ch.default_role);
                    ws.subscribe_user_to_channel(user_id, ch.id);

                    auto ch_members = db.get_channel_members_with_roles(ch.id);
                    json ch_members_arr = json::array();
                    for (const auto& m : ch_members) {
                        ch_members_arr.push_back({{"id", m.user_id}, {"username", m.username},
                                                  {"display_name", m.display_name},
                                                  {"is_online", m.is_online}, {"last_seen", m.last_seen}, {"role", m.role}});
                    }
                    std::string my_role = db.get_effective_role(ch.id, user_id);
                    json ch_data = {{"id", ch.id}, {"name", ch.name},
                                    {"description", ch.description},
                                    {"is_direct", ch.is_direct},
                                    {"is_public", ch.is_public},
                                    {"default_role", ch.default_role},
                                    {"default_join", ch.default_join},
                                    {"space_id", ch.space_id},
                                    {"is_archived", ch.is_archived},
                                    {"created_at", ch.created_at},
                                    {"my_role", my_role},
                                    {"members", ch_members_arr}};
                    json ch_notify = {{"type", "channel_added"}, {"channel", ch_data}};
                    ws.send_to_user(user_id, ch_notify.dump());
                }
            }

            // Send space_added to the accepting user
            auto sp = db.find_space_by_id(invite->space_id);
            if (sp) {
                auto members = db.get_space_members_with_roles(invite->space_id);
                json members_arr = json::array();
                for (const auto& m : members) {
                    members_arr.push_back({{"id", m.user_id}, {"username", m.username},
                                           {"display_name", m.display_name},
                                           {"is_online", m.is_online}, {"last_seen", m.last_seen}, {"role", m.role}});
                }
                json space_data = {{"id", sp->id}, {"name", sp->name},
                                   {"description", sp->description},
                                   {"is_public", sp->is_public},
                                   {"default_role", sp->default_role},
                                   {"created_at", sp->created_at},
                                   {"is_archived", sp->is_archived},
                                   {"avatar_file_id", sp->avatar_file_id},
                         {"profile_color", sp->profile_color},
                                   {"my_role", invite->role},
                                   {"members", members_arr}};
                json notify = {{"type", "space_added"}, {"space", space_data}};
                ws.send_to_user(user_id, notify.dump());

                // Notify existing space members about the new member
                json update = {{"type", "space_updated"}, {"space", {{"id", sp->id}, {"members", members_arr}}}};
                ws.broadcast_to_space(invite->space_id, update.dump());
            }

            res->writeHeader("Content-Type", "application/json")
                ->writeHeader("Access-Control-Allow-Origin", "*")
                ->end(R"({"ok":true})");
        });

        // Decline space invite
        app.post("/api/space-invites/:id/decline", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;
            std::string invite_id(req->getParameter("id"));

            auto invite = db.get_space_invite(invite_id);
            if (!invite || invite->invited_user_id != user_id) {
                res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(R"({"error":"Invite not found"})");
                return;
            }
            if (invite->status != "pending") {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(R"({"error":"Invite is no longer pending"})");
                return;
            }

            db.update_space_invite_status(invite_id, "declined");
            res->writeHeader("Content-Type", "application/json")
                ->writeHeader("Access-Control-Allow-Origin", "*")
                ->end(R"({"ok":true})");
        });

        // --- Space avatar upload ---
        app.post("/api/spaces/:id/avatar", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;
            std::string space_id(req->getParameter("id"));

            std::string content_type(req->getQuery("content_type"));
            if (content_type.empty()) content_type = "image/png";

            if (content_type.find("image/") != 0) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Only image files are allowed"})");
                return;
            }

            auto body = std::make_shared<std::string>();
            int64_t max_size = 50 * 1024 * 1024;

            res->onData([this, res, body, max_size, user_id, space_id, content_type](std::string_view data, bool last) mutable {
                body->append(data);

                if (static_cast<int64_t>(body->size()) > max_size) {
                    res->writeStatus("413")->writeHeader("Content-Type", "application/json")
                        ->end(R"json({"error":"Image too large (max 20MB)"})json");
                    return;
                }

                if (!last) return;

                // Check permissions
                std::string role = db.get_space_member_role(space_id, user_id);
                auto user = db.find_user_by_id(user_id);
                if (role != "admin" && role != "owner" && !(user && (user->role == "admin" || user->role == "owner"))) {
                    res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(R"({"error":"Admin permission required"})");
                    return;
                }

                try {
                    // Delete old avatar file if exists
                    auto current = db.find_space_by_id(space_id);
                    if (current && !current->avatar_file_id.empty()) {
                        std::string old_path = config.upload_dir + "/" + current->avatar_file_id;
                        std::filesystem::remove(old_path);
                    }

                    // Generate file ID and save
                    std::string file_id = format_utils::random_hex(32);
                    std::string path = config.upload_dir + "/" + file_id;
                    std::ofstream out(path, std::ios::binary);
                    if (!out) {
                        res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                            ->end(R"({"error":"Failed to save image"})");
                        return;
                    }
                    out.write(body->data(), body->size());
                    out.close();

                    db.set_space_avatar(space_id, file_id);
                    auto updated = db.find_space_by_id(space_id);

                    json resp = {{"id", updated->id}, {"name", updated->name},
                                 {"description", updated->description},
                                 {"is_public", updated->is_public},
                                 {"default_role", updated->default_role},
                                 {"avatar_file_id", updated->avatar_file_id},
                                 {"profile_color", updated->profile_color}};

                    // Broadcast to space members
                    json broadcast = {{"type", "space_updated"}, {"space", resp}};
                    ws.broadcast_to_space(space_id, broadcast.dump());

                    res->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(resp.dump());
                } catch (const std::exception& e) {
                    res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                        ->end(json({{"error", e.what()}}).dump());
                }
            });
            res->onAborted([]() {});
        });

        // --- Space avatar delete ---
        app.del("/api/spaces/:id/avatar", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;
            std::string space_id(req->getParameter("id"));

            // Check permissions
            std::string role = db.get_space_member_role(space_id, user_id);
            auto user = db.find_user_by_id(user_id);
            if (role != "admin" && role != "owner" && !(user && (user->role == "admin" || user->role == "owner"))) {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(R"({"error":"Admin permission required"})");
                return;
            }

            try {
                auto current = db.find_space_by_id(space_id);
                if (current && !current->avatar_file_id.empty()) {
                    std::string old_path = config.upload_dir + "/" + current->avatar_file_id;
                    std::filesystem::remove(old_path);
                }

                db.clear_space_avatar(space_id);
                auto updated = db.find_space_by_id(space_id);

                json resp = {{"id", updated->id}, {"name", updated->name},
                             {"description", updated->description},
                             {"is_public", updated->is_public},
                             {"default_role", updated->default_role},
                             {"avatar_file_id", updated->avatar_file_id},
                                 {"profile_color", updated->profile_color}};

                // Broadcast to space members
                json broadcast = {{"type", "space_updated"}, {"space", resp}};
                ws.broadcast_to_space(space_id, broadcast.dump());

                res->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(resp.dump());
            } catch (const std::exception& e) {
                res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                    ->end(json({{"error", e.what()}}).dump());
            }
        });

        // Get enabled tools for a space
        app.get("/api/spaces/:id/tools", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;
            std::string space_id(req->getParameter("id"));

            if (!db.is_space_member(space_id, user_id)) {
                auto user = db.find_user_by_id(user_id);
                if (!user || (user->role != "admin" && user->role != "owner")) {
                    res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Not a member of this space"})");
                    return;
                }
            }

            auto tools = db.get_space_tools(space_id);
            json arr = json::array();
            for (const auto& t : tools) arr.push_back(t);
            res->writeHeader("Content-Type", "application/json")
                ->end(arr.dump());
        });

        // Enable/disable a tool for a space (admin/owner only)
        app.put("/api/spaces/:id/tools", [this](auto* res, auto* req) {
            auto user_id_copy = get_user_id(res, req);
            std::string space_id_copy(req->getParameter("id"));
            std::string body;
            res->onData([this, res, user_id = std::move(user_id_copy),
                         space_id = std::move(space_id_copy), body = std::move(body)](
                std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                if (user_id.empty()) return;
                try {
                    // Check space admin/owner or server admin/owner
                    std::string space_role = db.get_space_member_role(space_id, user_id);
                    auto user = db.find_user_by_id(user_id);
                    bool is_server_admin = user && (user->role == "admin" || user->role == "owner");
                    if (space_role != "admin" && space_role != "owner" && !is_server_admin) {
                        res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                            ->end(R"({"error":"Admin access required"})");
                        return;
                    }

                    auto j = json::parse(body);
                    std::string tool = j.at("tool").get<std::string>();
                    bool enabled = j.at("enabled").get<bool>();

                    // Validate tool name
                    if (tool != "files" && tool != "calendar" && tool != "tasks") {
                        res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                            ->end(R"({"error":"Unknown tool"})");
                        return;
                    }

                    if (enabled) {
                        db.enable_space_tool(space_id, tool, user_id);
                    } else {
                        db.disable_space_tool(space_id, tool);
                    }

                    auto tools = db.get_space_tools(space_id);
                    json tools_arr = json::array();
                    for (const auto& t : tools) tools_arr.push_back(t);

                    json resp = {{"ok", true}, {"enabled_tools", tools_arr}};
                    res->writeHeader("Content-Type", "application/json")->end(resp.dump());
                } catch (const std::exception& e) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->end(json({{"error", e.what()}}).dump());
                }
            });
            res->onAborted([]() {});
        });
}

template <bool SSL>
std::string SpaceHandler<SSL>::get_user_id(uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req) {
    return validate_session_or_401(res, req, db);
}

template struct SpaceHandler<false>;
template struct SpaceHandler<true>;
