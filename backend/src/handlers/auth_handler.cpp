#include "handlers/auth_handler.h"
#include "handlers/auth_payload_utils.h"
#include "handlers/auth_utils.h"

using json = nlohmann::json;

template <bool SSL>
void AuthHandler<SSL>::register_routes(uWS::TemplatedApp<SSL>& app) {
  // WebAuthn (passkey) routes
  app.post("/api/auth/register/options", [this](auto* res, auto* req) {
    auto aborted = std::make_shared<bool>(false);
    std::string body;
    res->onAborted([aborted]() { *aborted = true; });
    res->onData(
      [this, res, aborted, body = std::move(body)](std::string_view data, bool last) mutable {
        body.append(data);
        if (!last) return;
        pool_.submit([this, res, aborted, body = std::move(body)]() {
          handle_register_options(res, aborted, body);
        });
      });
  });

  app.post("/api/auth/register/verify", [this](auto* res, auto* req) {
    auto aborted = std::make_shared<bool>(false);
    std::string body;
    res->onAborted([aborted]() { *aborted = true; });
    res->onData(
      [this, res, aborted, body = std::move(body)](std::string_view data, bool last) mutable {
        body.append(data);
        if (!last) return;
        pool_.submit([this, res, aborted, body = std::move(body)]() {
          handle_register_verify(res, aborted, body);
        });
      });
  });

  app.post("/api/auth/login/options", [this](auto* res, auto* req) {
    auto aborted = std::make_shared<bool>(false);
    std::string body;
    res->onAborted([aborted]() { *aborted = true; });
    res->onData(
      [this, res, aborted, body = std::move(body)](std::string_view data, bool last) mutable {
        body.append(data);
        if (!last) return;
        pool_.submit([this, res, aborted, body = std::move(body)]() {
          handle_login_options(res, aborted, body);
        });
      });
  });

  app.post("/api/auth/login/verify", [this](auto* res, auto* req) {
    auto aborted = std::make_shared<bool>(false);
    std::string body;
    res->onAborted([aborted]() { *aborted = true; });
    res->onData(
      [this, res, aborted, body = std::move(body)](std::string_view data, bool last) mutable {
        body.append(data);
        if (!last) return;
        pool_.submit([this, res, aborted, body = std::move(body)]() {
          handle_login_verify(res, aborted, body);
        });
      });
  });

  // PKI (browser key) routes
  app.post("/api/auth/pki/challenge", [this](auto* res, auto* req) {
    auto aborted = std::make_shared<bool>(false);
    std::string body;
    res->onAborted([aborted]() { *aborted = true; });
    res->onData(
      [this, res, aborted, body = std::move(body)](std::string_view data, bool last) mutable {
        body.append(data);
        if (!last) return;
        pool_.submit([this, res, aborted, body = std::move(body)]() {
          handle_pki_challenge(res, aborted, body);
        });
      });
  });

  app.post("/api/auth/pki/register", [this](auto* res, auto* req) {
    auto aborted = std::make_shared<bool>(false);
    std::string body;
    res->onAborted([aborted]() { *aborted = true; });
    res->onData(
      [this, res, aborted, body = std::move(body)](std::string_view data, bool last) mutable {
        body.append(data);
        if (!last) return;
        pool_.submit([this, res, aborted, body = std::move(body)]() {
          handle_pki_register(res, aborted, body);
        });
      });
  });

  app.post("/api/auth/pki/login", [this](auto* res, auto* req) {
    auto aborted = std::make_shared<bool>(false);
    std::string body;
    res->onAborted([aborted]() { *aborted = true; });
    res->onData(
      [this, res, aborted, body = std::move(body)](std::string_view data, bool last) mutable {
        body.append(data);
        if (!last) return;
        pool_.submit(
          [this, res, aborted, body = std::move(body)]() { handle_pki_login(res, aborted, body); });
      });
  });

  // Recovery key login
  app.post("/api/auth/recovery", [this](auto* res, auto* req) {
    auto aborted = std::make_shared<bool>(false);
    std::string body;
    res->onAborted([aborted]() { *aborted = true; });
    res->onData(
      [this, res, aborted, body = std::move(body)](std::string_view data, bool last) mutable {
        body.append(data);
        if (!last) return;
        pool_.submit([this, res, aborted, body = std::move(body)]() {
          handle_recovery_login(res, aborted, body);
        });
      });
  });

  // Recovery token (admin-generated) login
  app.post("/api/auth/recover-account", [this](auto* res, auto* req) {
    auto aborted = std::make_shared<bool>(false);
    std::string body;
    res->onAborted([aborted]() { *aborted = true; });
    res->onData(
      [this, res, aborted, body = std::move(body)](std::string_view data, bool last) mutable {
        body.append(data);
        if (!last) return;
        pool_.submit([this, res, aborted, body = std::move(body)]() {
          handle_recovery_token_login(res, aborted, body);
        });
      });
  });

  // Join request routes
  app.post("/api/auth/request-access/options", [this](auto* res, auto* req) {
    auto aborted = std::make_shared<bool>(false);
    std::string body;
    res->onAborted([aborted]() { *aborted = true; });
    res->onData(
      [this, res, aborted, body = std::move(body)](std::string_view data, bool last) mutable {
        body.append(data);
        if (!last) return;
        pool_.submit([this, res, aborted, body = std::move(body)]() {
          handle_request_access_options(res, aborted, body);
        });
      });
  });

  app.post("/api/auth/request-access", [this](auto* res, auto* req) {
    auto aborted = std::make_shared<bool>(false);
    std::string body;
    res->onAborted([aborted]() { *aborted = true; });
    res->onData(
      [this, res, aborted, body = std::move(body)](std::string_view data, bool last) mutable {
        body.append(data);
        if (!last) return;
        pool_.submit([this, res, aborted, body = std::move(body)]() {
          handle_request_access(res, aborted, body);
        });
      });
  });

  app.get("/api/auth/request-status/:id", [this](auto* res, auto* req) {
    auto aborted = std::make_shared<bool>(false);
    std::string request_id(req->getParameter(0));
    res->onAborted([aborted]() { *aborted = true; });
    pool_.submit([this, res, aborted, request_id = std::move(request_id)]() {
      handle_request_status(res, aborted, request_id);
    });
  });

  // Password auth routes
  app.post("/api/auth/password/register", [this](auto* res, auto* req) {
    auto aborted = std::make_shared<bool>(false);
    std::string body;
    res->onAborted([aborted]() { *aborted = true; });
    res->onData(
      [this, res, aborted, body = std::move(body)](std::string_view data, bool last) mutable {
        body.append(data);
        if (!last) return;
        pool_.submit([this, res, aborted, body = std::move(body)]() {
          handle_password_register(res, aborted, body);
        });
      });
  });

  app.post("/api/auth/password/login", [this](auto* res, auto* req) {
    auto aborted = std::make_shared<bool>(false);
    std::string body;
    res->onAborted([aborted]() { *aborted = true; });
    res->onData(
      [this, res, aborted, body = std::move(body)](std::string_view data, bool last) mutable {
        body.append(data);
        if (!last) return;
        pool_.submit([this, res, aborted, body = std::move(body)]() {
          handle_password_login(res, aborted, body);
        });
      });
  });

  app.post("/api/auth/password/change", [this](auto* res, auto* req) {
    auto aborted = std::make_shared<bool>(false);
    auto token = extract_bearer_token(req);
    std::string body;
    res->onAborted([aborted]() { *aborted = true; });
    res->onData([this, res, aborted, token = std::move(token), body = std::move(body)](
                  std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this, res, aborted, body = std::move(body), token = std::move(token)]() {
        handle_password_change(res, aborted, body, token);
      });
    });
  });

  app.post("/api/auth/password/set", [this](auto* res, auto* req) {
    auto aborted = std::make_shared<bool>(false);
    auto token = extract_bearer_token(req);
    std::string body;
    res->onAborted([aborted]() { *aborted = true; });
    res->onData([this, res, aborted, token = std::move(token), body = std::move(body)](
                  std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this, res, aborted, body = std::move(body), token = std::move(token)]() {
        handle_password_set(res, aborted, body, token);
      });
    });
  });

  app.del("/api/auth/password", [this](auto* res, auto* req) {
    auto aborted = std::make_shared<bool>(false);
    auto token = extract_bearer_token(req);
    res->onAborted([aborted]() { *aborted = true; });
    pool_.submit([this, res, aborted, token = std::move(token)]() {
      handle_password_delete(res, aborted, token);
    });
  });

  app.post("/api/auth/mfa/verify", [this](auto* res, auto* req) {
    auto aborted = std::make_shared<bool>(false);
    std::string body;
    res->onAborted([aborted]() { *aborted = true; });
    res->onData(
      [this, res, aborted, body = std::move(body)](std::string_view data, bool last) mutable {
        body.append(data);
        if (!last) return;
        pool_.submit([this, res, aborted, body = std::move(body)]() {
          handle_mfa_verify(res, aborted, body);
        });
      });
  });

  app.post("/api/auth/mfa/setup", [this](auto* res, auto* req) {
    auto aborted = std::make_shared<bool>(false);
    std::string body;
    res->onAborted([aborted]() { *aborted = true; });
    res->onData(
      [this, res, aborted, body = std::move(body)](std::string_view data, bool last) mutable {
        body.append(data);
        if (!last) return;
        pool_.submit(
          [this, res, aborted, body = std::move(body)]() { handle_mfa_setup(res, aborted, body); });
      });
  });

  app.post("/api/auth/mfa/setup/verify", [this](auto* res, auto* req) {
    auto aborted = std::make_shared<bool>(false);
    std::string body;
    res->onAborted([aborted]() { *aborted = true; });
    res->onData(
      [this, res, aborted, body = std::move(body)](std::string_view data, bool last) mutable {
        body.append(data);
        if (!last) return;
        pool_.submit([this, res, aborted, body = std::move(body)]() {
          handle_mfa_setup_verify(res, aborted, body);
        });
      });
  });

  // Device linking - get challenge for PKI device linking
  app.post("/api/auth/add-device/pki/challenge", [this](auto* res, auto* req) {
    auto aborted = std::make_shared<bool>(false);
    std::string body;
    res->onAborted([aborted]() { *aborted = true; });
    res->onData(
      [this, res, aborted, body = std::move(body)](std::string_view data, bool last) mutable {
        body.append(data);
        if (!last) return;
        pool_.submit([this, res, aborted, body = std::move(body)]() {
          try {
            auto j = json::parse(body);
            std::string device_token = j.at("device_token");
            auto user_id = db.validate_device_token(device_token);
            if (!user_id) {
              loop_->defer([res, aborted]() {
                if (*aborted) return;
                res->writeStatus("401")
                  ->writeHeader("Content-Type", "application/json")
                  ->end(R"({"error":"Invalid or expired device token"})");
              });
              return;
            }
            std::string challenge = webauthn::generate_challenge();
            json extra = auth_payload_utils::build_pki_challenge_extra("device_pki");
            db.store_webauthn_challenge(challenge, extra.dump());
            json resp = auth_payload_utils::build_challenge_response(challenge);
            auto resp_body = resp.dump();
            loop_->defer([res, aborted, resp_body = std::move(resp_body)]() {
              if (*aborted) return;
              res->writeHeader("Content-Type", "application/json")->end(resp_body);
            });
          } catch (const std::exception& e) {
            auto err = json({{"error", e.what()}}).dump();
            loop_->defer([res, aborted, err = std::move(err)]() {
              if (*aborted) return;
              res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
            });
          }
        });
      });
  });

  // Device linking - add browser key via device token
  app.post("/api/auth/add-device/pki", [this](auto* res, auto* req) {
    auto aborted = std::make_shared<bool>(false);
    std::string body;
    res->onAborted([aborted]() { *aborted = true; });
    res->onData(
      [this, res, aborted, body = std::move(body)](std::string_view data, bool last) mutable {
        body.append(data);
        if (!last) return;
        pool_.submit([this, res, aborted, body = std::move(body)]() {
          handle_add_device_pki(res, aborted, body);
        });
      });
  });

  // Device linking - passkey registration options
  app.post("/api/auth/add-device/passkey/options", [this](auto* res, auto* req) {
    auto aborted = std::make_shared<bool>(false);
    std::string body;
    res->onAborted([aborted]() { *aborted = true; });
    res->onData(
      [this, res, aborted, body = std::move(body)](std::string_view data, bool last) mutable {
        body.append(data);
        if (!last) return;
        pool_.submit([this, res, aborted, body = std::move(body)]() {
          handle_add_device_passkey_options(res, aborted, body);
        });
      });
  });

  // Device linking - passkey registration verify
  app.post("/api/auth/add-device/passkey/verify", [this](auto* res, auto* req) {
    auto aborted = std::make_shared<bool>(false);
    std::string body;
    res->onAborted([aborted]() { *aborted = true; });
    res->onData(
      [this, res, aborted, body = std::move(body)](std::string_view data, bool last) mutable {
        body.append(data);
        if (!last) return;
        pool_.submit([this, res, aborted, body = std::move(body)]() {
          handle_add_device_passkey_verify(res, aborted, body);
        });
      });
  });

  app.post("/api/auth/logout", [this](auto* res, auto* req) {
    auto aborted = std::make_shared<bool>(false);
    auto token = extract_bearer_token(req);
    res->onAborted([aborted]() { *aborted = true; });
    pool_.submit([this, res, aborted, token = std::move(token)]() {
      db.delete_session(token);
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        // Clear the session/csrf cookies so cookie-authed clients log out too.
        emit_clear_session_cookies(res);
        res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
      });
    });
  });
}

