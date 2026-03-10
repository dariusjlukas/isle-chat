#pragma once
#include <App.h>
#include <nlohmann/json.hpp>
#include "db/database.h"
#include "auth/webauthn.h"
#include "auth/password.h"
#include "config.h"
#include "ws/ws_handler.h"
#include "handlers/handler_utils.h"

using json = nlohmann::json;

template <bool SSL>
struct AdminHandler {
    Database& db;
    const Config& config;
    WsHandler<SSL>& ws;

    void register_routes(uWS::TemplatedApp<SSL>& app) {
        app.post("/api/admin/invites", [this](auto* res, auto* req) {
            auto user_id_copy = get_admin_id(res, req);
            std::string body;
            res->onData([this, res, user_id = std::move(user_id_copy), body = std::move(body)](
                std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                if (user_id.empty()) return;

                int expiry = defaults::INVITE_EXPIRY_HOURS;
                try {
                    auto j = json::parse(body);
                    expiry = j.value("expiry_hours", defaults::INVITE_EXPIRY_HOURS);
                } catch (...) {}

                auto token = db.create_invite(user_id, expiry);
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
                arr.push_back({{"id", inv.id}, {"token", inv.token},
                               {"created_by", inv.created_by_username},
                               {"used", inv.used},
                               {"expires_at", inv.expires_at},
                               {"created_at", inv.created_at}});
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
            auto user_id = get_admin_id(res, req);
            if (user_id.empty()) return;

            json resp = build_settings_response();
            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        });

        app.put("/api/admin/settings", [this](auto* res, auto* req) {
            auto admin_id_copy = get_admin_id(res, req);
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
            auto admin_id_copy = get_admin_id(res, req);
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
                arr.push_back({{"id", t.id}, {"token", t.token},
                               {"created_by", t.created_by_username},
                               {"for_user", t.for_username},
                               {"for_user_id", t.for_user_id},
                               {"used", t.used},
                               {"expires_at", t.expires_at},
                               {"created_at", t.created_at}});
            }
            res->writeHeader("Content-Type", "application/json")->end(arr.dump());
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
                               {"is_online", u.is_online}, {"last_seen", u.last_seen}});
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
    }

private:
    std::string get_admin_id(uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req) {
        return validate_admin_or_403(res, req, db);
    }

    std::string get_owner_id(uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req) {
        return validate_owner_or_403(res, req, db);
    }

    std::string get_setting_or(const std::string& key, const std::string& fallback) {
        auto val = db.get_setting(key);
        return val.value_or(fallback);
    }

    json build_settings_response() {
        auto max_file = db.get_setting("max_file_size");
        auto max_storage = db.get_setting("max_storage_size");
        int64_t storage_used = db.get_total_file_size();

        json auth_methods = get_auth_methods(db);

        return {
            {"max_file_size", max_file ? std::stoll(*max_file) : config.max_file_size},
            {"max_storage_size", max_storage ? std::stoll(*max_storage) : 0},
            {"storage_used", storage_used},
            {"auth_methods", auth_methods},
            {"server_name", get_setting_or("server_name", "Isle Chat")},
            {"registration_mode", get_setting_or("registration_mode", "invite")},
            {"file_uploads_enabled", get_setting_or("file_uploads_enabled", "true") == "true"},
            {"session_expiry_hours", std::stoi(get_setting_or("session_expiry_hours",
                                     std::to_string(config.session_expiry_hours)))},
            {"setup_completed", get_setting_or("setup_completed", "false") == "true"},
            {"server_archived", db.is_server_archived()},
            {"password_min_length", std::stoi(get_setting_or("password_min_length", "8"))},
            {"password_require_uppercase", get_setting_or("password_require_uppercase", "true") == "true"},
            {"password_require_lowercase", get_setting_or("password_require_lowercase", "true") == "true"},
            {"password_require_number", get_setting_or("password_require_number", "true") == "true"},
            {"password_require_special", get_setting_or("password_require_special", "false") == "true"},
            {"password_max_age_days", std::stoi(get_setting_or("password_max_age_days", "0"))},
            {"password_history_count", std::stoi(get_setting_or("password_history_count", "0"))},
            {"mfa_required_password", get_setting_or("mfa_required_password", "false") == "true"},
            {"mfa_required_pki", get_setting_or("mfa_required_pki", "false") == "true"},
            {"mfa_required_passkey", get_setting_or("mfa_required_passkey", "false") == "true"}
        };
    }

    void save_settings(uWS::HttpResponse<SSL>* res, const std::string& body, bool mark_setup = false) {
        try {
            auto j = json::parse(body);

            if (j.contains("max_file_size")) {
                db.set_setting("max_file_size", std::to_string(j["max_file_size"].get<int64_t>()));
            }
            if (j.contains("max_storage_size")) {
                db.set_setting("max_storage_size", std::to_string(j["max_storage_size"].get<int64_t>()));
            }
            if (j.contains("auth_methods")) {
                auto& arr = j["auth_methods"];
                if (!arr.is_array() || arr.empty()) {
                    throw std::runtime_error("auth_methods must be a non-empty array");
                }
                for (const auto& m : arr) {
                    auto s = m.get<std::string>();
                    if (s != "passkey" && s != "pki" && s != "password") {
                        throw std::runtime_error("Invalid auth method: " + s);
                    }
                }
                db.set_setting("auth_methods", arr.dump());
            }
            if (j.contains("server_name")) {
                db.set_setting("server_name", j["server_name"].get<std::string>());
            }
            if (j.contains("registration_mode")) {
                auto mode = j["registration_mode"].get<std::string>();
                if (mode != "invite" && mode != "invite_only" && mode != "approval" && mode != "open") {
                    throw std::runtime_error("Invalid registration mode: " + mode);
                }
                db.set_setting("registration_mode", mode);
            }
            if (j.contains("file_uploads_enabled")) {
                db.set_setting("file_uploads_enabled",
                               j["file_uploads_enabled"].get<bool>() ? "true" : "false");
            }
            if (j.contains("session_expiry_hours")) {
                int hours = j["session_expiry_hours"].get<int>();
                if (hours <= 0) throw std::runtime_error("Session expiry must be positive");
                db.set_setting("session_expiry_hours", std::to_string(hours));
            }

            // Password policy settings
            if (j.contains("password_min_length")) {
                int v = j["password_min_length"].get<int>();
                if (v < 1) throw std::runtime_error("Minimum password length must be at least 1");
                db.set_setting("password_min_length", std::to_string(v));
            }
            if (j.contains("password_require_uppercase"))
                db.set_setting("password_require_uppercase",
                               j["password_require_uppercase"].get<bool>() ? "true" : "false");
            if (j.contains("password_require_lowercase"))
                db.set_setting("password_require_lowercase",
                               j["password_require_lowercase"].get<bool>() ? "true" : "false");
            if (j.contains("password_require_number"))
                db.set_setting("password_require_number",
                               j["password_require_number"].get<bool>() ? "true" : "false");
            if (j.contains("password_require_special"))
                db.set_setting("password_require_special",
                               j["password_require_special"].get<bool>() ? "true" : "false");
            if (j.contains("password_max_age_days")) {
                int v = j["password_max_age_days"].get<int>();
                if (v < 0) throw std::runtime_error("Password max age must be non-negative");
                db.set_setting("password_max_age_days", std::to_string(v));
            }
            if (j.contains("password_history_count")) {
                int v = j["password_history_count"].get<int>();
                if (v < 0) throw std::runtime_error("Password history count must be non-negative");
                db.set_setting("password_history_count", std::to_string(v));
            }

            // MFA requirements
            if (j.contains("mfa_required_password")) {
                db.set_setting("mfa_required_password", j["mfa_required_password"].get<bool>() ? "true" : "false");
            }
            if (j.contains("mfa_required_pki")) {
                db.set_setting("mfa_required_pki", j["mfa_required_pki"].get<bool>() ? "true" : "false");
            }
            if (j.contains("mfa_required_passkey")) {
                db.set_setting("mfa_required_passkey", j["mfa_required_passkey"].get<bool>() ? "true" : "false");
            }

            if (mark_setup) {
                db.set_setting("setup_completed", "true");
            }

            res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
        } catch (const std::exception& e) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(json{{"error", e.what()}}.dump());
        }
    }

