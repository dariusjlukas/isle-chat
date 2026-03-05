#pragma once
#include <App.h>
#include <nlohmann/json.hpp>
#include <openssl/rand.h>
#include "db/database.h"
#include "auth/webauthn.h"
#include "config.h"

using json = nlohmann::json;

template <bool SSL>
struct AuthHandler {
    Database& db;
    const Config& config;

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

        app.post("/api/auth/logout", [this](auto* res, auto* req) {
            std::string token(req->getHeader("authorization"));
            if (token.rfind("Bearer ", 0) == 0) token = token.substr(7);
            db.delete_session(token);
            res->writeHeader("Content-Type", "application/json")
                ->end(R"({"ok":true})");
        });
    }

private:
    // Check if an auth method is enabled
    bool is_method_enabled(const std::string& method) {
        auto setting = db.get_setting("auth_methods");
        if (!setting) return true; // default: all methods enabled
        try {
            auto arr = json::parse(*setting);
            for (const auto& m : arr) {
                if (m.get<std::string>() == method) return true;
            }
        } catch (...) {}
        return false;
    }

    // Get session expiry hours from DB setting or config default
    int get_session_expiry() {
        auto setting = db.get_setting("session_expiry_hours");
        if (setting) {
            try { return std::stoi(*setting); } catch (...) {}
        }
        return config.session_expiry_hours;
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

        // Default: "invite" — require invite token
        if (!invite_token.empty()) {
            if (!db.validate_invite(invite_token)) {
                return "Invalid or expired invite token";
            }
            return "";
        }
        return "Invite token required. You can also request access below.";
    }

    // Complete user creation (shared between passkey and PKI registration)
    void complete_user_creation(User& user, const std::string& invite_token) {
        bool is_first_user_after_create = (user.role == "admin");

        if (!invite_token.empty()) {
            db.use_invite(invite_token, user.id);
        }

        if (is_first_user_after_create) {
            db.create_channel("general", "General discussion", false, user.id, {user.id},
                               true, "read");
        } else {
            auto general = db.find_general_channel();
            if (general) {
                db.add_channel_member(general->id, user.id, general->default_role);
            }
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
            {"status", user.status}
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
                {"timeout", 60000}
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
                {"timeout", 60000}
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
                {"timeout", 60000}
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

            } else {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Invalid auth_method. Use 'passkey' or 'pki'."})");
                return;
            }

            auto id = db.create_join_request(username, display_name, "", auth_method, credential_data);

            json resp = {{"request_id", id}, {"status", "pending"}};
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
