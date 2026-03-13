#include "handlers/admin_handler.h"
#include "handlers/admin_approval_utils.h"
#include "handlers/admin_settings_utils.h"
#include "handlers/format_utils.h"
#include <pqxx/pqxx>
#include <fstream>
#include <filesystem>
#include "auth/webauthn.h"
#include "auth/password.h"

using json = nlohmann::json;

template <bool SSL>
void AdminHandler<SSL>::register_routes(uWS::TemplatedApp<SSL>& app) {
    app.post("/api/admin/invites", [this](auto* res, auto* req) {
        auto user_id_copy = get_admin_id(res, req);
        std::string body;
        res->onData([this, res, user_id = std::move(user_id_copy), body = std::move(body)](
            std::string_view data, bool last) mutable {
            body.append(data);
            if (!last) return;
            if (user_id.empty()) return;

            int expiry = defaults::INVITE_EXPIRY_HOURS;
            int max_uses = 1;
            try {
                auto j = json::parse(body);
                expiry = j.value("expiry_hours", defaults::INVITE_EXPIRY_HOURS);
                if (expiry < 1) expiry = 1;
                if (expiry > defaults::MAX_INVITE_EXPIRY_HOURS) expiry = defaults::MAX_INVITE_EXPIRY_HOURS;
                max_uses = j.value("max_uses", 1);
                if (max_uses < 0) max_uses = 0;
            } catch (...) {}

            auto token = db.create_invite(user_id, expiry, max_uses);
            json resp = {{"token", token}};
            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        });
        res->onAborted([]() {});
    });

    app.get("/api/admin/invites", [this](auto* res, auto* req) {
        auto user_id = get_admin_id(res, req);
        if (user_id.empty()) return;

        auto invites = db.list_invites();
        json arr = json::array();
        for (const auto& inv : invites) {
            json obj = {{"id", inv.id}, {"token", inv.token},
                        {"created_by", inv.created_by_username},
                        {"used", inv.used},
                        {"expires_at", inv.expires_at},
                        {"created_at", inv.created_at},
                        {"max_uses", inv.max_uses},
                        {"use_count", inv.use_count}};
            json uses_arr = json::array();
            for (const auto& u : inv.uses) {
                uses_arr.push_back({{"username", u.username}, {"used_at", u.used_at}});
            }
            obj["uses"] = uses_arr;
            arr.push_back(obj);
        }
        res->writeHeader("Content-Type", "application/json")->end(arr.dump());
    });

    app.del("/api/admin/invites/:id", [this](auto* res, auto* req) {
        auto user_id = get_admin_id(res, req);
        if (user_id.empty()) return;

        std::string invite_id(req->getParameter("id"));
        bool deleted = db.revoke_invite(invite_id);
        if (!deleted) {
            res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Invite not found or already used"})");
            return;
        }
        res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
    });

    app.get("/api/admin/requests", [this](auto* res, auto* req) {
        auto user_id = get_admin_id(res, req);
        if (user_id.empty()) return;

        auto requests = db.list_pending_requests();
        json arr = json::array();
        for (const auto& r : requests) {
            arr.push_back({{"id", r.id}, {"username", r.username},
                           {"display_name", r.display_name},
                           {"auth_method", r.auth_method},
                           {"status", r.status}, {"created_at", r.created_at}});
        }
        res->writeHeader("Content-Type", "application/json")->end(arr.dump());
    });

    app.post("/api/admin/requests/:id/approve", [this](auto* res, auto* req) {
        auto admin_id = get_admin_id(res, req);
        if (admin_id.empty()) return;

        std::string request_id(req->getParameter("id"));
        handle_approve(res, request_id, admin_id);
    });

    app.post("/api/admin/requests/:id/deny", [this](auto* res, auto* req) {
        auto admin_id = get_admin_id(res, req);
        if (admin_id.empty()) return;

        std::string request_id(req->getParameter("id"));
        db.update_join_request(request_id, "denied", admin_id);
        res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
    });

    app.get("/api/admin/settings", [this](auto* res, auto* req) {
        auto user_id = get_owner_id(res, req);
        if (user_id.empty()) return;

        json resp = build_settings_response();
        res->writeHeader("Content-Type", "application/json")->end(resp.dump());
    });

    app.put("/api/admin/settings", [this](auto* res, auto* req) {
        auto admin_id_copy = get_owner_id(res, req);
        std::string body;
        res->onData([this, res, admin_id = std::move(admin_id_copy), body = std::move(body)](
            std::string_view data, bool last) mutable {
            body.append(data);
            if (!last) return;
            if (admin_id.empty()) return;
            save_settings(res, body);
        });
        res->onAborted([]() {});
    });

    app.post("/api/admin/setup", [this](auto* res, auto* req) {
        auto admin_id_copy = get_owner_id(res, req);
        std::string body;
        res->onData([this, res, admin_id = std::move(admin_id_copy), body = std::move(body)](
            std::string_view data, bool last) mutable {
            body.append(data);
            if (!last) return;
            if (admin_id.empty()) return;

            auto completed = db.get_setting("setup_completed");
            if (completed && *completed == "true") {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Setup already completed"})");
                return;
            }

            save_settings(res, body, true);
        });
        res->onAborted([]() {});
    });

    app.post("/api/admin/recovery-tokens", [this](auto* res, auto* req) {
        auto admin_id_copy = get_admin_id(res, req);
        std::string body;
        res->onData([this, res, admin_id = std::move(admin_id_copy), body = std::move(body)](
            std::string_view data, bool last) mutable {
            body.append(data);
            if (!last) return;
            if (admin_id.empty()) return;

            try {
                auto j = json::parse(body);
                std::string user_id = j.at("user_id");
                int expiry = j.value("expiry_hours", defaults::RECOVERY_TOKEN_EXPIRY_HOURS);

                auto user = db.find_user_by_id(user_id);
                if (!user) {
                    res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"User not found"})");
                    return;
                }

                auto token = db.create_recovery_token(admin_id, user_id, expiry);
                json resp = {{"token", token}};
                res->writeHeader("Content-Type", "application/json")->end(resp.dump());
            } catch (const std::exception& e) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(json({{"error", e.what()}}).dump());
            }
        });
        res->onAborted([]() {});
    });

    app.get("/api/admin/recovery-tokens", [this](auto* res, auto* req) {
        auto user_id = get_admin_id(res, req);
        if (user_id.empty()) return;

        auto tokens = db.list_recovery_tokens();
        json arr = json::array();
        for (const auto& t : tokens) {
            json obj = {{"id", t.id}, {"token", t.token},
                        {"created_by", t.created_by_username},
                        {"for_user", t.for_username},
                        {"for_user_id", t.for_user_id},
                        {"used", t.used},
                        {"expires_at", t.expires_at},
                        {"created_at", t.created_at}};
            if (!t.used_at.empty()) obj["used_at"] = t.used_at;
            arr.push_back(obj);
        }
        res->writeHeader("Content-Type", "application/json")->end(arr.dump());
    });

    app.del("/api/admin/recovery-tokens/:id", [this](auto* res, auto* req) {
        auto admin_id = get_admin_id(res, req);
        if (admin_id.empty()) return;

        std::string token_id(req->getParameter("id"));
        if (db.delete_recovery_token(token_id)) {
            res->writeHeader("Content-Type", "application/json")
                ->end(R"({"ok":true})");
        } else {
            res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Token not found or already used"})");
        }
    });

    // Upload server icon (owner only)
    app.post("/api/admin/server-icon", [this](auto* res, auto* req) {
        auto user_id_copy = get_owner_id(res, req);
        auto body = std::make_shared<std::string>();
        int64_t max_size = 50 * 1024 * 1024;

        res->onData([this, res, body, max_size, user_id = std::move(user_id_copy)](std::string_view data, bool last) mutable {
            body->append(data);
            if (static_cast<int64_t>(body->size()) > max_size) {
                res->writeStatus("413")->writeHeader("Content-Type", "application/json")
                    ->end(R"json({"error":"Icon too large (max 50MB)"})json");
                return;
            }
            if (!last) return;
            if (user_id.empty()) return;

            try {
                // Delete old icon file if exists
                auto old_id = db.get_setting("server_icon_file_id");
                if (old_id && !old_id->empty()) {
                    std::string old_path = config.upload_dir + "/" + *old_id;
                    std::filesystem::remove(old_path);
                }

                // Save new icon
                std::string file_id = format_utils::random_hex(32);
                std::string path = config.upload_dir + "/" + file_id;
                std::ofstream out(path, std::ios::binary);
                if (!out) {
                    res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Failed to save icon"})");
                    return;
                }
                out.write(body->data(), body->size());
                out.close();

                db.set_setting("server_icon_file_id", file_id);

                json resp = {{"server_icon_file_id", file_id}};
                res->writeHeader("Content-Type", "application/json")->end(resp.dump());
            } catch (const std::exception& e) {
                res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                    ->end(json({{"error", e.what()}}).dump());
            }
        });
        res->onAborted([]() {});
    });

    // Delete server icon (owner only)
    app.del("/api/admin/server-icon", [this](auto* res, auto* req) {
        auto user_id = get_owner_id(res, req);
        if (user_id.empty()) return;

        try {
            auto old_id = db.get_setting("server_icon_file_id");
            if (old_id && !old_id->empty()) {
                std::string old_path = config.upload_dir + "/" + *old_id;
                std::filesystem::remove(old_path);
            }
            db.set_setting("server_icon_file_id", "");
            res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
        } catch (const std::exception& e) {
            res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    });

    // Upload dark mode server icon (owner only)
    app.post("/api/admin/server-icon-dark", [this](auto* res, auto* req) {
        auto user_id_copy = get_owner_id(res, req);
        auto body = std::make_shared<std::string>();
        int64_t max_size = 50 * 1024 * 1024;

        res->onData([this, res, body, max_size, user_id = std::move(user_id_copy)](std::string_view data, bool last) mutable {
            body->append(data);
            if (static_cast<int64_t>(body->size()) > max_size) {
                res->writeStatus("413")->writeHeader("Content-Type", "application/json")
                    ->end(R"json({"error":"Icon too large (max 50MB)"})json");
                return;
            }
            if (!last) return;
            if (user_id.empty()) return;

            try {
                // Delete old dark icon file if exists
                auto old_id = db.get_setting("server_icon_dark_file_id");
                if (old_id && !old_id->empty()) {
                    std::string old_path = config.upload_dir + "/" + *old_id;
                    std::filesystem::remove(old_path);
                }

                // Save new dark icon
                std::string file_id = format_utils::random_hex(32);
                std::string path = config.upload_dir + "/" + file_id;
                std::ofstream out(path, std::ios::binary);
                if (!out) {
                    res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Failed to save icon"})");
                    return;
                }
                out.write(body->data(), body->size());
                out.close();

                db.set_setting("server_icon_dark_file_id", file_id);

                json resp = {{"server_icon_dark_file_id", file_id}};
                res->writeHeader("Content-Type", "application/json")->end(resp.dump());
            } catch (const std::exception& e) {
                res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                    ->end(json({{"error", e.what()}}).dump());
            }
        });
        res->onAborted([]() {});
    });

    // Delete dark mode server icon (owner only)
    app.del("/api/admin/server-icon-dark", [this](auto* res, auto* req) {
        auto user_id = get_owner_id(res, req);
        if (user_id.empty()) return;

        try {
            auto old_id = db.get_setting("server_icon_dark_file_id");
            if (old_id && !old_id->empty()) {
                std::string old_path = config.upload_dir + "/" + *old_id;
                std::filesystem::remove(old_path);
            }
            db.set_setting("server_icon_dark_file_id", "");
            res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
        } catch (const std::exception& e) {
            res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    });

    // Archive server (owner only)
    app.post("/api/admin/archive-server", [this](auto* res, auto* req) {
        auto user_id = get_owner_id(res, req);
        if (user_id.empty()) return;
        db.set_server_archived(true);
        json notify = {{"type", "server_archived_changed"}, {"archived", true}};
        ws.broadcast_to_presence(notify.dump());
        res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
    });

    app.post("/api/admin/unarchive-server", [this](auto* res, auto* req) {
        auto user_id = get_owner_id(res, req);
        if (user_id.empty()) return;
        db.set_server_archived(false);
        json notify = {{"type", "server_archived_changed"}, {"archived", false}};
        ws.broadcast_to_presence(notify.dump());
        res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
    });

    // List users (admin or owner)
    app.get("/api/admin/users", [this](auto* res, auto* req) {
        auto user_id = get_admin_id(res, req);
        if (user_id.empty()) return;

        auto users = db.list_users();
        json arr = json::array();
        for (const auto& u : users) {
            arr.push_back({{"id", u.id}, {"username", u.username},
                           {"display_name", u.display_name}, {"role", u.role},
                           {"is_online", u.is_online}, {"last_seen", u.last_seen},
                           {"is_banned", u.is_banned}});
        }
        res->writeHeader("Content-Type", "application/json")->end(arr.dump());
    });

    // Change user role (admin or owner, with hierarchy enforcement)
    app.put("/api/admin/users/:userId/role", [this](auto* res, auto* req) {
        auto actor_id_copy = get_admin_id(res, req);
        std::string target_user_id(req->getParameter("userId"));
        std::string body;
        res->onData([this, res, actor_id = std::move(actor_id_copy),
                     target_user_id = std::move(target_user_id), body = std::move(body)](
            std::string_view data, bool last) mutable {
            body.append(data);
            if (!last) return;
            if (actor_id.empty()) return;

            try {
                auto j = json::parse(body);
                std::string new_role = j.at("role");
                if (new_role != "owner" && new_role != "admin" && new_role != "user") {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Invalid role. Must be owner, admin, or user"})");
                    return;
                }

                auto actor = db.find_user_by_id(actor_id);
                auto target = db.find_user_by_id(target_user_id);
                if (!actor || !target) {
                    res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"User not found"})");
                    return;
                }

                int actor_rank = server_role_rank(actor->role);
                int target_rank = server_role_rank(target->role);
                int new_rank = server_role_rank(new_role);

                // Cannot promote anyone to a rank above your own
                if (new_rank > actor_rank) {
                    res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Cannot promote above your own rank"})");
                    return;
                }

                // Cannot demote someone of equal or higher rank (unless self-demotion)
                if (new_rank < target_rank && target_rank >= actor_rank && actor_id != target_user_id) {
                    res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Cannot demote a user of equal or higher rank"})");
                    return;
                }

                // Prevent demoting the last owner
                if (target->role == "owner" && new_role != "owner") {
                    int owner_count = db.count_users_with_role("owner");
                    if (owner_count <= 1) {
                        res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                            ->end(R"({"error":"Cannot demote the last owner. Assign a new owner or archive the server first.","last_owner":true})");
                        return;
                    }
                }

                db.update_user_role(target_user_id, new_role);

                // Notify the target user of their new server role
                json notify = {{"type", "server_role_changed"}, {"role", new_role}};
                ws.send_to_user(target_user_id, notify.dump());

                // Broadcast to all connected users so they can update user cards
                json broadcast = {{"type", "user_role_changed"}, {"user_id", target_user_id}, {"role", new_role}};
                ws.broadcast_to_presence(broadcast.dump());

                res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
            } catch (const std::exception& e) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(json({{"error", e.what()}}).dump());
            }
        });
        res->onAborted([]() {});
    });

    // Ban user (admin or owner, with hierarchy enforcement)
    app.post("/api/admin/users/:userId/ban", [this](auto* res, auto* req) {
        auto actor_id_copy = get_admin_id(res, req);
        std::string target_user_id(req->getParameter("userId"));
        std::string body;
        res->onData([this, res, actor_id = std::move(actor_id_copy),
                     target_user_id = std::move(target_user_id), body = std::move(body)](
            std::string_view data, bool last) mutable {
            body.append(data);
            if (!last) return;
            if (actor_id.empty()) return;

            try {
                auto actor = db.find_user_by_id(actor_id);
                auto target = db.find_user_by_id(target_user_id);
                if (!actor || !target) {
                    res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"User not found"})");
                    return;
                }

                if (target->is_banned) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"User is already banned"})");
                    return;
                }

                int actor_rank = server_role_rank(actor->role);
                int target_rank = server_role_rank(target->role);

                // Cannot ban someone of equal or higher rank
                if (target_rank >= actor_rank) {
                    res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Cannot ban a user of equal or higher rank"})");
                    return;
                }

                // Cannot ban yourself
                if (actor_id == target_user_id) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Cannot ban yourself"})");
                    return;
                }

                db.ban_user(target_user_id, actor_id);

                // Notify the banned user before disconnecting
                json notify = {{"type", "banned"}};
                ws.send_to_user(target_user_id, notify.dump());

                // Force disconnect the banned user's WebSocket connections
                ws.disconnect_user(target_user_id);

                // Broadcast to all connected users
                json broadcast = {{"type", "user_banned"}, {"user_id", target_user_id}};
                ws.broadcast_to_presence(broadcast.dump());

                res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
            } catch (const std::exception& e) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(json({{"error", e.what()}}).dump());
            }
        });
        res->onAborted([]() {});
    });

    // Unban user (admin or owner, with hierarchy enforcement)
    app.del("/api/admin/users/:userId/ban", [this](auto* res, auto* req) {
        auto actor_id = get_admin_id(res, req);
        if (actor_id.empty()) return;

        std::string target_user_id(req->getParameter("userId"));

        auto actor = db.find_user_by_id(actor_id);
        auto target = db.find_user_by_id(target_user_id);
        if (!actor || !target) {
            res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"User not found"})");
            return;
        }

        if (!target->is_banned) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"User is not banned"})");
            return;
        }

        int actor_rank = server_role_rank(actor->role);
        int target_rank = server_role_rank(target->role);

        // Cannot unban someone of equal or higher rank
        if (target_rank >= actor_rank) {
            res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Cannot unban a user of equal or higher rank"})");
            return;
        }

        db.unban_user(target_user_id);

        // Broadcast to all connected users
        json broadcast = {{"type", "user_unbanned"}, {"user_id", target_user_id}};
        ws.broadcast_to_presence(broadcast.dump());

        res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
    });
}

