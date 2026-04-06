#include "handlers/user_handler.h"
#include "handlers/format_utils.h"

using json = nlohmann::json;

template <bool SSL>
void UserHandler<SSL>::register_routes(uWS::TemplatedApp<SSL>& app) {
  app.get("/api/users", [this](auto* res, auto* req) {
    std::string user_id = get_user_id(res, req);
    if (user_id.empty()) return;

    auto users = db.list_users();
    json arr = json::array();
    for (const auto& u : users) {
      arr.push_back(
        {{"id", u.id},
         {"username", u.username},
         {"display_name", u.display_name},
         {"role", u.role},
         {"is_online", u.is_online},
         {"last_seen", u.last_seen},
         {"bio", u.bio},
         {"status", u.status},
         {"avatar_file_id", u.avatar_file_id},
         {"profile_color", u.profile_color}});
    }
    res->writeHeader("Content-Type", "application/json")->end(arr.dump());
  });

  app.get("/api/users/me", [this](auto* res, auto* req) {
    std::string user_id = get_user_id(res, req);
    if (user_id.empty()) return;

    auto user = db.find_user_by_id(user_id);
    if (!user) {
      res->writeStatus("404")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"User not found"})");
      return;
    }

    json resp = {
      {"id", user->id},
      {"username", user->username},
      {"display_name", user->display_name},
      {"role", user->role},
      {"is_online", user->is_online},
      {"last_seen", user->last_seen},
      {"bio", user->bio},
      {"status", user->status},
      {"avatar_file_id", user->avatar_file_id},
      {"profile_color", user->profile_color},
      {"has_password", db.has_password(user->id)},
      {"has_totp", db.has_totp(user->id)}};
    res->writeHeader("Content-Type", "application/json")->end(resp.dump());
  });

  app.put("/api/users/me", [this](auto* res, auto* req) {
    std::string user_id = get_user_id(res, req);
    if (user_id.empty()) return;

    res->onData(
      [this, res, user_id, body = std::string()](std::string_view data, bool last) mutable {
        body.append(data);
        if (!last) return;

        try {
          auto j = json::parse(body);
          auto current = db.find_user_by_id(user_id);
          if (!current) {
            res->writeStatus("404")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"User not found"})");
            return;
          }

          std::string display_name = j.value("display_name", current->display_name);
          std::string bio = j.value("bio", current->bio);
          std::string status = j.value("status", current->status);
          std::string profile_color = j.value("profile_color", current->profile_color);

          auto updated = db.update_user_profile(user_id, display_name, bio, status, profile_color);

          json user_json = {
            {"id", updated.id},
            {"username", updated.username},
            {"display_name", updated.display_name},
            {"role", updated.role},
            {"is_online", updated.is_online},
            {"last_seen", updated.last_seen},
            {"bio", updated.bio},
            {"status", updated.status},
            {"avatar_file_id", updated.avatar_file_id},
            {"profile_color", updated.profile_color}};
          res->writeHeader("Content-Type", "application/json")->end(user_json.dump());

          // Broadcast profile change to all connected users
          json broadcast = {{"type", "user_updated"}, {"user", user_json}};
          ws.broadcast_to_presence(broadcast.dump());
        } catch (const std::exception& e) {
          res->writeStatus("400")
            ->writeHeader("Content-Type", "application/json")
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
      res->writeStatus("500")
        ->writeHeader("Content-Type", "application/json")
        ->end(json({{"error", e.what()}}).dump());
    }
  });

  // --- Avatar upload ---

  app.post("/api/users/me/avatar", [this](auto* res, auto* req) {
    std::string user_id = get_user_id(res, req);
    if (user_id.empty()) return;

    std::string content_type(req->getQuery("content_type"));
    if (content_type.empty()) content_type = "image/png";

    // Only allow image types
    if (content_type.find("image/") != 0) {
      res->writeStatus("400")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Only image files are allowed for avatars"})");
      return;
    }

    auto body = std::make_shared<std::string>();
    int64_t max_size = 50 * 1024 * 1024;  // 50MB max for avatars

    res->onData(
      [this, res, body, max_size, user_id, content_type](std::string_view data, bool last) mutable {
        body->append(data);

        if (static_cast<int64_t>(body->size()) > max_size) {
          res->writeStatus("413")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"json({"error":"Avatar too large (max 20MB)"})json");
          return;
        }

        if (!last) return;

        try {
          // Delete old avatar file if exists
          auto current = db.find_user_by_id(user_id);
          if (current && !current->avatar_file_id.empty()) {
            std::string old_path = config.upload_dir + "/" + current->avatar_file_id;
            std::filesystem::remove(old_path);
          }

          // Generate file ID and save
          std::string file_id = format_utils::random_hex(32);
          std::string path = config.upload_dir + "/" + file_id;
          std::ofstream out(path, std::ios::binary);
          if (!out) {
            res->writeStatus("500")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Failed to save avatar"})");
            return;
          }
          out.write(body->data(), body->size());
          out.close();

          db.set_user_avatar(user_id, file_id);
          auto updated = db.find_user_by_id(user_id);

          json user_json = {
            {"id", updated->id},
            {"username", updated->username},
            {"display_name", updated->display_name},
            {"role", updated->role},
            {"is_online", updated->is_online},
            {"last_seen", updated->last_seen},
            {"bio", updated->bio},
            {"status", updated->status},
            {"avatar_file_id", updated->avatar_file_id},
            {"profile_color", updated->profile_color}};
          res->writeHeader("Content-Type", "application/json")->end(user_json.dump());

          json broadcast = {{"type", "user_updated"}, {"user", user_json}};
          ws.broadcast_to_presence(broadcast.dump());
        } catch (const std::exception& e) {
          res->writeStatus("500")
            ->writeHeader("Content-Type", "application/json")
            ->end(json({{"error", e.what()}}).dump());
        }
      });
    res->onAborted([]() {});
  });

  app.del("/api/users/me/avatar", [this](auto* res, auto* req) {
    std::string user_id = get_user_id(res, req);
    if (user_id.empty()) return;

    try {
      auto current = db.find_user_by_id(user_id);
      if (current && !current->avatar_file_id.empty()) {
        std::string old_path = config.upload_dir + "/" + current->avatar_file_id;
        std::filesystem::remove(old_path);
      }

      db.clear_user_avatar(user_id);
      auto updated = db.find_user_by_id(user_id);

      json user_json = {
        {"id", updated->id},
        {"username", updated->username},
        {"display_name", updated->display_name},
        {"role", updated->role},
        {"is_online", updated->is_online},
        {"last_seen", updated->last_seen},
        {"bio", updated->bio},
        {"status", updated->status},
        {"avatar_file_id", updated->avatar_file_id},
        {"profile_color", updated->profile_color}};
      res->writeHeader("Content-Type", "application/json")->end(user_json.dump());

      json broadcast = {{"type", "user_updated"}, {"user", user_json}};
      ws.broadcast_to_presence(broadcast.dump());
    } catch (const std::exception& e) {
      res->writeStatus("500")
        ->writeHeader("Content-Type", "application/json")
        ->end(json({{"error", e.what()}}).dump());
    }
  });

  // Serve avatar files (public, no auth needed for displaying in chat)
  app.get("/api/avatars/:id", [this](auto* res, auto* req) {
    std::string file_id(req->getParameter("id"));

    // Validate file_id is hex-only (prevent path traversal)
    for (char c : file_id) {
      if (!std::isxdigit(static_cast<unsigned char>(c))) {
        res->writeStatus("400")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Invalid file ID"})");
        return;
      }
    }

    std::string path = config.upload_dir + "/" + file_id;
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
      res->writeStatus("404")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Avatar not found"})");
      return;
    }

    auto size = in.tellg();
    in.seekg(0);
    std::string content(size, '\0');
    in.read(content.data(), size);

    res->writeHeader("Content-Type", "image/png")
      ->writeHeader("Cache-Control", "public, max-age=31536000, immutable")
      ->end(content);
  });

  // --- Passkey management ---

  app.get("/api/users/me/passkeys", [this](auto* res, auto* req) {
    std::string user_id = get_user_id(res, req);
    if (user_id.empty()) return;

    auto creds = db.list_webauthn_credentials(user_id);
    json arr = json::array();
    for (const auto& c : creds) {
      arr.push_back(
        {{"id", c.credential_id}, {"device_name", c.device_name}, {"created_at", c.created_at}});
    }
    res->writeHeader("Content-Type", "application/json")->end(arr.dump());
  });

  app.post("/api/users/me/passkeys/options", [this](auto* res, auto* req) {
    std::string user_id = get_user_id(res, req);
    if (user_id.empty()) return;

    try {
      auto user = db.find_user_by_id(user_id);
      if (!user) {
        res->writeStatus("404")
          ->writeHeader("Content-Type", "application/json")
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
        {"rp", {{"name", config.webauthn_rp_name}, {"id", config.webauthn_rp_id}}},
        {"user",
         {{"id", user_handle}, {"name", user->username}, {"displayName", user->display_name}}},
        {"challenge", challenge},
        {"pubKeyCredParams",
         json::array({
           {{"type", "public-key"}, {"alg", -7}},   // ES256
           {{"type", "public-key"}, {"alg", -257}}  // RS256
         })},
        {"excludeCredentials", exclude},
        {"authenticatorSelection",
         {{"residentKey", "required"}, {"userVerification", "preferred"}}},
        {"attestation", "none"},
        {"timeout", defaults::WEBAUTHN_TIMEOUT_MS}};

      res->writeHeader("Content-Type", "application/json")->end(options.dump());
    } catch (const std::exception& e) {
      res->writeStatus("400")
        ->writeHeader("Content-Type", "application/json")
        ->end(json({{"error", e.what()}}).dump());
    }
  });

  app.post("/api/users/me/passkeys/verify", [this](auto* res, auto* req) {
    std::string user_id = get_user_id(res, req);
    if (user_id.empty()) return;

    res->onData(
      [this, res, user_id, body = std::string()](std::string_view data, bool last) mutable {
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
            res->writeStatus("401")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Invalid or expired challenge"})");
            return;
          }

          auto extra = json::parse(stored->extra_data);
          if (extra.at("type") != "add_passkey" || extra.at("user_id") != user_id) {
            res->writeStatus("400")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Challenge mismatch"})");
            return;
          }

          auto result = webauthn::verify_registration(
            attestation_object,
            client_data_json,
            challenge,
            config.webauthn_origin,
            config.webauthn_rp_id);

          if (!result) {
            res->writeStatus("401")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"WebAuthn verification failed"})");
            return;
          }

          db.delete_webauthn_challenge(challenge);
          db.store_webauthn_credential(
            user_id,
            result->credential_id,
            result->public_key,
            result->sign_count,
            device_name,
            transports_str);

          res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
        } catch (const std::exception& e) {
          res->writeStatus("400")
            ->writeHeader("Content-Type", "application/json")
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
        res->writeStatus("400")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Cannot remove your only credential"})");
        return;
      }
      db.remove_webauthn_credential(credential_id, user_id);
      res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
    } catch (const std::exception& e) {
      res->writeStatus("400")
        ->writeHeader("Content-Type", "application/json")
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
      res->writeStatus("500")
        ->writeHeader("Content-Type", "application/json")
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
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Invalid or expired challenge"})");
          return;
        }

        auto extra = json::parse(stored->extra_data);
        if (extra.at("type") != "pki_add" || extra.at("user_id") != user_id) {
          res->writeStatus("400")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Challenge mismatch"})");
          return;
        }

        if (!webauthn::verify_pki_signature(public_key, challenge, signature)) {
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
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
        res->writeStatus("400")
          ->writeHeader("Content-Type", "application/json")
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
      arr.push_back({{"id", c.id}, {"device_name", c.device_name}, {"created_at", c.created_at}});
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
        res->writeStatus("400")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Cannot remove your only credential"})");
        return;
      }
      db.remove_pki_credential(key_id, user_id);
      res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
    } catch (const std::exception& e) {
      res->writeStatus("400")
        ->writeHeader("Content-Type", "application/json")
        ->end(json({{"error", e.what()}}).dump());
    }
  });

  // --- Device linking ---

  app.post("/api/users/me/device-tokens", [this](auto* res, auto* req) {
    std::string user_id = get_user_id(res, req);
    if (user_id.empty()) return;

    try {
      std::string token = db.create_device_token(user_id);
      json resp = {{"token", token}};
      res->writeHeader("Content-Type", "application/json")->end(resp.dump());
    } catch (const std::exception& e) {
      res->writeStatus("500")
        ->writeHeader("Content-Type", "application/json")
        ->end(json({{"error", e.what()}}).dump());
    }
  });

  app.get("/api/users/me/devices", [this](auto* res, auto* req) {
    std::string user_id = get_user_id(res, req);
    if (user_id.empty()) return;

    auto keys = db.list_user_keys(user_id);
    json arr = json::array();
    for (const auto& k : keys) {
      arr.push_back({{"id", k.id}, {"device_name", k.device_name}, {"created_at", k.created_at}});
    }
    res->writeHeader("Content-Type", "application/json")->end(arr.dump());
  });

  app.del("/api/users/me/devices/:id", [this](auto* res, auto* req) {
    std::string user_id = get_user_id(res, req);
    if (user_id.empty()) return;

    std::string key_id(req->getParameter(0));
    try {
      int total = db.count_user_credentials(user_id);
      if (total <= 1) {
        res->writeStatus("400")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Cannot remove your only credential"})");
        return;
      }
      db.remove_user_key(key_id, user_id);
      res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
    } catch (const std::exception& e) {
      res->writeStatus("400")
        ->writeHeader("Content-Type", "application/json")
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
      res->writeStatus("500")
        ->writeHeader("Content-Type", "application/json")
        ->end(json({{"error", e.what()}}).dump());
    }
  });

  // --- TOTP management ---

  app.get("/api/users/me/totp/status", [this](auto* res, auto* req) {
    std::string user_id = get_user_id(res, req);
    if (user_id.empty()) return;
    bool enabled = db.has_totp(user_id);
    json resp = {{"enabled", enabled}};
    res->writeHeader("Content-Type", "application/json")->end(resp.dump());
  });

  app.post("/api/users/me/totp/setup", [this](auto* res, auto* req) {
    std::string user_id = get_user_id(res, req);
    if (user_id.empty()) return;

    try {
      auto user = db.find_user_by_id(user_id);
      if (!user) {
        res->writeStatus("404")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"User not found"})");
        return;
      }

      auto secret = totp::generate_secret();
      db.store_totp_secret(user_id, secret);

      auto server_name = db.get_setting("server_name").value_or("EnclaveStation");
      auto uri = totp::build_uri(secret, user->username, server_name);

      json resp = {{"secret", secret}, {"uri", uri}};
      res->writeHeader("Content-Type", "application/json")->end(resp.dump());
    } catch (const std::exception& e) {
      res->writeStatus("500")
        ->writeHeader("Content-Type", "application/json")
        ->end(json({{"error", e.what()}}).dump());
    }
  });

  app.post("/api/users/me/totp/verify", [this](auto* res, auto* req) {
    std::string user_id = get_user_id(res, req);
    if (user_id.empty()) return;

    res->onData(
      [this, res, user_id, body = std::string()](std::string_view data, bool last) mutable {
        body.append(data);
        if (!last) return;

        try {
          auto j = json::parse(body);
          std::string code = j.at("code");

          auto secret = db.get_unverified_totp_secret(user_id);
          if (!secret) {
            res->writeStatus("400")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"No TOTP setup in progress"})");
            return;
          }

          if (!totp::verify_code(*secret, code)) {
            res->writeStatus("401")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Invalid verification code"})");
            return;
          }

          db.verify_totp(user_id);
          res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
        } catch (const std::exception& e) {
          res->writeStatus("400")
            ->writeHeader("Content-Type", "application/json")
            ->end(json({{"error", e.what()}}).dump());
        }
      });
    res->onAborted([]() {});
  });

  app.del("/api/users/me/totp", [this](auto* res, auto* req) {
    std::string user_id = get_user_id(res, req);
    if (user_id.empty()) return;

    res->onData([this, res, user_id, body = std::string()](
                  std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;

      try {
        auto j = json::parse(body);
        std::string code = j.at("code");

        auto secret = db.get_totp_secret(user_id);
        if (!secret) {
          res->writeStatus("400")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"TOTP is not enabled"})");
          return;
        }

        if (!totp::verify_code(*secret, code)) {
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Invalid verification code"})");
          return;
        }

        // Check if user has auth methods that require MFA
        auto check_mfa = [&](const std::string& method) -> bool {
          auto val = db.get_setting("mfa_required_" + method);
          return val && *val == "true";
        };

        std::vector<std::string> blocking_methods;
        if (check_mfa("password") && db.has_password(user_id))
          blocking_methods.push_back("password");
        if (check_mfa("pki") && !db.list_pki_credentials(user_id).empty())
          blocking_methods.push_back("browser key");
        if (check_mfa("passkey") && !db.list_webauthn_credentials(user_id).empty())
          blocking_methods.push_back("passkey");

        if (!blocking_methods.empty()) {
          std::string methods_str;
          for (size_t i = 0; i < blocking_methods.size(); i++) {
            if (i > 0) methods_str += (i == blocking_methods.size() - 1) ? " and " : ", ";
            methods_str += blocking_methods[i];
          }
          res->writeStatus("403")
            ->writeHeader("Content-Type", "application/json")
            ->end(
              json(
                {{"error",
                  "Cannot disable two-factor authentication because this server requires it for " +
                    methods_str +
                    " login. Remove those authentication methods first or contact an "
                    "administrator."}})
                .dump());
          return;
        }

        db.delete_totp(user_id);
        res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
      } catch (const std::exception& e) {
        res->writeStatus("400")
          ->writeHeader("Content-Type", "application/json")
          ->end(json({{"error", e.what()}}).dump());
      }
    });
    res->onAborted([]() {});
  });

  // User settings (key-value preferences)
  app.get("/api/users/me/settings", [this](auto* res, auto* req) {
    auto user_id = get_user_id(res, req);
    if (user_id.empty()) return;
    auto settings = db.get_all_user_settings(user_id);
    json j = json::object();
    for (const auto& [key, value] : settings) {
      j[key] = value;
    }
    res->writeHeader("Content-Type", "application/json")->end(j.dump());
  });

  app.put("/api/users/me/settings", [this](auto* res, auto* req) {
    auto user_id_copy = get_user_id(res, req);
    std::string body;
    res->onData([this, res, user_id = std::move(user_id_copy), body = std::move(body)](
                  std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      if (user_id.empty()) return;
      try {
        auto j = json::parse(body);
        for (auto& [key, value] : j.items()) {
          if (!value.is_string()) continue;
          // Only allow agent_* settings
          if (key.substr(0, 6) != "agent_") continue;
          db.set_user_setting(user_id, key, value.get<std::string>());
        }
        res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
      } catch (const std::exception& e) {
        res->writeStatus("400")
          ->writeHeader("Content-Type", "application/json")
          ->end(json({{"error", e.what()}}).dump());
      }
    });
    res->onAborted([]() {});
  });
}

template <bool SSL>
std::string UserHandler<SSL>::get_user_id(uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req) {
  return validate_session_or_401(res, req, db);
}

template struct UserHandler<false>;
template struct UserHandler<true>;