template <bool SSL>
bool AuthHandler<SSL>::is_method_enabled(const std::string& method) {
  return is_auth_method_enabled(db, method);
}

template <bool SSL>
int AuthHandler<SSL>::get_session_expiry() {
  return ::get_session_expiry(db, config);
}

template <bool SSL>
bool AuthHandler<SSL>::is_mfa_required_for_method(const std::string& method) {
  return auth_utils::is_required_setting_enabled(db.get_setting("mfa_required_" + method));
}

template <bool SSL>
bool AuthHandler<SSL>::check_and_handle_mfa(
  uWS::HttpResponse<SSL>* res,
  std::shared_ptr<bool> aborted,
  const User& user,
  const std::string& auth_method) {
  bool mfa_required = is_mfa_required_for_method(auth_method);
  bool user_has_totp = db.has_totp(user.id);
  if (!mfa_required && !user_has_totp) return false;
  auto mfa_token = db.create_mfa_pending_token(user.id, auth_method);
  auto decision = auth_utils::build_mfa_decision(mfa_required, user_has_totp, mfa_token);

  auto resp_body = decision.response.dump();
  loop_->defer([res, aborted, resp_body = std::move(resp_body)]() {
    if (*aborted) return;
    res->writeHeader("Content-Type", "application/json")->end(resp_body);
  });
  return true;
}

template <bool SSL>
std::string AuthHandler<SSL>::check_registration_eligibility(
  const std::string& username, const std::string& invite_token) {
  (void)username;
  bool has_invite = !invite_token.empty();
  bool invite_valid = has_invite && db.validate_invite(invite_token);
  return auth_utils::registration_eligibility_message(
    db.count_users() == 0,
    db.get_setting("registration_mode").value_or("invite"),
    has_invite,
    invite_valid);
}

template <bool SSL>
void AuthHandler<SSL>::complete_user_creation(User& user, const std::string& invite_token) {
  if (!invite_token.empty()) {
    db.use_invite(invite_token, user.id);
  }
}

template <bool SSL>
std::string AuthHandler<SSL>::generate_user_handle() {
  return auth_utils::generate_user_handle();
}

