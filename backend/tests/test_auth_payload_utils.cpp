#include <gtest/gtest.h>

#include "auth/webauthn.h"
#include "handlers/auth_payload_utils.h"

using json = nlohmann::json;

namespace {

Config make_config() {
    Config config;
    config.webauthn_rp_name = "EnclaveStation";
    config.webauthn_rp_id = "chat.example";
    config.webauthn_origin = "https://chat.example";
    return config;
}

std::string encode_json(const json& value) {
    std::string dumped = value.dump();
    return webauthn::base64url_encode(
        reinterpret_cast<const unsigned char*>(dumped.data()),
        dumped.size());
}

}  // namespace

TEST(AuthPayloadUtils, ExtractsChallengeFromClientData) {
    json client_data = {
        {"type", "webauthn.get"},
        {"challenge", "server-challenge"},
        {"origin", "https://chat.example"}
    };

    EXPECT_EQ(auth_payload_utils::extract_challenge_from_client_data_b64(encode_json(client_data)),
              "server-challenge");
}

TEST(AuthPayloadUtils, ParsesAndChecksChallengeExtras) {
    auto registration_extra = auth_payload_utils::build_registration_challenge_extra(
        "registration", "alice", "Alice", "user-handle", "invite-123");
    auto parsed = auth_payload_utils::parse_challenge_extra(registration_extra.dump());

    EXPECT_TRUE(auth_payload_utils::challenge_has_type(parsed, "registration"));
    EXPECT_FALSE(auth_payload_utils::challenge_has_type(parsed, "authentication"));
    EXPECT_EQ(parsed.at("username"), "alice");
    EXPECT_EQ(parsed.at("display_name"), "Alice");
    EXPECT_EQ(parsed.at("user_handle"), "user-handle");
    EXPECT_EQ(parsed.at("invite_token"), "invite-123");

    auto join_request_extra = auth_payload_utils::build_registration_challenge_extra(
        "join_request", "bob", "Bob", "user-handle-2");
    EXPECT_FALSE(join_request_extra.contains("invite_token"));
}

TEST(AuthPayloadUtils, BuildsPkiAndDeviceChallengeExtras) {
    auto pki_login = auth_payload_utils::build_pki_challenge_extra("pki_login", "public-key");
    EXPECT_EQ(pki_login.at("type"), "pki_login");
    EXPECT_EQ(pki_login.at("public_key"), "public-key");

    auto pki_registration = auth_payload_utils::build_pki_challenge_extra("pki_registration");
    EXPECT_EQ(pki_registration.at("type"), "pki_registration");
    EXPECT_FALSE(pki_registration.contains("public_key"));

    auto device_passkey = auth_payload_utils::build_device_passkey_challenge_extra(
        "user-1", "device-token-1");
    EXPECT_EQ(device_passkey.at("type"), "device_passkey");
    EXPECT_EQ(device_passkey.at("user_id"), "user-1");
    EXPECT_EQ(device_passkey.at("device_token"), "device-token-1");

    auto auth = auth_payload_utils::build_authentication_challenge_extra();
    EXPECT_EQ(auth.at("type"), "authentication");

    auto challenge_response = auth_payload_utils::build_challenge_response("challenge-1");
    EXPECT_EQ(challenge_response.at("challenge"), "challenge-1");
}

TEST(AuthPayloadUtils, BuildsPasskeyOptionPayloads) {
    Config config = make_config();

    auto registration = auth_payload_utils::build_passkey_registration_options(
        config, "challenge-1", "handle-1", "alice", "Alice");
    EXPECT_EQ(registration.at("rp").at("name"), "EnclaveStation");
    EXPECT_EQ(registration.at("rp").at("id"), "chat.example");
    EXPECT_EQ(registration.at("user").at("id"), "handle-1");
    EXPECT_EQ(registration.at("user").at("name"), "alice");
    EXPECT_EQ(registration.at("user").at("displayName"), "Alice");
    EXPECT_EQ(registration.at("challenge"), "challenge-1");
    EXPECT_FALSE(registration.contains("excludeCredentials"));
    ASSERT_EQ(registration.at("pubKeyCredParams").size(), 2u);
    EXPECT_EQ(registration.at("authenticatorSelection").at("residentKey"), "required");
    EXPECT_EQ(registration.at("timeout"), defaults::WEBAUTHN_TIMEOUT_MS);

    json exclude = json::array({json{{"type", "public-key"}, {"id", "cred-1"}}});
    auto with_exclude = auth_payload_utils::build_passkey_registration_options(
        config, "challenge-2", "handle-2", "bob", "Bob", exclude);
    EXPECT_EQ(with_exclude.at("excludeCredentials"), exclude);

    auto login = auth_payload_utils::build_passkey_login_options(config, "challenge-3");
    EXPECT_EQ(login.at("challenge"), "challenge-3");
    EXPECT_EQ(login.at("rpId"), "chat.example");
    EXPECT_TRUE(login.at("allowCredentials").empty());
    EXPECT_EQ(login.at("userVerification"), "preferred");
}

TEST(AuthPayloadUtils, BuildsExcludeCredentialsFromStoredCredentials) {
    Database::WebAuthnCredential first;
    first.credential_id = "cred-1";
    first.transports = R"(["usb","internal"])";

    Database::WebAuthnCredential second;
    second.credential_id = "cred-2";
    second.transports = "[]";

    auto exclude = auth_payload_utils::build_exclude_credentials({first, second});
    ASSERT_EQ(exclude.size(), 2u);
    EXPECT_EQ(exclude.at(0).at("id"), "cred-1");
    EXPECT_EQ(exclude.at(0).at("transports"), json::array({"usb", "internal"}));
    EXPECT_EQ(exclude.at(1).at("id"), "cred-2");
    EXPECT_FALSE(exclude.at(1).contains("transports"));
}

TEST(AuthPayloadUtils, BuildsAuthAndStatusResponses) {
    json user = {{"id", "user-1"}, {"username", "alice"}};

    // P1.4 Release C: token is no longer included in the response body.
    // Auth is via Set-Cookie (set on the response object, not the JSON).
    auto token_response = auth_payload_utils::build_token_user_response(
        "session-1", user, json{{"must_change_password", true}, {"must_setup_key", true}});
    EXPECT_FALSE(token_response.contains("token"));
    EXPECT_EQ(token_response.at("user"), user);
    EXPECT_TRUE(token_response.at("must_change_password").get<bool>());
    EXPECT_TRUE(token_response.at("must_setup_key").get<bool>());

    auto basic_token_response = auth_payload_utils::build_token_user_response(
        "session-plain", user, json::array());
    EXPECT_FALSE(basic_token_response.contains("token"));
    EXPECT_EQ(basic_token_response.at("user"), user);
    EXPECT_EQ(basic_token_response.size(), 1u);

    auto totp = auth_payload_utils::build_totp_setup_response("secret-1", "otpauth://totp/test");
    EXPECT_EQ(totp.at("secret"), "secret-1");
    EXPECT_EQ(totp.at("uri"), "otpauth://totp/test");

    auto pending = auth_payload_utils::build_join_request_status_response("pending");
    EXPECT_EQ(pending.at("status"), "pending");
    EXPECT_FALSE(pending.contains("token"));
    EXPECT_FALSE(pending.contains("user"));

    auto approved = auth_payload_utils::build_join_request_status_response(
        "approved", "session-2", user);
    EXPECT_EQ(approved.at("status"), "approved");
    EXPECT_FALSE(approved.contains("token"));
    EXPECT_EQ(approved.at("user"), user);
}