template <bool SSL>
std::string AdminHandler<SSL>::get_admin_id(uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req) {
    return validate_admin_or_403(res, req, db);
}

template <bool SSL>
std::string AdminHandler<SSL>::get_owner_id(uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req) {
    return validate_owner_or_403(res, req, db);
}

template <bool SSL>
std::string AdminHandler<SSL>::get_setting_or(const std::string& key, const std::string& fallback) {
    auto val = db.get_setting(key);
    return val.value_or(fallback);
}

template <bool SSL>
json AdminHandler<SSL>::build_settings_response() {
    admin_settings::Snapshot snapshot;
    snapshot.config_max_file_size = config.max_file_size;
    snapshot.config_session_expiry_hours = config.session_expiry_hours;
    snapshot.storage_used = db.get_total_file_size();
    snapshot.server_archived = db.is_server_archived();

    snapshot.max_file_size = db.get_setting("max_file_size");
    snapshot.max_storage_size = db.get_setting("max_storage_size");
    snapshot.auth_methods = db.get_setting("auth_methods");
    snapshot.server_name = db.get_setting("server_name");
    snapshot.server_icon_file_id = db.get_setting("server_icon_file_id");
    snapshot.server_icon_dark_file_id = db.get_setting("server_icon_dark_file_id");
    snapshot.registration_mode = db.get_setting("registration_mode");
    snapshot.file_uploads_enabled = db.get_setting("file_uploads_enabled");
    snapshot.session_expiry_hours = db.get_setting("session_expiry_hours");
    snapshot.setup_completed = db.get_setting("setup_completed");
    snapshot.password_min_length = db.get_setting("password_min_length");
    snapshot.password_require_uppercase = db.get_setting("password_require_uppercase");
    snapshot.password_require_lowercase = db.get_setting("password_require_lowercase");
    snapshot.password_require_number = db.get_setting("password_require_number");
    snapshot.password_require_special = db.get_setting("password_require_special");
    snapshot.password_max_age_days = db.get_setting("password_max_age_days");
    snapshot.password_history_count = db.get_setting("password_history_count");
    snapshot.mfa_required_password = db.get_setting("mfa_required_password");
    snapshot.mfa_required_pki = db.get_setting("mfa_required_pki");
    snapshot.mfa_required_passkey = db.get_setting("mfa_required_passkey");

    return admin_settings::build_settings_response(snapshot);
}