template <bool SSL>
json AuthHandler<SSL>::make_user_json(const User& user) {
  return auth_utils::make_user_json(user, db.has_password(user.id), db.has_totp(user.id));
}

// --- WebAuthn (passkey) handlers ---

template <bool SSL>
void AuthHandler<SSL>::handle_register_options(
  uWS::HttpResponse<SSL>* res, std::shared_ptr<bool> aborted, const std::string& body) {
  try {
    if (!is_method_enabled("passkey")) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Passkey authentication is not enabled on this server"})");
      });
      return;
    }

    auto j = json::parse(body);
    std::string username = j.at("username");
    std::string display_name = j.at("display_name");
    std::string invite_token = j.value("token", "");

    auto err = check_registration_eligibility(username, invite_token);
    if (!err.empty()) {
      auto err_body = json({{"error", err}}).dump();
      loop_->defer([res, aborted, err_body = std::move(err_body)]() {
        if (*aborted) return;
        res->writeStatus("403")->writeHeader("Content-Type", "application/json")->end(err_body);
      });
      return;
    }

    std::string challenge = webauthn::generate_challenge();
    std::string user_handle = generate_user_handle();

    json extra = auth_payload_utils::build_registration_challenge_extra(
      "registration", username, display_name, user_handle, invite_token);
    db.store_webauthn_challenge(challenge, extra.dump());

    json options = auth_payload_utils::build_passkey_registration_options(
      config, challenge, user_handle, username, display_name);

    auto resp_body = options.dump();
    loop_->defer([res, aborted, resp_body = std::move(resp_body)]() {
      if (*aborted) return;
      res->writeHeader("Content-Type", "application/json")->end(resp_body);
    });
  } catch (const std::exception& e) {
    auto err_body = json({{"error", e.what()}}).dump();
    loop_->defer([res, aborted, err_body = std::move(err_body)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
    });
  }
}

template <bool SSL>
void AuthHandler<SSL>::handle_register_verify(
  uWS::HttpResponse<SSL>* res, std::shared_ptr<bool> aborted, const std::string& body) {
  try {
    if (!is_method_enabled("passkey")) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Passkey authentication is not enabled on this server"})");
      });
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

    std::string challenge =
      auth_payload_utils::extract_challenge_from_client_data_b64(client_data_json);

    auto stored = db.get_webauthn_challenge(challenge);
    if (!stored) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Invalid or expired challenge"})");
      });
      return;
    }

    auto extra = auth_payload_utils::parse_challenge_extra(stored->extra_data);
    if (!auth_payload_utils::challenge_has_type(extra, "registration")) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("400")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Challenge is not for registration"})");
      });
      return;
    }

    auto result = webauthn::verify_registration(
      attestation_object,
      client_data_json,
      challenge,
      config.webauthn_origin,
      config.webauthn_rp_id);

    db.delete_webauthn_challenge(challenge);

    std::string username = extra.at("username");
    std::string display_name = extra.at("display_name");
    std::string invite_token = extra.value("invite_token", "");
    bool is_first_user = (db.count_users() == 0);
    std::string role = is_first_user ? "owner" : "user";

    auto user = db.create_user(username, display_name, "", role);
    db.store_webauthn_credential(
      user.id,
      result->credential_id,
      result->public_key,
      result->sign_count,
      "Passkey",
      transports_str);

    complete_user_creation(user, invite_token);

    std::string token = db.create_session(user.id, get_session_expiry());
    int max_age = get_session_expiry() * 3600;
    bool secure = config.has_ssl();
    json resp = auth_payload_utils::build_token_user_response(token, make_user_json(user));
    auto resp_body = resp.dump();
    loop_->defer([res, aborted, resp_body = std::move(resp_body), token, max_age, secure]() {
      if (*aborted) return;
      emit_session_cookies(res, token, max_age, secure);
      res->writeHeader("Content-Type", "application/json")->end(resp_body);
    });
  } catch (const pqxx::unique_violation&) {
    loop_->defer([res, aborted]() {
      if (*aborted) return;
      res->writeStatus("409")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Username already taken"})");
    });
  } catch (const std::exception& e) {
    auto err_body = json({{"error", e.what()}}).dump();
    loop_->defer([res, aborted, err_body = std::move(err_body)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
    });
  }
}

template <bool SSL>
void AuthHandler<SSL>::handle_login_options(
  uWS::HttpResponse<SSL>* res, std::shared_ptr<bool> aborted, const std::string& body) {
  try {
    if (!is_method_enabled("passkey")) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Passkey authentication is not enabled on this server"})");
      });
      return;
    }

    std::string challenge = webauthn::generate_challenge();
    json extra = auth_payload_utils::build_authentication_challenge_extra();
    db.store_webauthn_challenge(challenge, extra.dump());

    json options = auth_payload_utils::build_passkey_login_options(config, challenge);

    auto resp_body = options.dump();
    loop_->defer([res, aborted, resp_body = std::move(resp_body)]() {
      if (*aborted) return;
      res->writeHeader("Content-Type", "application/json")->end(resp_body);
    });
  } catch (const std::exception& e) {
    auto err_body = json({{"error", e.what()}}).dump();
    loop_->defer([res, aborted, err_body = std::move(err_body)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
    });
  }
}

template <bool SSL>
void AuthHandler<SSL>::handle_login_verify(
  uWS::HttpResponse<SSL>* res, std::shared_ptr<bool> aborted, const std::string& body) {
  try {
    if (!is_method_enabled("passkey")) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Passkey authentication is not enabled on this server"})");
      });
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
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Unknown credential"})");
      });
      return;
    }

    std::string challenge =
      auth_payload_utils::extract_challenge_from_client_data_b64(client_data_json);

    auto stored = db.get_webauthn_challenge(challenge);
    if (!stored) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Invalid or expired challenge"})");
      });
      return;
    }

    auto extra = auth_payload_utils::parse_challenge_extra(stored->extra_data);
    if (!auth_payload_utils::challenge_has_type(extra, "authentication")) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("400")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Challenge is not for authentication"})");
      });
      return;
    }

    auto new_sign_count = webauthn::verify_authentication(
      auth_data,
      client_data_json,
      signature,
      cred->public_key,
      cred->sign_count,
      challenge,
      config.webauthn_origin,
      config.webauthn_rp_id);

    if (!new_sign_count) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"WebAuthn verification failed"})");
      });
      return;
    }

    db.delete_webauthn_challenge(challenge);
    db.update_webauthn_sign_count(credential_id, *new_sign_count);

    auto user = db.find_user_by_credential_id(credential_id);
    if (!user) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("404")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"User not found"})");
      });
      return;
    }

    if (user->is_banned) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Your account has been banned"})");
      });
      return;
    }

    if (db.is_server_locked_down() && user->role != "admin" && user->role != "owner") {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Server is in lockdown mode. Only administrators may log in."})");
      });
      return;
    }

    if (check_and_handle_mfa(res, aborted, *user, "passkey")) return;

    std::string token = db.create_session(user->id, get_session_expiry());
    int max_age = get_session_expiry() * 3600;
    bool secure = config.has_ssl();
    json resp = auth_payload_utils::build_token_user_response(token, make_user_json(*user));
    auto resp_body = resp.dump();
    loop_->defer([res, aborted, resp_body = std::move(resp_body), token, max_age, secure]() {
      if (*aborted) return;
      emit_session_cookies(res, token, max_age, secure);
      res->writeHeader("Content-Type", "application/json")->end(resp_body);
    });
  } catch (const std::exception& e) {
    auto err_body = json({{"error", e.what()}}).dump();
    loop_->defer([res, aborted, err_body = std::move(err_body)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
    });
  }
}

// --- PKI (browser key) handlers ---

