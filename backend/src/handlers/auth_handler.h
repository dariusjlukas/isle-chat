#pragma once
#include <App.h>
#include <nlohmann/json.hpp>
#include <openssl/rand.h>
#include "db/database.h"
#include "auth/webauthn.h"
#include "auth/password.h"
#include "auth/totp.h"
#include "config.h"
#include "ws/ws_handler.h"
#include "handlers/handler_utils.h"

using json = nlohmann::json;

template <bool SSL>
struct AuthHandler {
    Database& db;
    const Config& config;
    WsHandler<SSL>& ws;

    void register_routes(uWS::TemplatedApp<SSL>& app) {
        // WebAuthn (passkey) routes
        app.post("/api/auth/register/options", [this](auto* res, auto* req) {
            std::string body;
            res->onData([this, res, body = std::move(body)](std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                handle_register_options(res, body);
            });
            res->onAborted([]() {});
        });

        app.post("/api/auth/register/verify", [this](auto* res, auto* req) {
            std::string body;
            res->onData([this, res, body = std::move(body)](std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                handle_register_verify(res, body);
            });
            res->onAborted([]() {});
        });

        app.post("/api/auth/login/options", [this](auto* res, auto* req) {
            std::string body;
            res->onData([this, res, body = std::move(body)](std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                handle_login_options(res, body);
            });
            res->onAborted([]() {});
        });

        app.post("/api/auth/login/verify", [this](auto* res, auto* req) {
            std::string body;
            res->onData([this, res, body = std::move(body)](std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                handle_login_verify(res, body);
            });
            res->onAborted([]() {});
        });

        // PKI (browser key) routes
        app.post("/api/auth/pki/challenge", [this](auto* res, auto* req) {
            std::string body;
            res->onData([this, res, body = std::move(body)](std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                handle_pki_challenge(res, body);
            });
            res->onAborted([]() {});
        });

        app.post("/api/auth/pki/register", [this](auto* res, auto* req) {
            std::string body;
            res->onData([this, res, body = std::move(body)](std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                handle_pki_register(res, body);
            });
            res->onAborted([]() {});
        });

        app.post("/api/auth/pki/login", [this](auto* res, auto* req) {
            std::string body;
            res->onData([this, res, body = std::move(body)](std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                handle_pki_login(res, body);
            });
            res->onAborted([]() {});
        });

        // Recovery key login
        app.post("/api/auth/recovery", [this](auto* res, auto* req) {
            std::string body;
            res->onData([this, res, body = std::move(body)](std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                handle_recovery_login(res, body);
            });
            res->onAborted([]() {});
        });

        // Recovery token (admin-generated) login
        app.post("/api/auth/recover-account", [this](auto* res, auto* req) {
            std::string body;
            res->onData([this, res, body = std::move(body)](std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                handle_recovery_token_login(res, body);
            });
            res->onAborted([]() {});
        });

        // Join request routes
        app.post("/api/auth/request-access/options", [this](auto* res, auto* req) {
            std::string body;
            res->onData([this, res, body = std::move(body)](std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                handle_request_access_options(res, body);
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

        app.get("/api/auth/request-status/:id", [this](auto* res, auto* req) {
            std::string request_id(req->getParameter(0));
            handle_request_status(res, request_id);
        });

        // Password auth routes
        app.post("/api/auth/password/register", [this](auto* res, auto* req) {
            std::string body;
            res->onData([this, res, body = std::move(body)](std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                handle_password_register(res, body);
            });
            res->onAborted([]() {});
        });

        app.post("/api/auth/password/login", [this](auto* res, auto* req) {
            std::string body;
            res->onData([this, res, body = std::move(body)](std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                handle_password_login(res, body);
            });
            res->onAborted([]() {});
        });

        app.post("/api/auth/password/change", [this](auto* res, auto* req) {
            auto token = std::string(req->getHeader("authorization"));
            if (token.rfind("Bearer ", 0) == 0) token = token.substr(7);
            std::string body;
            res->onData([this, res, token, body = std::move(body)](std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                handle_password_change(res, body, token);
            });
            res->onAborted([]() {});
        });

        app.post("/api/auth/password/set", [this](auto* res, auto* req) {
            auto token = std::string(req->getHeader("authorization"));
            if (token.rfind("Bearer ", 0) == 0) token = token.substr(7);
            std::string body;
            res->onData([this, res, token, body = std::move(body)](std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                handle_password_set(res, body, token);
            });
            res->onAborted([]() {});
        });

        app.del("/api/auth/password", [this](auto* res, auto* req) {
            auto token = std::string(req->getHeader("authorization"));
            if (token.rfind("Bearer ", 0) == 0) token = token.substr(7);
            handle_password_delete(res, token);
        });

        app.post("/api/auth/mfa/verify", [this](auto* res, auto* req) {
            std::string body;
            res->onData([this, res, body = std::move(body)](std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                handle_mfa_verify(res, body);
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
    bool is_method_enabled(const std::string& method) {
        return is_auth_method_enabled(db, method);
    }

    int get_session_expiry() {
        return ::get_session_expiry(db, config);
    }

    bool is_mfa_required_for_method(const std::string& method) {
        auto val = db.get_setting("mfa_required_" + method);
        return val && *val == "true";
    }

    // Returns true if MFA is needed (response already sent), false if caller should proceed with session
    bool check_and_handle_mfa(uWS::HttpResponse<SSL>* res, const User& user,
                               const std::string& auth_method) {
        bool mfa_required = is_mfa_required_for_method(auth_method);
        bool user_has_totp = db.has_totp(user.id);

        if (!mfa_required && !user_has_totp) return false;

        if (mfa_required && !user_has_totp) {
            // User hasn't set up TOTP but admin requires it — let them in but flag it
            auto token = db.create_session(user.id, get_session_expiry());
            json resp = {{"token", token}, {"user", make_user_json(user)}, {"must_setup_totp", true}};
            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
            return true;
        }

        // User has TOTP — require MFA step
        auto mfa_token = db.create_mfa_pending_token(user.id, auth_method);
        json resp = {{"mfa_required", true}, {"mfa_token", mfa_token}};
        res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        return true;
    }

    // Check registration eligibility for direct registration (with invite token)
    // The approval-based flow is handled separately through join requests.
    // Returns empty string if allowed, error message if not
    std::string check_registration_eligibility(const std::string& username,
                                                const std::string& invite_token) {
        bool is_first_user = (db.count_users() == 0);
        if (is_first_user) return "";

        auto mode = db.get_setting("registration_mode");
        std::string reg_mode = mode.value_or("invite");

        if (reg_mode == "open") return "";

        if (reg_mode == "approval") {
            return "This server requires admin approval. Please request access instead.";
        }

        // "invite" or "invite_only" — require invite token
        if (!invite_token.empty()) {
            if (!db.validate_invite(invite_token)) {
                return "Invalid or expired invite token";
            }
            return "";
        }

        if (reg_mode == "invite_only") {
            return "This server requires an invite token to register.";
        }
        return "Invite token required. You can also request access below.";
    }

    // Complete user creation (shared between passkey and PKI registration)
    void complete_user_creation(User& user, const std::string& invite_token) {
        if (!invite_token.empty()) {
            db.use_invite(invite_token, user.id);
        }
    }

    std::string generate_user_handle() {
        unsigned char buf[16];
        RAND_bytes(buf, sizeof(buf));
        return webauthn::base64url_encode(buf, sizeof(buf));
    }

    json make_user_json(const User& user) {
        return {
            {"id", user.id},
            {"username", user.username},
            {"display_name", user.display_name},
            {"role", user.role},
            {"bio", user.bio},
            {"status", user.status},
            {"avatar_file_id", user.avatar_file_id},
            {"profile_color", user.profile_color}
        };
    }

    // --- WebAuthn (passkey) handlers ---

    void handle_register_options(uWS::HttpResponse<SSL>* res, const std::string& body) {
        try {
            if (!is_method_enabled("passkey")) {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Passkey authentication is not enabled on this server"})");
                return;
            }

            auto j = json::parse(body);
            std::string username = j.at("username");
            std::string display_name = j.at("display_name");
            std::string invite_token = j.value("token", "");

            auto err = check_registration_eligibility(username, invite_token);
            if (!err.empty()) {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->end(json({{"error", err}}).dump());
                return;
            }

            std::string challenge = webauthn::generate_challenge();
            std::string user_handle = generate_user_handle();

            json extra = {
                {"type", "registration"},
                {"username", username},
                {"display_name", display_name},
                {"user_handle", user_handle},
                {"invite_token", invite_token}
            };
            db.store_webauthn_challenge(challenge, extra.dump());

            json options = {
                {"rp", {
                    {"name", config.webauthn_rp_name},
                    {"id", config.webauthn_rp_id}
                }},
                {"user", {
                    {"id", user_handle},
                    {"name", username},
                    {"displayName", display_name}
                }},
                {"challenge", challenge},
                {"pubKeyCredParams", json::array({
                    {{"type", "public-key"}, {"alg", -7}},
                    {{"type", "public-key"}, {"alg", -257}}
                })},
                {"authenticatorSelection", {
                    {"residentKey", "required"},
                    {"userVerification", "preferred"}
                }},
                {"attestation", "none"},
                {"timeout", defaults::WEBAUTHN_TIMEOUT_MS}
            };

            res->writeHeader("Content-Type", "application/json")->end(options.dump());
        } catch (const std::exception& e) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    }

    void handle_register_verify(uWS::HttpResponse<SSL>* res, const std::string& body) {
        try {
            if (!is_method_enabled("passkey")) {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Passkey authentication is not enabled on this server"})");
                return;
            }

            auto j = json::parse(body);
            auto response = j.at("response");
            std::string attestation_object = response.at("attestationObject");
            std::string client_data_json = response.at("clientDataJSON");

            std::string transports_str = "[]";
            if (response.contains("transports")) {
                transports_str = response["transports"].dump();
            }

            auto cd_bytes = webauthn::base64url_decode(client_data_json);
            std::string cd_str(cd_bytes.begin(), cd_bytes.end());
            auto cd_json = json::parse(cd_str);
            std::string challenge = cd_json.at("challenge");

            auto stored = db.get_webauthn_challenge(challenge);
            if (!stored) {
                res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Invalid or expired challenge"})");
                return;
            }

            auto extra = json::parse(stored->extra_data);
            if (extra.at("type") != "registration") {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Challenge is not for registration"})");
                return;
            }

            auto result = webauthn::verify_registration(
                attestation_object, client_data_json, challenge,
                config.webauthn_origin, config.webauthn_rp_id);

            db.delete_webauthn_challenge(challenge);

            std::string username = extra.at("username");
            std::string display_name = extra.at("display_name");
            std::string invite_token = extra.value("invite_token", "");
            bool is_first_user = (db.count_users() == 0);
            std::string role = is_first_user ? "admin" : "user";

            auto user = db.create_user(username, display_name, "", role);
            db.store_webauthn_credential(user.id, result->credential_id,
                                          result->public_key, result->sign_count,
                                          "Passkey", transports_str);

            complete_user_creation(user, invite_token);

            std::string token = db.create_session(user.id, get_session_expiry());
            json resp = {{"token", token}, {"user", make_user_json(user)}};
            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        } catch (const pqxx::unique_violation&) {
            res->writeStatus("409")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Username already taken"})");
        } catch (const std::exception& e) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    }

    void handle_login_options(uWS::HttpResponse<SSL>* res, const std::string& body) {
        try {
            if (!is_method_enabled("passkey")) {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Passkey authentication is not enabled on this server"})");
                return;
            }

            std::string challenge = webauthn::generate_challenge();
            json extra = {{"type", "authentication"}};
            db.store_webauthn_challenge(challenge, extra.dump());

            json options = {
                {"challenge", challenge},
                {"rpId", config.webauthn_rp_id},
                {"allowCredentials", json::array()},
                {"userVerification", "preferred"},
                {"timeout", defaults::WEBAUTHN_TIMEOUT_MS}
            };

            res->writeHeader("Content-Type", "application/json")->end(options.dump());
        } catch (const std::exception& e) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    }

    void handle_login_verify(uWS::HttpResponse<SSL>* res, const std::string& body) {
        try {
            if (!is_method_enabled("passkey")) {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Passkey authentication is not enabled on this server"})");
                return;
            }

            auto j = json::parse(body);
            std::string credential_id = j.at("id");
            auto response = j.at("response");
            std::string auth_data = response.at("authenticatorData");
            std::string client_data_json = response.at("clientDataJSON");
            std::string signature = response.at("signature");

            auto cred = db.find_webauthn_credential(credential_id);
            if (!cred) {
                res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Unknown credential"})");
                return;
            }

            auto cd_bytes = webauthn::base64url_decode(client_data_json);
            std::string cd_str(cd_bytes.begin(), cd_bytes.end());
            auto cd_json = json::parse(cd_str);
            std::string challenge = cd_json.at("challenge");

            auto stored = db.get_webauthn_challenge(challenge);
            if (!stored) {
                res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Invalid or expired challenge"})");
                return;
            }

            auto extra = json::parse(stored->extra_data);
            if (extra.at("type") != "authentication") {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Challenge is not for authentication"})");
                return;
            }

            auto new_sign_count = webauthn::verify_authentication(
                auth_data, client_data_json, signature,
                cred->public_key, cred->sign_count,
                challenge, config.webauthn_origin, config.webauthn_rp_id);

            if (!new_sign_count) {
                res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"WebAuthn verification failed"})");
                return;
            }

            db.delete_webauthn_challenge(challenge);
            db.update_webauthn_sign_count(credential_id, *new_sign_count);

            auto user = db.find_user_by_credential_id(credential_id);
            if (!user) {
                res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"User not found"})");
                return;
            }

            if (check_and_handle_mfa(res, *user, "passkey")) return;

            std::string token = db.create_session(user->id, get_session_expiry());
            json resp = {{"token", token}, {"user", make_user_json(*user)}};
            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        } catch (const std::exception& e) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    }

    // --- PKI (browser key) handlers ---

    void handle_pki_challenge(uWS::HttpResponse<SSL>* res, const std::string& body) {
        try {
            if (!is_method_enabled("pki")) {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Browser key authentication is not enabled on this server"})");
                return;
            }

            auto j = json::parse(body);
            std::string public_key = j.value("public_key", "");

            std::string challenge = webauthn::generate_challenge();
            std::string type = public_key.empty() ? "pki_registration" : "pki_login";

            json extra = {{"type", type}};
            if (!public_key.empty()) extra["public_key"] = public_key;
            db.store_webauthn_challenge(challenge, extra.dump());

            json resp = {{"challenge", challenge}};
            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        } catch (const std::exception& e) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    }

    void handle_pki_register(uWS::HttpResponse<SSL>* res, const std::string& body) {
        try {
            if (!is_method_enabled("pki")) {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Browser key authentication is not enabled on this server"})");
                return;
            }

            auto j = json::parse(body);
            std::string username = j.at("username");
            std::string display_name = j.at("display_name");
            std::string invite_token = j.value("token", "");
            std::string public_key = j.at("public_key");
            std::string challenge = j.at("challenge");
            std::string signature = j.at("signature");

            // Verify challenge
            auto stored = db.get_webauthn_challenge(challenge);
            if (!stored) {
                res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Invalid or expired challenge"})");
                return;
            }
            auto extra = json::parse(stored->extra_data);
            if (extra.at("type") != "pki_registration") {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Challenge is not for PKI registration"})");
                return;
            }

            // Verify signature
            if (!webauthn::verify_pki_signature(public_key, challenge, signature)) {
                res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Signature verification failed"})");
                return;
            }

            db.delete_webauthn_challenge(challenge);

            // Check registration eligibility
            auto err = check_registration_eligibility(username, invite_token);
            if (!err.empty()) {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->end(json({{"error", err}}).dump());
                return;
            }

            bool is_first_user = (db.count_users() == 0);
            std::string role = is_first_user ? "admin" : "user";

            auto user = db.create_user(username, display_name, "", role);
            db.store_pki_credential(user.id, public_key);

            // Generate recovery keys
            auto [plaintext_keys, key_hashes] = webauthn::generate_recovery_keys();
            db.store_recovery_keys(user.id, key_hashes);

            complete_user_creation(user, invite_token);

            std::string token = db.create_session(user.id, get_session_expiry());
            json resp = {
                {"token", token},
                {"user", make_user_json(user)},
                {"recovery_keys", plaintext_keys}
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

    void handle_pki_login(uWS::HttpResponse<SSL>* res, const std::string& body) {
        try {
            if (!is_method_enabled("pki")) {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Browser key authentication is not enabled on this server"})");
                return;
            }

            auto j = json::parse(body);
            std::string public_key = j.at("public_key");
            std::string challenge = j.at("challenge");
            std::string signature = j.at("signature");

            // Verify challenge
            auto stored = db.get_webauthn_challenge(challenge);
            if (!stored) {
                res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Invalid or expired challenge"})");
                return;
            }
            auto extra = json::parse(stored->extra_data);
            if (extra.at("type") != "pki_login") {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Challenge is not for PKI login"})");
                return;
            }

            // Verify signature
            if (!webauthn::verify_pki_signature(public_key, challenge, signature)) {
                res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Signature verification failed"})");
                return;
            }

            db.delete_webauthn_challenge(challenge);

            // Find user
            auto user = db.find_user_by_pki_key(public_key);
            if (!user) {
                res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"No account found for this browser key"})");
                return;
            }

            if (check_and_handle_mfa(res, *user, "pki")) return;

            std::string token = db.create_session(user->id, get_session_expiry());
            json resp = {{"token", token}, {"user", make_user_json(*user)}};
            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        } catch (const std::exception& e) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    }

    // --- Recovery key handler ---

    void handle_recovery_login(uWS::HttpResponse<SSL>* res, const std::string& body) {
        try {
            auto j = json::parse(body);
            std::string recovery_key = j.at("recovery_key");

            std::string key_hash = webauthn::hash_recovery_key(recovery_key);
            auto user_id = db.verify_and_consume_recovery_key(key_hash);
            if (!user_id) {
                res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Invalid or already used recovery key"})");
                return;
            }

            auto user = db.find_user_by_id(*user_id);
            if (!user) {
                res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"User not found"})");
                return;
            }

            std::string token = db.create_session(user->id, get_session_expiry());
            json resp = {
                {"token", token},
                {"user", make_user_json(*user)},
                {"must_setup_key", true}
            };
            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        } catch (const std::exception& e) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    }

    // --- Recovery token handler ---

    void handle_recovery_token_login(uWS::HttpResponse<SSL>* res, const std::string& body) {
        try {
            auto j = json::parse(body);
            std::string token = j.at("token");

            auto user_id = db.get_recovery_token_user_id(token);
            if (!user_id) {
                res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Invalid or expired recovery token"})");
                return;
            }

            auto user = db.find_user_by_id(*user_id);
            if (!user) {
                res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"User not found"})");
                return;
            }

            db.use_recovery_token(token);

            std::string session = db.create_session(user->id, get_session_expiry());
            json resp = {
                {"token", session},
                {"user", make_user_json(*user)},
                {"must_setup_key", true}
            };
            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        } catch (const std::exception& e) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    }

    // --- Join request handlers ---

    // Generate WebAuthn options for a join request (passkey method)
    void handle_request_access_options(uWS::HttpResponse<SSL>* res, const std::string& body) {
        try {
            auto mode = db.get_setting("registration_mode");
            std::string reg_mode = mode.value_or("invite");
            if (reg_mode == "open" || reg_mode == "invite_only") {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Requesting access is not available on this server"})");
                return;
            }

            if (!is_method_enabled("passkey")) {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Passkey authentication is not enabled on this server"})");
                return;
            }

            auto j = json::parse(body);
            std::string username = j.at("username");
            std::string display_name = j.at("display_name");

            std::string challenge = webauthn::generate_challenge();
            std::string user_handle = generate_user_handle();

            json extra = {
                {"type", "join_request"},
                {"username", username},
                {"display_name", display_name},
                {"user_handle", user_handle}
            };
            db.store_webauthn_challenge(challenge, extra.dump());

            json options = {
                {"rp", {
                    {"name", config.webauthn_rp_name},
                    {"id", config.webauthn_rp_id}
                }},
                {"user", {
                    {"id", user_handle},
                    {"name", username},
                    {"displayName", display_name}
                }},
                {"challenge", challenge},
                {"pubKeyCredParams", json::array({
                    {{"type", "public-key"}, {"alg", -7}},
                    {{"type", "public-key"}, {"alg", -257}}
                })},
                {"authenticatorSelection", {
                    {"residentKey", "required"},
                    {"userVerification", "preferred"}
                }},
                {"attestation", "none"},
                {"timeout", defaults::WEBAUTHN_TIMEOUT_MS}
            };

            res->writeHeader("Content-Type", "application/json")->end(options.dump());
        } catch (const std::exception& e) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    }

    // Submit a join request with pre-generated credentials
    void handle_request_access(uWS::HttpResponse<SSL>* res, const std::string& body) {
        try {
            auto mode = db.get_setting("registration_mode");
            std::string reg_mode = mode.value_or("invite");
            if (reg_mode == "open" || reg_mode == "invite_only") {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Requesting access is not available on this server"})");
                return;
            }

            auto j = json::parse(body);
            std::string username = j.at("username");
            std::string display_name = j.at("display_name");
            std::string auth_method = j.at("auth_method");

            std::string credential_data;

            if (auth_method == "pki") {
                if (!is_method_enabled("pki")) {
                    res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Browser key authentication is not enabled"})");
                    return;
                }

                std::string public_key = j.at("public_key");
                std::string challenge = j.at("challenge");
                std::string signature = j.at("signature");

                // Verify the challenge
                auto stored = db.get_webauthn_challenge(challenge);
                if (!stored) {
                    res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Invalid or expired challenge"})");
                    return;
                }
                db.delete_webauthn_challenge(challenge);

                // Verify signature proves key ownership
                if (!webauthn::verify_pki_signature(public_key, challenge, signature)) {
                    res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Signature verification failed"})");
                    return;
                }

                credential_data = json({{"public_key", public_key}}).dump();

            } else if (auth_method == "passkey") {
                if (!is_method_enabled("passkey")) {
                    res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Passkey authentication is not enabled"})");
                    return;
                }

                auto credential = j.at("credential");
                auto response = credential.at("response");
                std::string attestation_object = response.at("attestationObject");
                std::string client_data_json = response.at("clientDataJSON");

                std::string transports_str = "[]";
                if (response.contains("transports")) {
                    transports_str = response["transports"].dump();
                }

                // Extract and verify challenge
                auto cd_bytes = webauthn::base64url_decode(client_data_json);
                std::string cd_str(cd_bytes.begin(), cd_bytes.end());
                auto cd_json = json::parse(cd_str);
                std::string challenge = cd_json.at("challenge");

                auto stored = db.get_webauthn_challenge(challenge);
                if (!stored) {
                    res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Invalid or expired challenge"})");
                    return;
                }

                auto extra = json::parse(stored->extra_data);
                if (extra.at("type") != "join_request") {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Challenge is not for a join request"})");
                    return;
                }

                // Verify the attestation
                auto result = webauthn::verify_registration(
                    attestation_object, client_data_json, challenge,
                    config.webauthn_origin, config.webauthn_rp_id);

                if (!result) {
                    res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"WebAuthn verification failed"})");
                    return;
                }

                db.delete_webauthn_challenge(challenge);

                // Store verified credential data for later account creation
                credential_data = json({
                    {"credential_id", result->credential_id},
                    {"public_key", webauthn::base64url_encode(result->public_key)},
                    {"sign_count", result->sign_count},
                    {"transports", transports_str}
                }).dump();

            } else if (auth_method == "password") {
                if (!is_method_enabled("password")) {
                    res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Password authentication is not enabled"})");
                    return;
                }

                std::string password = j.at("password");

                // Validate password complexity
                auto policy = get_password_policy();
                auto validation_error = password_auth::validate_password(password, policy);
                if (!validation_error.empty()) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->end(json({{"error", validation_error}}).dump());
                    return;
                }

                // Hash password and store in credential_data
                auto hash = password_auth::hash_password(password);
                credential_data = json({{"password_hash", hash}}).dump();

            } else {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Invalid auth_method. Use 'passkey', 'pki', or 'password'."})");
                return;
            }

            auto id = db.create_join_request(username, display_name, "", auth_method, credential_data);

            // Notify admin/owner users via WebSocket
            json notify = {{"type", "join_request_created"}, {"request_id", id}};
            auto all_users = db.list_users();
            for (const auto& u : all_users) {
                if (u.role == "admin" || u.role == "owner") {
                    ws.send_to_user(u.id, notify.dump());
                }
            }

            json resp = {{"request_id", id}, {"status", "pending"}};
            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        } catch (const std::exception& e) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    }

    // --- Password auth handlers ---

    password_auth::PasswordPolicy get_password_policy() {
        password_auth::PasswordPolicy policy;
        auto v = db.get_setting("password_min_length");
        if (v) { try { policy.min_length = std::stoi(*v); } catch (...) {} }
        v = db.get_setting("password_require_uppercase");
        if (v) policy.require_uppercase = (*v == "true");
        v = db.get_setting("password_require_lowercase");
        if (v) policy.require_lowercase = (*v == "true");
        v = db.get_setting("password_require_number");
        if (v) policy.require_number = (*v == "true");
        v = db.get_setting("password_require_special");
        if (v) policy.require_special = (*v == "true");
        v = db.get_setting("password_max_age_days");
        if (v) { try { policy.max_age_days = std::stoi(*v); } catch (...) {} }
        v = db.get_setting("password_history_count");
        if (v) { try { policy.history_count = std::stoi(*v); } catch (...) {} }
        return policy;
    }

    void handle_password_register(uWS::HttpResponse<SSL>* res, const std::string& body) {
        try {
            if (!is_method_enabled("password")) {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Password authentication is not enabled on this server"})");
                return;
            }

            auto j = json::parse(body);
            std::string username = j.at("username");
            std::string display_name = j.at("display_name");
            std::string password = j.at("password");
            std::string invite_token = j.value("token", "");

            // Validate password complexity
            auto policy = get_password_policy();
            auto validation_error = password_auth::validate_password(password, policy);
            if (!validation_error.empty()) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(json({{"error", validation_error}}).dump());
                return;
            }

            // Check registration eligibility
            auto eligibility_error = check_registration_eligibility(username, invite_token);
            if (!eligibility_error.empty()) {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->end(json({{"error", eligibility_error}}).dump());
                return;
            }

            // Create user
            bool is_first_user = (db.count_users() == 0);
            std::string role = is_first_user ? "admin" : "user";
            auto user = db.create_user(username, display_name, "", role);

            // Hash and store password
            auto hash = password_auth::hash_password(password);
            db.store_password(user.id, hash);

            // Use invite token if provided
            complete_user_creation(user, invite_token);

            // Create session
            auto token = db.create_session(user.id, get_session_expiry());

            json resp = {{"token", token}, {"user", make_user_json(user)}};
            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        } catch (const pqxx::unique_violation&) {
            res->writeStatus("409")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Username already taken"})");
        } catch (const std::exception& e) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    }

    void handle_password_login(uWS::HttpResponse<SSL>* res, const std::string& body) {
        try {
            if (!is_method_enabled("password")) {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Password authentication is not enabled on this server"})");
                return;
            }

            auto j = json::parse(body);
            std::string username = j.at("username");
            std::string password = j.at("password");

            // Find user by username
            auto user = db.find_user_by_username(username);
            if (!user) {
                res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Invalid username or password"})");
                return;
            }

            // Get stored password hash
            auto stored_hash = db.get_password_hash(user->id);
            if (!stored_hash) {
                res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Invalid username or password"})");
                return;
            }

            // Verify password
            if (!password_auth::verify_password(password, *stored_hash)) {
                res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Invalid username or password"})");
                return;
            }

            if (check_and_handle_mfa(res, *user, "password")) return;

            // Create session
            auto token = db.create_session(user->id, get_session_expiry());

            // Check password expiry
            auto policy = get_password_policy();
            json resp = {{"token", token}, {"user", make_user_json(*user)}};
            if (policy.max_age_days > 0) {
                resp["must_change_password"] = db.is_password_expired(user->id, policy.max_age_days);
            }
            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        } catch (const std::exception& e) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    }

    void handle_password_change(uWS::HttpResponse<SSL>* res, const std::string& body,
                                 const std::string& session_token) {
        try {
            auto user_id = db.validate_session(session_token);
            if (!user_id) {
                res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Unauthorized"})");
                return;
            }

            auto j = json::parse(body);
            std::string current_password = j.at("current_password");
            std::string new_password = j.at("new_password");

            // Verify current password
            auto stored_hash = db.get_password_hash(*user_id);
            if (!stored_hash || !password_auth::verify_password(current_password, *stored_hash)) {
                res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Current password is incorrect"})");
                return;
            }

            // Validate new password complexity
            auto policy = get_password_policy();
            auto validation_error = password_auth::validate_password(new_password, policy);
            if (!validation_error.empty()) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(json({{"error", validation_error}}).dump());
                return;
            }

            // Check password history
            if (policy.history_count > 0) {
                auto history = db.get_password_history(*user_id, policy.history_count);
                // Also include the current password in history check
                history.push_back(*stored_hash);
                if (password_auth::matches_history(new_password, history)) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->end(json({{"error", "Cannot reuse a recent password"}}).dump());
                    return;
                }
            }

            // Save old password to history
            db.add_password_history(*user_id, *stored_hash);

            // Hash and store new password
            auto new_hash = password_auth::hash_password(new_password);
            db.store_password(*user_id, new_hash);

            res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
        } catch (const std::exception& e) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    }

    void handle_password_set(uWS::HttpResponse<SSL>* res, const std::string& body,
                              const std::string& session_token) {
        try {
            auto user_id = db.validate_session(session_token);
            if (!user_id) {
                res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Unauthorized"})");
                return;
            }

            // Check that password auth is enabled
            auto methods_str = db.get_setting("auth_methods").value_or("");
            if (methods_str.find("password") == std::string::npos) {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Password authentication is not enabled"})");
                return;
            }

            // User must NOT already have a password
            if (db.has_password(*user_id)) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Password already set. Use the change password endpoint instead."})");
                return;
            }

            auto j = json::parse(body);
            std::string new_password = j.at("password");

            // Validate password complexity
            auto policy = get_password_policy();
            auto validation_error = password_auth::validate_password(new_password, policy);
            if (!validation_error.empty()) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(json({{"error", validation_error}}).dump());
                return;
            }

            // Hash and store
            auto hash = password_auth::hash_password(new_password);
            db.store_password(*user_id, hash);

            res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
        } catch (const std::exception& e) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    }

    void handle_password_delete(uWS::HttpResponse<SSL>* res, const std::string& session_token) {
        try {
            auto user_id = db.validate_session(session_token);
            if (!user_id) {
                res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Unauthorized"})");
                return;
            }

            if (!db.has_password(*user_id)) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"No password is set"})");
                return;
            }

            // Ensure the user has at least one other auth credential
            int other_creds = db.count_user_credentials(*user_id);
            if (other_creds < 1) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Cannot remove password — no other login method configured. Add a passkey or browser key first."})");
                return;
            }

            db.delete_password(*user_id);
            res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
        } catch (const std::exception& e) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    }

    void handle_mfa_verify(uWS::HttpResponse<SSL>* res, const std::string& body) {
        try {
            auto j = json::parse(body);
            std::string mfa_token = j.at("mfa_token");
            std::string totp_code = j.at("totp_code");

            auto pending = db.validate_mfa_pending_token(mfa_token);
            if (!pending) {
                res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Invalid or expired MFA token"})");
                return;
            }

            auto [user_id, auth_method] = *pending;

            auto secret = db.get_totp_secret(user_id);
            if (!secret) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"TOTP is not set up"})");
                return;
            }

            if (!totp::verify_code(*secret, totp_code)) {
                res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Invalid verification code"})");
                return;
            }

            // MFA verified — consume the token and create a real session
            db.delete_mfa_pending_token(mfa_token);

            auto user = db.find_user_by_id(user_id);
            if (!user) {
                res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"User not found"})");
                return;
            }

            auto token = db.create_session(user->id, get_session_expiry());
            json resp = {{"token", token}, {"user", make_user_json(*user)}};

            // For password login, also check expiry
            if (auth_method == "password") {
                auto policy = get_password_policy();
                if (policy.max_age_days > 0) {
                    resp["must_change_password"] = db.is_password_expired(user->id, policy.max_age_days);
                }
            }

            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        } catch (const std::exception& e) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    }

    // Poll for join request status
    void handle_request_status(uWS::HttpResponse<SSL>* res, const std::string& request_id) {
        try {
            auto request = db.get_join_request(request_id);
            if (!request) {
                res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Request not found"})");
                return;
            }

            json resp = {{"status", request->status}};

            if (request->status == "approved" && !request->session_token.empty()) {
                // Validate the session is still active
                auto user_id = db.validate_session(request->session_token);
                if (user_id) {
                    auto user = db.find_user_by_id(*user_id);
                    if (user) {
                        resp["token"] = request->session_token;
                        resp["user"] = make_user_json(*user);
                    }
                }
            }

            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        } catch (const std::exception& e) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    }
};