template <bool SSL>
void AdminHandler<SSL>::save_settings(uWS::HttpResponse<SSL>* res, const std::string& body, bool mark_setup) {
    try {
        auto updates = admin_settings::collect_settings_updates(json::parse(body), mark_setup);
        for (const auto& [key, value] : updates) {
            db.set_setting(key, value);
        }

        res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
    } catch (const std::exception& e) {
        res->writeStatus("400")->writeHeader("Content-Type", "application/json")
            ->end(json{{"error", e.what()}}.dump());
    }
}

template <bool SSL>
int AdminHandler<SSL>::get_session_expiry_hours() {
    return ::get_session_expiry(db, config);
}

template <bool SSL>
void AdminHandler<SSL>::handle_approve(uWS::HttpResponse<SSL>* res, const std::string& request_id,
                     const std::string& admin_id) {
    try {
        auto request = db.get_join_request(request_id);
        if (!request) {
            res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Request not found"})");
            return;
        }

        if (request->status != "pending") {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Request already processed"})");
            return;
        }

        // Create user account from join request
        auto user = db.create_user(request->username, request->display_name, "", "user");

        // Store credentials based on auth method
        auto parsed = admin_approval_utils::parse_credential_data(
            request->auth_method,
            request->credential_data,
            []() { return webauthn::generate_recovery_keys(); });

        if (parsed.pki_public_key) {
            db.store_pki_credential(user.id, *parsed.pki_public_key);
            db.store_recovery_keys(user.id, parsed.recovery_key_hashes);
        } else if (parsed.passkey_credential_id) {
            db.store_webauthn_credential(user.id, *parsed.passkey_credential_id,
                                         parsed.passkey_public_key, parsed.passkey_sign_count,
                                         "Passkey", parsed.passkey_transports);
        } else if (parsed.password_hash) {
            db.store_password(user.id, *parsed.password_hash);
        }

        // Create session token for polling pickup
        std::string session_token = db.create_session(user.id, get_session_expiry_hours());
        db.set_join_request_session(request_id, session_token);
        db.update_join_request(request_id, "approved", admin_id);

        json resp = {{"ok", true}};
        res->writeHeader("Content-Type", "application/json")->end(resp.dump());
    } catch (const pqxx::unique_violation&) {
        res->writeStatus("409")->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Username already taken"})");
    } catch (const std::exception& e) {
        res->writeStatus("400")->writeHeader("Content-Type", "application/json")
            ->end(json({{"error", e.what()}}).dump());
    }
}

template struct AdminHandler<false>;
template struct AdminHandler<true>;