template <bool SSL>
void AuthHandler<SSL>::handle_pki_challenge(
  uWS::HttpResponse<SSL>* res, std::shared_ptr<bool> aborted, const std::string& body) {
  try {
    if (!is_method_enabled("pki")) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Browser key authentication is not enabled on this server"})");
      });
      return;
    }

    auto j = json::parse(body);
    std::string public_key = j.value("public_key", "");

    std::string challenge = webauthn::generate_challenge();
    std::string type = public_key.empty() ? "pki_registration" : "pki_login";

    json extra = auth_payload_utils::build_pki_challenge_extra(
      type, public_key.empty() ? std::nullopt : std::make_optional(public_key));
    db.store_webauthn_challenge(challenge, extra.dump());

    json resp = auth_payload_utils::build_challenge_response(challenge);
    auto resp_body = resp.dump();
    loop_->defer([res, aborted, resp_body = std::move(resp_body)]() {
      if (*aborted) return;
      res->writeHeader("Content-Type", "application/json")->end(resp_body);
    });
  } catch (const std::exception& e) {
    auto err_body = json({{"error", e.what()}}).dump();
    loop_->defer([res, aborted, err_body = std::move(err_body)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
    });
  }
}

template <bool SSL>
void AuthHandler<SSL>::handle_pki_register(
  uWS::HttpResponse<SSL>* res, std::shared_ptr<bool> aborted, const std::string& body) {
  try {
    if (!is_method_enabled("pki")) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Browser key authentication is not enabled on this server"})");
      });
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
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Invalid or expired challenge"})");
      });
      return;
    }
    auto extra = auth_payload_utils::parse_challenge_extra(stored->extra_data);
    if (!auth_payload_utils::challenge_has_type(extra, "pki_registration")) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("400")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Challenge is not for PKI registration"})");
      });
      return;
    }

    // Verify signature
    if (!webauthn::verify_pki_signature(public_key, challenge, signature)) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Signature verification failed"})");
      });
      return;
    }

    db.delete_webauthn_challenge(challenge);

    // Check registration eligibility
    auto err = check_registration_eligibility(username, invite_token);
    if (!err.empty()) {
      auto err_body = json({{"error", err}}).dump();
      loop_->defer([res, aborted, err_body = std::move(err_body)]() {
        if (*aborted) return;
        res->writeStatus("403")->writeHeader("Content-Type", "application/json")->end(err_body);
      });
      return;
    }

    bool is_first_user = (db.count_users() == 0);
    std::string role = is_first_user ? "owner" : "user";

    auto user = db.create_user(username, display_name, "", role);
    db.store_pki_credential(user.id, public_key);

    // Generate recovery keys
    auto [plaintext_keys, key_hashes] = webauthn::generate_recovery_keys();
    db.store_recovery_keys(user.id, key_hashes);

    complete_user_creation(user, invite_token);

    std::string token = db.create_session(user.id, get_session_expiry());
    int max_age = get_session_expiry() * 3600;
    bool secure = config.has_ssl();
    json resp = auth_payload_utils::build_token_user_response(
      token, make_user_json(user), json{{"recovery_keys", plaintext_keys}});
    auto resp_body = resp.dump();
    loop_->defer([res, aborted, resp_body = std::move(resp_body), token, max_age, secure]() {
      if (*aborted) return;
      emit_session_cookies(res, token, max_age, secure);
      res->writeHeader("Content-Type", "application/json")->end(resp_body);
    });
  } catch (const pqxx::unique_violation&) {
    loop_->defer([res, aborted]() {
      if (*aborted) return;
      res->writeStatus("409")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Username already taken"})");
    });
  } catch (const std::exception& e) {
    auto err_body = json({{"error", e.what()}}).dump();
    loop_->defer([res, aborted, err_body = std::move(err_body)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
    });
  }
}

template <bool SSL>
void AuthHandler<SSL>::handle_pki_login(
  uWS::HttpResponse<SSL>* res, std::shared_ptr<bool> aborted, const std::string& body) {
  try {
    if (!is_method_enabled("pki")) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Browser key authentication is not enabled on this server"})");
      });
      return;
    }

    auto j = json::parse(body);
    std::string public_key = j.at("public_key");
    std::string challenge = j.at("challenge");
    std::string signature = j.at("signature");

    // Verify challenge
    auto stored = db.get_webauthn_challenge(challenge);
    if (!stored) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Invalid or expired challenge"})");
      });
      return;
    }
    auto extra = auth_payload_utils::parse_challenge_extra(stored->extra_data);
    if (!auth_payload_utils::challenge_has_type(extra, "pki_login")) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("400")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Challenge is not for PKI login"})");
      });
      return;
    }

    // Verify signature
    if (!webauthn::verify_pki_signature(public_key, challenge, signature)) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Signature verification failed"})");
      });
      return;
    }

    db.delete_webauthn_challenge(challenge);

    // Find user
    auto user = db.find_user_by_pki_key(public_key);
    if (!user) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"No account found for this browser key"})");
      });
      return;
    }

    if (user->is_banned) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Your account has been banned"})");
      });
      return;
    }

    if (db.is_server_locked_down() && user->role != "admin" && user->role != "owner") {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Server is in lockdown mode. Only administrators may log in."})");
      });
      return;
    }

    if (check_and_handle_mfa(res, aborted, *user, "pki")) return;

    std::string token = db.create_session(user->id, get_session_expiry());
    int max_age = get_session_expiry() * 3600;
    bool secure = config.has_ssl();
    json resp = auth_payload_utils::build_token_user_response(token, make_user_json(*user));
    auto resp_body = resp.dump();
    loop_->defer([res, aborted, resp_body = std::move(resp_body), token, max_age, secure]() {
      if (*aborted) return;
      emit_session_cookies(res, token, max_age, secure);
      res->writeHeader("Content-Type", "application/json")->end(resp_body);
    });
  } catch (const std::exception& e) {
    auto err_body = json({{"error", e.what()}}).dump();
    loop_->defer([res, aborted, err_body = std::move(err_body)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
    });
  }
}

// --- Device linking handlers ---

template <bool SSL>
void AuthHandler<SSL>::handle_add_device_pki(
  uWS::HttpResponse<SSL>* res, std::shared_ptr<bool> aborted, const std::string& body) {
  try {
    if (!is_method_enabled("pki")) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Browser key authentication is not enabled"})");
      });
      return;
    }

    auto j = json::parse(body);
    std::string device_token = j.at("device_token");
    std::string public_key = j.at("public_key");
    std::string challenge = j.at("challenge");
    std::string signature = j.at("signature");
    std::string device_name = j.value("device_name", "Browser Key");

    auto user_id = db.validate_device_token(device_token);
    if (!user_id) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Invalid or expired device token"})");
      });
      return;
    }

    // Verify the challenge
    auto stored = db.get_webauthn_challenge(challenge);
    if (!stored) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Invalid or expired challenge"})");
      });
      return;
    }
    auto extra = auth_payload_utils::parse_challenge_extra(stored->extra_data);
    if (!auth_payload_utils::challenge_has_type(extra, "device_pki")) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("400")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Challenge mismatch"})");
      });
      return;
    }

    if (!webauthn::verify_pki_signature(public_key, challenge, signature)) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Signature verification failed"})");
      });
      return;
    }

    db.delete_webauthn_challenge(challenge);
    db.mark_device_token_used(device_token);
    db.store_pki_credential(*user_id, public_key, device_name);

    auto user = db.find_user_by_id(*user_id);
    if (!user) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("404")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"User not found"})");
      });
      return;
    }

    std::string session = db.create_session(user->id, get_session_expiry());
    int max_age = get_session_expiry() * 3600;
    bool secure = config.has_ssl();
    json resp = auth_payload_utils::build_token_user_response(session, make_user_json(*user));
    auto resp_body = resp.dump();
    loop_->defer([res, aborted, resp_body = std::move(resp_body), session, max_age, secure]() {
      if (*aborted) return;
      emit_session_cookies(res, session, max_age, secure);
      res->writeHeader("Content-Type", "application/json")->end(resp_body);
    });
  } catch (const std::exception& e) {
    auto err_body = json({{"error", e.what()}}).dump();
    loop_->defer([res, aborted, err_body = std::move(err_body)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
    });
  }
}

