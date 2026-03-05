#pragma once
#include <App.h>
#include <nlohmann/json.hpp>
#include "db/database.h"
#include "ws/ws_handler.h"
#include "auth/webauthn.h"
#include "config.h"

using json = nlohmann::json;

template <bool SSL>
struct UserHandler {
    Database& db;
    WsHandler<SSL>& ws;
    const Config& config;

    void register_routes(uWS::TemplatedApp<SSL>& app) {
        app.get("/api/users", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            auto users = db.list_users();
            json arr = json::array();
            for (const auto& u : users) {
                arr.push_back({{"id", u.id}, {"username", u.username},
                               {"display_name", u.display_name}, {"role", u.role},
                               {"is_online", u.is_online}, {"last_seen", u.last_seen},
                               {"bio", u.bio}, {"status", u.status}});
            }
            res->writeHeader("Content-Type", "application/json")->end(arr.dump());
        });

        app.get("/api/users/me", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            auto user = db.find_user_by_id(user_id);
            if (!user) {
                res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"User not found"})");
                return;
            }

            json resp = {{"id", user->id}, {"username", user->username},
                         {"display_name", user->display_name}, {"role", user->role},
                         {"is_online", user->is_online}, {"last_seen", user->last_seen},
                         {"bio", user->bio}, {"status", user->status}};
            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        });

        app.put("/api/users/me", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            res->onData([this, res, user_id, body = std::string()](std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;

                try {
                    auto j = json::parse(body);
                    auto current = db.find_user_by_id(user_id);
                    if (!current) {
                        res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                            ->end(R"({"error":"User not found"})");
                        return;
                    }

                    std::string display_name = j.value("display_name", current->display_name);
                    std::string bio = j.value("bio", current->bio);
                    std::string status = j.value("status", current->status);

                    auto updated = db.update_user_profile(user_id, display_name, bio, status);

                    json user_json = {{"id", updated.id}, {"username", updated.username},
                                 {"display_name", updated.display_name}, {"role", updated.role},
                                 {"is_online", updated.is_online}, {"last_seen", updated.last_seen},
                                 {"bio", updated.bio}, {"status", updated.status}};
                    res->writeHeader("Content-Type", "application/json")->end(user_json.dump());

                    // Broadcast profile change to all connected users
                    json broadcast = {{"type", "user_updated"}, {"user", user_json}};
                    ws.broadcast_to_presence(broadcast.dump());
                } catch (const std::exception& e) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->end(json({{"error", e.what()}}).dump());
                }
            });
            res->onAborted([]() {});
        });

        app.del("/api/users/me", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            try {
                db.delete_user(user_id);
                res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
            } catch (const std::exception& e) {
                res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                    ->end(json({{"error", e.what()}}).dump());
            }
        });

        // --- Passkey management ---

        app.get("/api/users/me/passkeys", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            auto creds = db.list_webauthn_credentials(user_id);
            json arr = json::array();
            for (const auto& c : creds) {
                arr.push_back({{"id", c.credential_id}, {"device_name", c.device_name},
                               {"created_at", c.created_at}});
            }
            res->writeHeader("Content-Type", "application/json")->end(arr.dump());
        });

        app.post("/api/users/me/passkeys/options", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            try {
                auto user = db.find_user_by_id(user_id);
                if (!user) {
                    res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"User not found"})");
                    return;
                }

                std::string challenge = webauthn::generate_challenge();

                // Build excludeCredentials from existing passkeys
                auto existing = db.list_webauthn_credentials(user_id);
                json exclude = json::array();
                for (const auto& c : existing) {
                    json cred_desc = {{"type", "public-key"}, {"id", c.credential_id}};
                    if (!c.transports.empty() && c.transports != "[]") {
                        cred_desc["transports"] = json::parse(c.transports);
                    }
                    exclude.push_back(cred_desc);
                }

                auto uid_bytes = std::vector<unsigned char>(user_id.begin(), user_id.end());
                std::string user_handle = webauthn::base64url_encode(uid_bytes);

                json extra = {{"type", "add_passkey"}, {"user_id", user_id}};
                db.store_webauthn_challenge(challenge, extra.dump());

                json options = {
                    {"rp", {
                        {"name", config.webauthn_rp_name},
                        {"id", config.webauthn_rp_id}
                    }},
                    {"user", {
                        {"id", user_handle},
                        {"name", user->username},
                        {"displayName", user->display_name}
                    }},
                    {"challenge", challenge},
                    {"pubKeyCredParams", json::array({
                        {{"type", "public-key"}, {"alg", -7}},   // ES256
                        {{"type", "public-key"}, {"alg", -257}}  // RS256
                    })},
                    {"excludeCredentials", exclude},
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
        });

        app.post("/api/users/me/passkeys/verify", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            res->onData([this, res, user_id, body = std::string()](std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;

                try {
                    auto j = json::parse(body);
                    std::string credential_id = j.at("id");
                    auto response = j.at("response");
                    std::string attestation_object = response.at("attestationObject");
                    std::string client_data_json = response.at("clientDataJSON");

                    std::string transports_str = "[]";
                    if (response.contains("transports")) {
                        transports_str = response["transports"].dump();
                    }

                    std::string device_name = j.value("device_name", "Passkey");

                    // Extract challenge from clientDataJSON
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
                    if (extra.at("type") != "add_passkey" || extra.at("user_id") != user_id) {
                        res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                            ->end(R"({"error":"Challenge mismatch"})");
                        return;
                    }

                    auto result = webauthn::verify_registration(
                        attestation_object, client_data_json, challenge,
                        config.webauthn_origin, config.webauthn_rp_id);

                    if (!result) {
                        res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                            ->end(R"({"error":"WebAuthn verification failed"})");
                        return;
                    }

                    db.delete_webauthn_challenge(challenge);
                    db.store_webauthn_credential(user_id, result->credential_id,
                                                  result->public_key, result->sign_count,
                                                  device_name, transports_str);

                    res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
                } catch (const std::exception& e) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->end(json({{"error", e.what()}}).dump());
                }
            });
            res->onAborted([]() {});
        });

        app.del("/api/users/me/passkeys/:id", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            std::string credential_id(req->getParameter(0));
            try {
                int total = db.count_user_credentials(user_id);
                if (total <= 1) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Cannot remove your only credential"})");
                    return;
                }
                db.remove_webauthn_credential(credential_id, user_id);
                res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
            } catch (const std::exception& e) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(json({{"error", e.what()}}).dump());
            }
        });

        // --- PKI key management ---

        app.post("/api/users/me/keys/challenge", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            try {
                std::string challenge = webauthn::generate_challenge();
                json extra = {{"type", "pki_add"}, {"user_id", user_id}};
                db.store_webauthn_challenge(challenge, extra.dump());
                json resp = {{"challenge", challenge}};
                res->writeHeader("Content-Type", "application/json")->end(resp.dump());
            } catch (const std::exception& e) {
                res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                    ->end(json({{"error", e.what()}}).dump());
            }
        });

        app.post("/api/users/me/keys", [this](auto* res, auto* req) {
            std::string user_id_copy = get_user_id(res, req);
            std::string body;
            res->onData([this, res, user_id = std::move(user_id_copy), body = std::move(body)](
                std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                if (user_id.empty()) return;

                try {
                    auto j = json::parse(body);
                    std::string public_key = j.at("public_key");
                    std::string challenge = j.at("challenge");
                    std::string signature = j.at("signature");
                    std::string device_name = j.value("device_name", "Browser Key");

                    auto stored = db.get_webauthn_challenge(challenge);
                    if (!stored) {
                        res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                            ->end(R"({"error":"Invalid or expired challenge"})");
                        return;
                    }

                    auto extra = json::parse(stored->extra_data);
                    if (extra.at("type") != "pki_add" || extra.at("user_id") != user_id) {
                        res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                            ->end(R"({"error":"Challenge mismatch"})");
                        return;
                    }

                    if (!webauthn::verify_pki_signature(public_key, challenge, signature)) {
                        res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                            ->end(R"({"error":"Signature verification failed"})");
                        return;
                    }

                    db.delete_webauthn_challenge(challenge);
                    db.store_pki_credential(user_id, public_key, device_name);

                    // If this is user's first PKI key, generate recovery keys
                    auto pki_keys = db.list_pki_credentials(user_id);
                    int remaining = db.count_remaining_recovery_keys(user_id);
                    json resp = {{"ok", true}};

                    if (pki_keys.size() == 1 && remaining == 0) {
                        auto [plaintext, hashes] = webauthn::generate_recovery_keys();
                        db.store_recovery_keys(user_id, hashes);
                        resp["recovery_keys"] = plaintext;
                    }

                    res->writeHeader("Content-Type", "application/json")->end(resp.dump());
                } catch (const std::exception& e) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->end(json({{"error", e.what()}}).dump());
                }
            });
            res->onAborted([]() {});
        });

        app.get("/api/users/me/keys", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            auto creds = db.list_pki_credentials(user_id);
            json arr = json::array();
            for (const auto& c : creds) {
                arr.push_back({{"id", c.id}, {"device_name", c.device_name},
                               {"created_at", c.created_at}});
            }
            res->writeHeader("Content-Type", "application/json")->end(arr.dump());
        });

        app.del("/api/users/me/keys/:id", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            std::string key_id(req->getParameter(0));
            try {
                int total = db.count_user_credentials(user_id);
                if (total <= 1) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Cannot remove your only credential"})");
                    return;
                }
                db.remove_pki_credential(key_id, user_id);
                res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
            } catch (const std::exception& e) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(json({{"error", e.what()}}).dump());
            }
        });

        // --- Recovery key management ---

        app.get("/api/users/me/recovery-keys/count", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            int remaining = db.count_remaining_recovery_keys(user_id);
            json resp = {{"remaining", remaining}};
            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        });

        app.post("/api/users/me/recovery-keys/regenerate", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            try {
                db.delete_recovery_keys(user_id);
                auto [plaintext, hashes] = webauthn::generate_recovery_keys();
                db.store_recovery_keys(user_id, hashes);
                json resp = {{"recovery_keys", plaintext}};
                res->writeHeader("Content-Type", "application/json")->end(resp.dump());
            } catch (const std::exception& e) {
                res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                    ->end(json({{"error", e.what()}}).dump());
            }
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
};
