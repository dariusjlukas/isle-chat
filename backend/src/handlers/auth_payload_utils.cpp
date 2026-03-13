#include "handlers/auth_payload_utils.h"

#include "auth/webauthn.h"

namespace auth_payload_utils {

std::string extract_challenge_from_client_data_b64(const std::string& client_data_json_b64) {
    auto client_data_bytes = webauthn::base64url_decode(client_data_json_b64);
    std::string client_data_str(client_data_bytes.begin(), client_data_bytes.end());
    return json::parse(client_data_str).at("challenge");
}

json parse_challenge_extra(const std::string& extra_data_json) {
    return json::parse(extra_data_json);
}

bool challenge_has_type(const json& extra, const std::string& expected_type) {
    return extra.at("type") == expected_type;
}

json build_registration_challenge_extra(const std::string& type,
                                        const std::string& username,
                                        const std::string& display_name,
                                        const std::string& user_handle,
                                        const std::optional<std::string>& invite_token) {
    json extra = {
        {"type", type},
        {"username", username},
        {"display_name", display_name},
        {"user_handle", user_handle}
    };

    if (invite_token && !invite_token->empty()) {
        extra["invite_token"] = *invite_token;
    }

    return extra;
}

json build_authentication_challenge_extra() {
    return json{{"type", "authentication"}};
}

json build_pki_challenge_extra(const std::string& type,
                               const std::optional<std::string>& public_key) {
    json extra = {{"type", type}};
    if (public_key && !public_key->empty()) {
        extra["public_key"] = *public_key;
    }
    return extra;
}

json build_device_passkey_challenge_extra(const std::string& user_id,
                                          const std::string& device_token) {
    return {
        {"type", "device_passkey"},
        {"user_id", user_id},
        {"device_token", device_token}
    };
}

json build_passkey_registration_options(const Config& config,
                                        const std::string& challenge,
                                        const std::string& user_handle,
                                        const std::string& username,
                                        const std::string& display_name,
                                        const json& exclude_credentials) {
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

    if (!exclude_credentials.empty()) {
        options["excludeCredentials"] = exclude_credentials;
    }

    return options;
}

json build_passkey_login_options(const Config& config, const std::string& challenge) {
    return {
        {"challenge", challenge},
        {"rpId", config.webauthn_rp_id},
        {"allowCredentials", json::array()},
        {"userVerification", "preferred"},
        {"timeout", defaults::WEBAUTHN_TIMEOUT_MS}
    };
}

json build_challenge_response(const std::string& challenge) {
    return json{{"challenge", challenge}};
}

json build_exclude_credentials(const std::vector<Database::WebAuthnCredential>& credentials) {
    json exclude = json::array();
    for (const auto& credential : credentials) {
        json descriptor = {
            {"type", "public-key"},
            {"id", credential.credential_id}
        };
        if (!credential.transports.empty() && credential.transports != "[]") {
            descriptor["transports"] = json::parse(credential.transports);
        }
        exclude.push_back(descriptor);
    }
    return exclude;
}

json build_token_user_response(const std::string& token,
                               const json& user_json,
                               const json& extra) {
    json response = {
        {"token", token},
        {"user", user_json}
    };

    if (extra.is_object()) {
        for (auto it = extra.begin(); it != extra.end(); ++it) {
            response[it.key()] = it.value();
        }
    }

    return response;
}

json build_totp_setup_response(const std::string& secret, const std::string& uri) {
    return {
        {"secret", secret},
        {"uri", uri}
    };
}

json build_join_request_status_response(const std::string& status,
                                        const std::optional<std::string>& token,
                                        const std::optional<json>& user) {
    json response = {{"status", status}};

    if (token && !token->empty()) {
        response["token"] = *token;
    }
    if (user) {
        response["user"] = *user;
    }

    return response;
}

}  // namespace auth_payload_utils