template <bool SSL>
void AuthHandler<SSL>::handle_add_device_passkey_options(
  uWS::HttpResponse<SSL>* res, std::shared_ptr<bool> aborted, const std::string& body) {
  try {
    if (!is_method_enabled("passkey")) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Passkey authentication is not enabled"})");
      });
      return;
    }

    auto j = json::parse(body);
    std::string device_token = j.at("device_token");

    auto user_id = db.validate_device_token(device_token);
    if (!user_id) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Invalid or expired device token"})");
      });
      return;
    }

    auto user = db.find_user_by_id(*user_id);
    if (!user) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("404")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"User not found"})");
      });
      return;
    }

    std::string challenge = webauthn::generate_challenge();

    auto existing = db.list_webauthn_credentials(*user_id);
    json exclude = auth_payload_utils::build_exclude_credentials(existing);

    auto uid_bytes = std::vector<unsigned char>(user_id->begin(), user_id->end());
    std::string user_handle = webauthn::base64url_encode(uid_bytes);

    json extra = auth_payload_utils::build_device_passkey_challenge_extra(*user_id, device_token);
    db.store_webauthn_challenge(challenge, extra.dump());

    json options = auth_payload_utils::build_passkey_registration_options(
      config, challenge, user_handle, user->username, user->display_name, exclude);

    auto resp_body = options.dump();
    loop_->defer([res, aborted, resp_body = std::move(resp_body)]() {
      if (*aborted) return;
      res->writeHeader("Content-Type", "application/json")->end(resp_body);
    });
  } catch (const std::exception& e) {
    auto err_body = json({{"error", e.what()}}).dump();
    loop_->defer([res, aborted, err_body = std::move(err_body)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
    });
  }
}

template <bool SSL>
void AuthHandler<SSL>::handle_add_device_passkey_verify(
  uWS::HttpResponse<SSL>* res, std::shared_ptr<bool> aborted, const std::string& body) {
  try {
    auto j = json::parse(body);
    std::string credential_id = j.at("id");
    auto response = j.at("response");
    std::string attestation_object = response.at("attestationObject");
    std::string client_data_json = response.at("clientDataJSON");
    std::string device_name = j.value("device_name", "Passkey");

    std::string transports_str = "[]";
    if (response.contains("transports")) {
      transports_str = response["transports"].dump();
    }

    std::string challenge =
      auth_payload_utils::extract_challenge_from_client_data_b64(client_data_json);

    auto stored = db.get_webauthn_challenge(challenge);
    if (!stored) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Invalid or expired challenge"})");
      });
      return;
    }

    auto extra = auth_payload_utils::parse_challenge_extra(stored->extra_data);
    if (!auth_payload_utils::challenge_has_type(extra, "device_passkey")) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("400")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Challenge mismatch"})");
      });
      return;
    }

    std::string user_id = extra.at("user_id");
    std::string device_token = extra.at("device_token");

    // Validate device token is still valid
    auto token_user = db.validate_device_token(device_token);
    if (!token_user || *token_user != user_id) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Device token expired"})");
      });
      return;
    }

    auto result = webauthn::verify_registration(
      attestation_object,
      client_data_json,
      challenge,
      config.webauthn_origin,
      config.webauthn_rp_id);

    if (!result) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"WebAuthn verification failed"})");
      });
      return;
    }

    db.delete_webauthn_challenge(challenge);
    db.mark_device_token_used(device_token);
    db.store_webauthn_credential(
      user_id,
      result->credential_id,
      result->public_key,
      result->sign_count,
      device_name,
      transports_str);

    auto user = db.find_user_by_id(user_id);
    if (!user) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("404")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"User not found"})");
      });
      return;
    }

    std::string session = db.create_session(user->id, get_session_expiry());
    int max_age = get_session_expiry() * 3600;
    bool secure = config.has_ssl();
    json resp = auth_payload_utils::build_token_user_response(session, make_user_json(*user));
    auto resp_body = resp.dump();
    loop_->defer([res, aborted, resp_body = std::move(resp_body), session, max_age, secure]() {
      if (*aborted) return;
      emit_session_cookies(res, session, max_age, secure);
      res->writeHeader("Content-Type", "application/json")->end(resp_body);
    });
  } catch (const std::exception& e) {
    auto err_body = json({{"error", e.what()}}).dump();
    loop_->defer([res, aborted, err_body = std::move(err_body)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
    });
  }
}

// --- Recovery key handler ---

template <bool SSL>
void AuthHandler<SSL>::handle_recovery_login(
  uWS::HttpResponse<SSL>* res, std::shared_ptr<bool> aborted, const std::string& body) {
  try {
    auto j = json::parse(body);
    std::string recovery_key = j.at("recovery_key");

    std::string key_hash = webauthn::hash_recovery_key(recovery_key);
    auto user_id = db.verify_and_consume_recovery_key(key_hash);
    if (!user_id) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Invalid or already used recovery key"})");
      });
      return;
    }

    auto user = db.find_user_by_id(*user_id);
    if (!user) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("404")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"User not found"})");
      });
      return;
    }

    if (user->is_banned) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Your account has been banned"})");
      });
      return;
    }

    if (db.is_server_locked_down() && user->role != "admin" && user->role != "owner") {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Server is in lockdown mode. Only administrators may log in."})");
      });
      return;
    }

    std::string token = db.create_session(user->id, get_session_expiry());
    int max_age = get_session_expiry() * 3600;
    bool secure = config.has_ssl();
    json resp = auth_payload_utils::build_token_user_response(
      token, make_user_json(*user), json{{"must_setup_key", true}});
    auto resp_body = resp.dump();
    loop_->defer([res, aborted, resp_body = std::move(resp_body), token, max_age, secure]() {
      if (*aborted) return;
      emit_session_cookies(res, token, max_age, secure);
      res->writeHeader("Content-Type", "application/json")->end(resp_body);
    });
  } catch (const std::exception& e) {
    auto err_body = json({{"error", e.what()}}).dump();
    loop_->defer([res, aborted, err_body = std::move(err_body)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
    });
  }
}

// --- Recovery token handler ---

template <bool SSL>
void AuthHandler<SSL>::handle_recovery_token_login(
  uWS::HttpResponse<SSL>* res, std::shared_ptr<bool> aborted, const std::string& body) {
  try {
    auto j = json::parse(body);
    std::string token = j.at("token");

    auto user_id = db.get_recovery_token_user_id(token);
    if (!user_id) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Invalid or expired recovery token"})");
      });
      return;
    }

    auto user = db.find_user_by_id(*user_id);
    if (!user) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("404")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"User not found"})");
      });
      return;
    }

    if (user->is_banned) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Your account has been banned"})");
      });
      return;
    }

    if (db.is_server_locked_down() && user->role != "admin" && user->role != "owner") {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Server is in lockdown mode. Only administrators may log in."})");
      });
      return;
    }

    db.use_recovery_token(token);

    std::string session = db.create_session(user->id, get_session_expiry());
    int max_age = get_session_expiry() * 3600;
    bool secure = config.has_ssl();
    json resp = auth_payload_utils::build_token_user_response(
      session, make_user_json(*user), json{{"must_setup_key", true}});
    auto resp_body = resp.dump();
    loop_->defer([res, aborted, resp_body = std::move(resp_body), session, max_age, secure]() {
      if (*aborted) return;
      emit_session_cookies(res, session, max_age, secure);
      res->writeHeader("Content-Type", "application/json")->end(resp_body);
    });
  } catch (const std::exception& e) {
    auto err_body = json({{"error", e.what()}}).dump();
    loop_->defer([res, aborted, err_body = std::move(err_body)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
    });
  }
}

// --- Join request handlers ---