    int get_session_expiry_hours() {
        return ::get_session_expiry(db, config);
    }

    void handle_approve(uWS::HttpResponse<SSL>* res, const std::string& request_id,
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
            if (request->auth_method == "pki" && !request->credential_data.empty()) {
                auto cred = json::parse(request->credential_data);
                std::string public_key = cred.at("public_key");
                db.store_pki_credential(user.id, public_key);

                // Generate recovery keys for PKI users
                auto [plaintext_keys, key_hashes] = webauthn::generate_recovery_keys();
                db.store_recovery_keys(user.id, key_hashes);
            } else if (request->auth_method == "passkey" && !request->credential_data.empty()) {
                auto cred = json::parse(request->credential_data);
                std::string credential_id = cred.at("credential_id");
                auto pk_b64 = cred.at("public_key").get<std::string>();
                auto pk_bytes = webauthn::base64url_decode(pk_b64);
                uint32_t sign_count = cred.value("sign_count", 0);
                std::string transports = cred.value("transports", "[]");
                db.store_webauthn_credential(user.id, credential_id, pk_bytes, sign_count, "Passkey", transports);
            } else if (request->auth_method == "password" && !request->credential_data.empty()) {
                auto cred = json::parse(request->credential_data);
                std::string password_hash = cred.at("password_hash");
                db.store_password(user.id, password_hash);
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
};
