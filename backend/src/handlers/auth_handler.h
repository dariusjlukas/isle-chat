#pragma once
#include <App.h>
#include <nlohmann/json.hpp>
#include "db/database.h"
#include "auth/pki.h"
#include "config.h"

using json = nlohmann::json;

template <bool SSL>
struct AuthHandler {
    Database& db;
    const Config& config;

    void register_routes(uWS::TemplatedApp<SSL>& app) {
        app.post("/api/auth/challenge", [this](auto* res, auto* req) {
            std::string body;
            res->onData([this, res, body = std::move(body)](std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                handle_challenge(res, body);
            });
            res->onAborted([]() {});
        });

        app.post("/api/auth/verify", [this](auto* res, auto* req) {
            std::string body;
            res->onData([this, res, body = std::move(body)](std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                handle_verify(res, body);
            });
            res->onAborted([]() {});
        });

        app.post("/api/auth/register", [this](auto* res, auto* req) {
            std::string body;
            res->onData([this, res, body = std::move(body)](std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                handle_register(res, body);
            });
            res->onAborted([]() {});
        });

        app.post("/api/auth/request-access", [this](auto* res, auto* req) {
            std::string body;
            res->onData([this, res, body = std::move(body)](std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                handle_request_access(res, body);
            });
            res->onAborted([]() {});
        });

        app.post("/api/auth/add-device", [this](auto* res, auto* req) {
            std::string body;
            res->onData([this, res, body = std::move(body)](std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                handle_add_device(res, body);
            });
            res->onAborted([]() {});
        });

        app.post("/api/auth/logout", [this](auto* res, auto* req) {
            std::string token(req->getHeader("authorization"));
            if (token.rfind("Bearer ", 0) == 0) token = token.substr(7);
            db.delete_session(token);
            res->writeHeader("Content-Type", "application/json")
                ->end(R"({"ok":true})");
        });
    }

private:
    void handle_challenge(uWS::HttpResponse<SSL>* res, const std::string& body) {
        try {
            auto j = json::parse(body);
            std::string public_key = j.at("public_key");

            auto user = db.find_user_by_public_key(public_key);
            if (!user) {
                res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"User not found"})");
                return;
            }

            std::string challenge = pki::generate_challenge();
            db.store_challenge(public_key, challenge);

            json resp = {{"challenge", challenge}};
            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        } catch (const std::exception& e) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    }

    void handle_verify(uWS::HttpResponse<SSL>* res, const std::string& body) {
        try {
            auto j = json::parse(body);
            std::string public_key = j.at("public_key");
            std::string challenge = j.at("challenge");
            std::string signature = j.at("signature");

            // Verify the challenge exists and matches
            auto stored = db.get_challenge(public_key);
            if (!stored || *stored != challenge) {
                res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Invalid or expired challenge"})");
                return;
            }

            // Verify the signature
            if (!pki::verify_signature(public_key, challenge, signature)) {
                res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Invalid signature"})");
                return;
            }

            // Find user and create session
            auto user = db.find_user_by_public_key(public_key);
            if (!user) {
                res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"User not found"})");
                return;
            }

            db.delete_challenge(public_key);
            std::string token = db.create_session(user->id, config.session_expiry_hours);

            json resp = {
                {"token", token},
                {"user", {
                    {"id", user->id},
                    {"username", user->username},
                    {"display_name", user->display_name},
                    {"role", user->role},
                    {"bio", user->bio},
                    {"status", user->status}
                }}
            };
            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        } catch (const std::exception& e) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    }

    void handle_register(uWS::HttpResponse<SSL>* res, const std::string& body) {
        try {
            auto j = json::parse(body);
            std::string username = j.at("username");
            std::string display_name = j.at("display_name");
            std::string public_key = j.at("public_key");
            std::string invite_token = j.value("token", "");

            // Check if this is the first user (becomes admin)
            bool is_first_user = (db.count_users() == 0);

            if (!is_first_user && invite_token.empty()) {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Invite token required"})");
                return;
            }

            if (!is_first_user && !db.validate_invite(invite_token)) {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Invalid or expired invite token"})");
                return;
            }

            // Check if username or public key already exists
            auto existing = db.find_user_by_public_key(public_key);
            if (existing) {
                res->writeStatus("409")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Public key already registered"})");
                return;
            }

            std::string role = is_first_user ? "admin" : "user";
            auto user = db.create_user(username, display_name, public_key, role);

            if (!invite_token.empty()) {
                db.use_invite(invite_token, user.id);
            }

            // If first user, create "general" channel (public, default read-only)
            if (is_first_user) {
                db.create_channel("general", "General discussion", false, user.id, {user.id},
                                   true, "read");
            } else {
                // Add new user to general channel with its default role
                auto general = db.find_general_channel();
                if (general) {
                    db.add_channel_member(general->id, user.id, general->default_role);
                }
            }

            // Create session
            std::string token = db.create_session(user.id, config.session_expiry_hours);

            json resp = {
                {"token", token},
                {"user", {
                    {"id", user.id},
                    {"username", user.username},
                    {"display_name", user.display_name},
                    {"role", user.role},
                    {"bio", user.bio},
                    {"status", user.status}
                }}
            };
            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        } catch (const pqxx::unique_violation&) {
            res->writeStatus("409")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Username already taken"})");
        } catch (const std::exception& e) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    }

    void handle_add_device(uWS::HttpResponse<SSL>* res, const std::string& body) {
        try {
            auto j = json::parse(body);
            std::string device_token = j.at("device_token");
            std::string public_key = j.at("public_key");
            std::string device_name = j.value("device_name", "New Device");

            auto user_id = db.validate_device_token(device_token);
            if (!user_id) {
                res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Invalid or expired device token"})");
                return;
            }

            auto existing = db.find_user_by_public_key(public_key);
            if (existing) {
                res->writeStatus("409")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"This key is already registered"})");
                return;
            }

            db.add_user_key(*user_id, public_key, device_name);
            db.mark_device_token_used(device_token);

            auto user = db.find_user_by_id(*user_id);
            std::string session_token = db.create_session(*user_id, config.session_expiry_hours);

            json resp = {
                {"token", session_token},
                {"user", {
                    {"id", user->id},
                    {"username", user->username},
                    {"display_name", user->display_name},
                    {"role", user->role},
                    {"bio", user->bio},
                    {"status", user->status}
                }}
            };
            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        } catch (const std::exception& e) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    }

    void handle_request_access(uWS::HttpResponse<SSL>* res, const std::string& body) {
        try {
            auto j = json::parse(body);
            std::string username = j.at("username");
            std::string display_name = j.at("display_name");
            std::string public_key = j.at("public_key");

            auto id = db.create_join_request(username, display_name, public_key);

            json resp = {{"request_id", id}, {"status", "pending"},
                         {"message", "Your request has been submitted. An admin will review it."}};
            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        } catch (const std::exception& e) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    }
};