template <bool SSL>
void AuthHandler<SSL>::handle_request_access_options(
  uWS::HttpResponse<SSL>* res, std::shared_ptr<bool> aborted, const std::string& body) {
  try {
    auto mode = db.get_setting("registration_mode");
    std::string reg_mode = mode.value_or("invite");
    if (reg_mode == "open" || reg_mode == "invite_only") {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Requesting access is not available on this server"})");
      });
      return;
    }

    if (!is_method_enabled("passkey")) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Passkey authentication is not enabled on this server"})");
      });
      return;
    }

    auto j = json::parse(body);
    std::string username = j.at("username");
    std::string display_name = j.at("display_name");

    std::string challenge = webauthn::generate_challenge();
    std::string user_handle = generate_user_handle();

    json extra = auth_payload_utils::build_registration_challenge_extra(
      "join_request", username, display_name, user_handle);
    db.store_webauthn_challenge(challenge, extra.dump());

    json options = auth_payload_utils::build_passkey_registration_options(
      config, challenge, user_handle, username, display_name);

    auto resp_body = options.dump();
    loop_->defer([res, aborted, resp_body = std::move(resp_body)]() {
      if (*aborted) return;
      res->writeHeader("Content-Type", "application/json")->end(resp_body);
    });
  } catch (const std::exception& e) {
    auto err_body = json({{"error", e.what()}}).dump();
    loop_->defer([res, aborted, err_body = std::move(err_body)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
    });
  }
}

template <bool SSL>
void AuthHandler<SSL>::handle_request_access(
  uWS::HttpResponse<SSL>* res, std::shared_ptr<bool> aborted, const std::string& body) {
  try {
    auto mode = db.get_setting("registration_mode");
    std::string reg_mode = mode.value_or("invite");
    if (reg_mode == "open" || reg_mode == "invite_only") {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Requesting access is not available on this server"})");
      });
      return;
    }

    auto j = json::parse(body);
    std::string username = j.at("username");
    std::string display_name = j.at("display_name");
    std::string auth_method = j.at("auth_method");

    std::string credential_data;

    if (auth_method == "pki") {
      if (!is_method_enabled("pki")) {
        loop_->defer([res, aborted]() {
          if (*aborted) return;
          res->writeStatus("403")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Browser key authentication is not enabled"})");
        });
        return;
      }

      std::string public_key = j.at("public_key");
      std::string challenge = j.at("challenge");
      std::string signature = j.at("signature");

      // Verify the challenge
      auto stored = db.get_webauthn_challenge(challenge);
      if (!stored) {
        loop_->defer([res, aborted]() {
          if (*aborted) return;
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Invalid or expired challenge"})");
        });
        return;
      }
      db.delete_webauthn_challenge(challenge);

      // Verify signature proves key ownership
      if (!webauthn::verify_pki_signature(public_key, challenge, signature)) {
        loop_->defer([res, aborted]() {
          if (*aborted) return;
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Signature verification failed"})");
        });
        return;
      }

      credential_data = json({{"public_key", public_key}}).dump();

    } else if (auth_method == "passkey") {
      if (!is_method_enabled("passkey")) {
        loop_->defer([res, aborted]() {
          if (*aborted) return;
          res->writeStatus("403")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Passkey authentication is not enabled"})");
        });
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
      std::string challenge =
        auth_payload_utils::extract_challenge_from_client_data_b64(client_data_json);

      auto stored = db.get_webauthn_challenge(challenge);
      if (!stored) {
        loop_->defer([res, aborted]() {
          if (*aborted) return;
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Invalid or expired challenge"})");
        });
        return;
      }

      auto extra = auth_payload_utils::parse_challenge_extra(stored->extra_data);
      if (!auth_payload_utils::challenge_has_type(extra, "join_request")) {
        loop_->defer([res, aborted]() {
          if (*aborted) return;
          res->writeStatus("400")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Challenge is not for a join request"})");
        });
        return;
      }

      // Verify the attestation
      auto result = webauthn::verify_registration(
        attestation_object,
        client_data_json,
        challenge,
        config.webauthn_origin,
        config.webauthn_rp_id);

      if (!result) {
        loop_->defer([res, aborted]() {
          if (*aborted) return;
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"WebAuthn verification failed"})");
        });
        return;
      }

      db.delete_webauthn_challenge(challenge);

      // Store verified credential data for later account creation
      credential_data = json({{"credential_id", result->credential_id},
                              {"public_key", webauthn::base64url_encode(result->public_key)},
                              {"sign_count", result->sign_count},
                              {"transports", transports_str}})
                          .dump();

    } else if (auth_method == "password") {
      if (!is_method_enabled("password")) {
        loop_->defer([res, aborted]() {
          if (*aborted) return;
          res->writeStatus("403")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Password authentication is not enabled"})");
        });
        return;
      }

      std::string password = j.at("password");

      // Validate password complexity
      auto policy = get_password_policy();
      auto validation_error = password_auth::validate_password(password, policy);
      if (!validation_error.empty()) {
        auto err_body = json({{"error", validation_error}}).dump();
        loop_->defer([res, aborted, err_body = std::move(err_body)]() {
          if (*aborted) return;
          res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
        });
        return;
      }

      // Hash password and store in credential_data
      auto hash = password_auth::hash_password(password);
      credential_data = json({{"password_hash", hash}}).dump();

    } else {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("400")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Invalid auth_method. Use 'passkey', 'pki', or 'password'."})");
      });
      return;
    }

    auto id = db.create_join_request(username, display_name, "", auth_method, credential_data);

    // Notify admin/owner users via WebSocket
    json notify = {{"type", "join_request_created"}, {"request_id", id}};
    auto all_users = db.list_users();
    std::string notif_content = username + " requested to join";
    for (const auto& u : all_users) {
      if (u.role == "admin" || u.role == "owner") {
        ws.send_to_user(u.id, notify.dump());

        // Create bell notification for the join request
        auto nid = db.create_notification(u.id, "join_request", "", "", "", notif_content);
        json notif = {
          {"type", "new_notification"},
          {"notification",
           {{"id", nid},
            {"user_id", u.id},
            {"type", "join_request"},
            {"source_user_id", ""},
            {"source_username", username},
            {"channel_id", ""},
            {"channel_name", ""},
            {"message_id", ""},
            {"space_id", ""},
            {"content", notif_content},
            {"created_at", ""},
            {"is_read", false}}}};
        ws.send_to_user(u.id, notif.dump());
      }
    }

    json resp = {{"request_id", id}, {"status", "pending"}};
    auto resp_body = resp.dump();
    loop_->defer([res, aborted, resp_body = std::move(resp_body)]() {
      if (*aborted) return;
      res->writeHeader("Content-Type", "application/json")->end(resp_body);
    });
  } catch (const std::exception& e) {
    auto err_body = json({{"error", e.what()}}).dump();
    loop_->defer([res, aborted, err_body = std::move(err_body)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
    });
  }
}

// --- Password auth handlers ---

template <bool SSL>
password_auth::PasswordPolicy AuthHandler<SSL>::get_password_policy() {
  return auth_utils::build_password_policy(
    [this](const std::string& key) { return db.get_setting(key); });
}

template <bool SSL>
void AuthHandler<SSL>::handle_password_register(
  uWS::HttpResponse<SSL>* res, std::shared_ptr<bool> aborted, const std::string& body) {
  try {
    if (!is_method_enabled("password")) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Password authentication is not enabled on this server"})");
      });
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
      auto err_body = json({{"error", validation_error}}).dump();
      loop_->defer([res, aborted, err_body = std::move(err_body)]() {
        if (*aborted) return;
        res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
      });
      return;
    }

    // Check registration eligibility
    auto eligibility_error = check_registration_eligibility(username, invite_token);
    if (!eligibility_error.empty()) {
      auto err_body = json({{"error", eligibility_error}}).dump();
      loop_->defer([res, aborted, err_body = std::move(err_body)]() {
        if (*aborted) return;
        res->writeStatus("403")->writeHeader("Content-Type", "application/json")->end(err_body);
      });
      return;
    }

    // Create user
    bool is_first_user = (db.count_users() == 0);
    std::string role = is_first_user ? "owner" : "user";
    auto user = db.create_user(username, display_name, "", role);

    // Hash and store password
    auto hash = password_auth::hash_password(password);
    db.store_password(user.id, hash);

    // Use invite token if provided
    complete_user_creation(user, invite_token);

    // Create session
    auto token = db.create_session(user.id, get_session_expiry());
    int max_age = get_session_expiry() * 3600;
    bool secure = config.has_ssl();

    json resp = auth_payload_utils::build_token_user_response(token, make_user_json(user));
    auto resp_body = resp.dump();
    loop_->defer([res, aborted, resp_body = std::move(resp_body), token, max_age, secure]() {
      if (*aborted) return;
      emit_session_cookies(res, token, max_age, secure);
      res->writeHeader("Content-Type", "application/json")->end(resp_body);
    });
  } catch (const pqxx::unique_violation&) {
    loop_->defer([res, aborted]() {
      if (*aborted) return;
      res->writeStatus("409")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Username already taken"})");
    });
  } catch (const std::exception& e) {
    auto err_body = json({{"error", e.what()}}).dump();
    loop_->defer([res, aborted, err_body = std::move(err_body)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
    });
  }
}

template <bool SSL>
void AuthHandler<SSL>::handle_password_login(
  uWS::HttpResponse<SSL>* res, std::shared_ptr<bool> aborted, const std::string& body) {
  try {
    if (!is_method_enabled("password")) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Password authentication is not enabled on this server"})");
      });
      return;
    }

    auto j = json::parse(body);
    std::string username = j.at("username");
    std::string password = j.at("password");

    // Find user by username
    auto user = db.find_user_by_username(username);
    if (!user) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Invalid username or password"})");
      });
      return;
    }

    // Get stored password hash
    auto stored_hash = db.get_password_hash(user->id);
    if (!stored_hash) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Invalid username or password"})");
      });
      return;
    }

    // Verify password
    if (!password_auth::verify_password(password, *stored_hash)) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Invalid username or password"})");
      });
      return;
    }

    if (user->is_banned) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Your account has been banned"})");
      });
      return;
    }

    if (db.is_server_locked_down() && user->role != "admin" && user->role != "owner") {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Server is in lockdown mode. Only administrators may log in."})");
      });
      return;
    }

    if (check_and_handle_mfa(res, aborted, *user, "password")) return;

    // Create session
    auto token = db.create_session(user->id, get_session_expiry());
    int max_age = get_session_expiry() * 3600;
    bool secure = config.has_ssl();

    // Check password expiry
    auto policy = get_password_policy();
    json resp = auth_payload_utils::build_token_user_response(
      token,
      make_user_json(*user),
      policy.max_age_days > 0
        ? json{{"must_change_password", db.is_password_expired(user->id, policy.max_age_days)}}
        : json::object());
    auto resp_body = resp.dump();
    loop_->defer([res, aborted, resp_body = std::move(resp_body), token, max_age, secure]() {
      if (*aborted) return;
      emit_session_cookies(res, token, max_age, secure);
      res->writeHeader("Content-Type", "application/json")->end(resp_body);
    });
  } catch (const std::exception& e) {
    auto err_body = json({{"error", e.what()}}).dump();
    loop_->defer([res, aborted, err_body = std::move(err_body)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
    });
  }
}

template <bool SSL>
void AuthHandler<SSL>::handle_password_change(
  uWS::HttpResponse<SSL>* res,
  std::shared_ptr<bool> aborted,
  const std::string& body,
  const std::string& session_token) {
  try {
    auto user_id = db.validate_session(session_token);
    if (!user_id) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Unauthorized"})");
      });
      return;
    }

    auto j = json::parse(body);
    std::string current_password = j.at("current_password");
    std::string new_password = j.at("new_password");

    // Verify current password
    auto stored_hash = db.get_password_hash(*user_id);
    if (!stored_hash || !password_auth::verify_password(current_password, *stored_hash)) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Current password is incorrect"})");
      });
      return;
    }

    // Validate new password complexity
    auto policy = get_password_policy();
    auto validation_error = password_auth::validate_password(new_password, policy);
    if (!validation_error.empty()) {
      auto err_body = json({{"error", validation_error}}).dump();
      loop_->defer([res, aborted, err_body = std::move(err_body)]() {
        if (*aborted) return;
        res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
      });
      return;
    }

    // Check password history
    if (policy.history_count > 0) {
      auto history = db.get_password_history(*user_id, policy.history_count);
      // Also include the current password in history check
      history.push_back(*stored_hash);
      if (password_auth::matches_history(new_password, history)) {
        loop_->defer([res, aborted]() {
          if (*aborted) return;
          res->writeStatus("400")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Cannot reuse a recent password"})");
        });
        return;
      }
    }

    // Save old password to history
    db.add_password_history(*user_id, *stored_hash);

    // Hash and store new password
    auto new_hash = password_auth::hash_password(new_password);
    db.store_password(*user_id, new_hash);

    loop_->defer([res, aborted]() {
      if (*aborted) return;
      res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
    });
  } catch (const std::exception& e) {
    auto err_body = json({{"error", e.what()}}).dump();
    loop_->defer([res, aborted, err_body = std::move(err_body)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
    });
  }
}

template <bool SSL>
void AuthHandler<SSL>::handle_password_set(
  uWS::HttpResponse<SSL>* res,
  std::shared_ptr<bool> aborted,
  const std::string& body,
  const std::string& session_token) {
  try {
    auto user_id = db.validate_session(session_token);
    if (!user_id) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Unauthorized"})");
      });
      return;
    }

    // Check that password auth is enabled
    auto methods_str = db.get_setting("auth_methods").value_or("");
    if (methods_str.find("password") == std::string::npos) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Password authentication is not enabled"})");
      });
      return;
    }

    // User must NOT already have a password
    if (db.has_password(*user_id)) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("400")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Password already set. Use the change password endpoint instead."})");
      });
      return;
    }

    auto j = json::parse(body);
    std::string new_password = j.at("password");

    // Validate password complexity
    auto policy = get_password_policy();
    auto validation_error = password_auth::validate_password(new_password, policy);
    if (!validation_error.empty()) {
      auto err_body = json({{"error", validation_error}}).dump();
      loop_->defer([res, aborted, err_body = std::move(err_body)]() {
        if (*aborted) return;
        res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
      });
      return;
    }

    // Hash and store
    auto hash = password_auth::hash_password(new_password);
    db.store_password(*user_id, hash);

    loop_->defer([res, aborted]() {
      if (*aborted) return;
      res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
    });
  } catch (const std::exception& e) {
    auto err_body = json({{"error", e.what()}}).dump();
    loop_->defer([res, aborted, err_body = std::move(err_body)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
    });
  }
}

template <bool SSL>
void AuthHandler<SSL>::handle_password_delete(
  uWS::HttpResponse<SSL>* res, std::shared_ptr<bool> aborted, const std::string& session_token) {
  try {
    auto user_id = db.validate_session(session_token);
    if (!user_id) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Unauthorized"})");
      });
      return;
    }

    if (!db.has_password(*user_id)) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("400")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"No password is set"})");
      });
      return;
    }

    // Ensure the user has at least one other auth credential
    int other_creds = db.count_user_credentials(*user_id);
    if (other_creds < 1) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("400")
          ->writeHeader("Content-Type", "application/json")
          ->end(
            R"({"error":"Cannot remove password — no other login method configured. Add a passkey or browser key first."})");
      });
      return;
    }

    db.delete_password(*user_id);
    loop_->defer([res, aborted]() {
      if (*aborted) return;
      res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
    });
  } catch (const std::exception& e) {
    auto err_body = json({{"error", e.what()}}).dump();
    loop_->defer([res, aborted, err_body = std::move(err_body)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
    });
  }
}

template <bool SSL>
void AuthHandler<SSL>::handle_mfa_verify(
  uWS::HttpResponse<SSL>* res, std::shared_ptr<bool> aborted, const std::string& body) {
  try {
    auto j = json::parse(body);
    std::string mfa_token = j.at("mfa_token");
    std::string totp_code = j.at("totp_code");

    auto pending = db.validate_mfa_pending_token(mfa_token);
    if (!pending) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Invalid or expired MFA token"})");
      });
      return;
    }

    auto [user_id, auth_method] = *pending;

    auto secret = db.get_totp_secret(user_id);
    if (!secret) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("400")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"TOTP is not set up"})");
      });
      return;
    }

    auto last_step_signed = db.get_totp_last_step(user_id);
    std::optional<uint64_t> last_step;
    if (last_step_signed.has_value()) {
      last_step = static_cast<uint64_t>(*last_step_signed);
    }
    auto matched_step = totp::verify_code(*secret, totp_code, last_step);
    if (!matched_step) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Invalid verification code"})");
      });
      return;
    }
    db.set_totp_last_step(user_id, static_cast<int64_t>(*matched_step));

    // MFA verified -- consume the token and create a real session
    db.delete_mfa_pending_token(mfa_token);

    auto user = db.find_user_by_id(user_id);
    if (!user) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("404")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"User not found"})");
      });
      return;
    }

    if (user->is_banned) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Your account has been banned"})");
      });
      return;
    }

    if (db.is_server_locked_down() && user->role != "admin" && user->role != "owner") {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Server is in lockdown mode. Only administrators may log in."})");
      });
      return;
    }

    auto token = db.create_session(user->id, get_session_expiry());
    int max_age = get_session_expiry() * 3600;
    bool secure = config.has_ssl();
    json extra = json::object();
    if (auth_method == "password") {
      auto policy = get_password_policy();
      if (policy.max_age_days > 0) {
        extra["must_change_password"] = db.is_password_expired(user->id, policy.max_age_days);
      }
    }

    json resp = auth_payload_utils::build_token_user_response(token, make_user_json(*user), extra);

    auto resp_body = resp.dump();
    loop_->defer([res, aborted, resp_body = std::move(resp_body), token, max_age, secure]() {
      if (*aborted) return;
      emit_session_cookies(res, token, max_age, secure);
      res->writeHeader("Content-Type", "application/json")->end(resp_body);
    });
  } catch (const std::exception& e) {
    auto err_body = json({{"error", e.what()}}).dump();
    loop_->defer([res, aborted, err_body = std::move(err_body)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
    });
  }
}

template <bool SSL>
void AuthHandler<SSL>::handle_mfa_setup(
  uWS::HttpResponse<SSL>* res, std::shared_ptr<bool> aborted, const std::string& body) {
  try {
    auto j = json::parse(body);
    std::string mfa_token = j.at("mfa_token");

    auto pending = db.validate_mfa_pending_token(mfa_token);
    if (!pending) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Invalid or expired MFA token"})");
      });
      return;
    }

    auto [user_id, auth_method] = *pending;

    auto user = db.find_user_by_id(user_id);
    if (!user) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("404")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"User not found"})");
      });
      return;
    }

    auto secret = totp::generate_secret();
    db.store_totp_secret(user_id, secret);

    auto server_name = db.get_setting("server_name").value_or("EnclaveStation");
    auto uri = totp::build_uri(secret, user->username, server_name);

    json resp = auth_payload_utils::build_totp_setup_response(secret, uri);
    auto resp_body = resp.dump();
    loop_->defer([res, aborted, resp_body = std::move(resp_body)]() {
      if (*aborted) return;
      res->writeHeader("Content-Type", "application/json")->end(resp_body);
    });
  } catch (const std::exception& e) {
    auto err_body = json({{"error", e.what()}}).dump();
    loop_->defer([res, aborted, err_body = std::move(err_body)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
    });
  }
}

template <bool SSL>
void AuthHandler<SSL>::handle_mfa_setup_verify(
  uWS::HttpResponse<SSL>* res, std::shared_ptr<bool> aborted, const std::string& body) {
  try {
    auto j = json::parse(body);
    std::string mfa_token = j.at("mfa_token");
    std::string code = j.at("code");

    auto pending = db.validate_mfa_pending_token(mfa_token);
    if (!pending) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Invalid or expired MFA token"})");
      });
      return;
    }

    auto [user_id, auth_method] = *pending;

    auto secret = db.get_unverified_totp_secret(user_id);
    if (!secret) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("400")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"No TOTP setup in progress"})");
      });
      return;
    }

    auto last_step_signed = db.get_totp_last_step(user_id);
    std::optional<uint64_t> last_step;
    if (last_step_signed.has_value()) {
      last_step = static_cast<uint64_t>(*last_step_signed);
    }
    auto matched_step = totp::verify_code(*secret, code, last_step);
    if (!matched_step) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("401")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Invalid verification code"})");
      });
      return;
    }

    // TOTP verified -- mark as verified, consume MFA token, create session
    db.verify_totp(user_id);
    db.set_totp_last_step(user_id, static_cast<int64_t>(*matched_step));
    db.delete_mfa_pending_token(mfa_token);

    auto user = db.find_user_by_id(user_id);
    if (!user) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("404")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"User not found"})");
      });
      return;
    }

    if (user->is_banned) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Your account has been banned"})");
      });
      return;
    }

    if (db.is_server_locked_down() && user->role != "admin" && user->role != "owner") {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Server is in lockdown mode. Only administrators may log in."})");
      });
      return;
    }

    auto token = db.create_session(user->id, get_session_expiry());
    int max_age = get_session_expiry() * 3600;
    bool secure = config.has_ssl();
    json extra = json::object();
    if (auth_method == "password") {
      auto policy = get_password_policy();
      if (policy.max_age_days > 0) {
        extra["must_change_password"] = db.is_password_expired(user->id, policy.max_age_days);
      }
    }

    json resp = auth_payload_utils::build_token_user_response(token, make_user_json(*user), extra);

    auto resp_body = resp.dump();
    loop_->defer([res, aborted, resp_body = std::move(resp_body), token, max_age, secure]() {
      if (*aborted) return;
      emit_session_cookies(res, token, max_age, secure);
      res->writeHeader("Content-Type", "application/json")->end(resp_body);
    });
  } catch (const std::exception& e) {
    auto err_body = json({{"error", e.what()}}).dump();
    loop_->defer([res, aborted, err_body = std::move(err_body)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
    });
  }
}

// Poll for join request status
template <bool SSL>
void AuthHandler<SSL>::handle_request_status(
  uWS::HttpResponse<SSL>* res, std::shared_ptr<bool> aborted, const std::string& request_id) {
  try {
    auto request = db.get_join_request(request_id);
    if (!request) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("404")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Request not found"})");
      });
      return;
    }

    json resp = auth_payload_utils::build_join_request_status_response(request->status);

    if (request->status == "approved" && !request->session_token.empty()) {
      // Validate the session is still active
      auto user_id = db.validate_session(request->session_token);
      if (user_id) {
        auto user = db.find_user_by_id(*user_id);
        if (user) {
          resp = auth_payload_utils::build_join_request_status_response(
            request->status, request->session_token, make_user_json(*user));
        }
      }
    }

    auto resp_body = resp.dump();
    loop_->defer([res, aborted, resp_body = std::move(resp_body)]() {
      if (*aborted) return;
      res->writeHeader("Content-Type", "application/json")->end(resp_body);
    });
  } catch (const std::exception& e) {
    auto err_body = json({{"error", e.what()}}).dump();
    loop_->defer([res, aborted, err_body = std::move(err_body)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err_body);
    });
  }
}

template struct AuthHandler<false>;
template struct AuthHandler<true>;
