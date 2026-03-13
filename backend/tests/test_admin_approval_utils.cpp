#include <gtest/gtest.h>
#include "handlers/admin_approval_utils.h"

TEST(AdminApprovalUtils, ParsesPkiCredentialAndRecoveryHashes) {
    auto parsed = admin_approval_utils::parse_credential_data(
        "pki",
        R"({"public_key":"pem-key"})",
        []() {
            return std::make_pair(std::vector<std::string>{"plain-1"},
                                  std::vector<std::string>{"hash-1", "hash-2"});
        });

    ASSERT_TRUE(parsed.pki_public_key.has_value());
    EXPECT_EQ(*parsed.pki_public_key, "pem-key");
    EXPECT_EQ(parsed.recovery_key_hashes.size(), 2u);
    EXPECT_EQ(parsed.recovery_key_hashes[0], "hash-1");
}

TEST(AdminApprovalUtils, ParsesPasskeyCredential) {
    auto parsed = admin_approval_utils::parse_credential_data(
        "passkey",
        R"({"credential_id":"cred-1","public_key":"AQID","sign_count":7,"transports":"[\"internal\"]"})",
        []() { return std::make_pair(std::vector<std::string>{}, std::vector<std::string>{}); });

    ASSERT_TRUE(parsed.passkey_credential_id.has_value());
    EXPECT_EQ(*parsed.passkey_credential_id, "cred-1");
    ASSERT_EQ(parsed.passkey_public_key.size(), 3u);
    EXPECT_EQ(parsed.passkey_public_key[0], 1);
    EXPECT_EQ(parsed.passkey_sign_count, 7u);
    EXPECT_EQ(parsed.passkey_transports, "[\"internal\"]");
}

TEST(AdminApprovalUtils, ParsesPasswordCredential) {
    auto parsed = admin_approval_utils::parse_credential_data(
        "password",
        R"({"password_hash":"argon-hash"})",
        []() { return std::make_pair(std::vector<std::string>{}, std::vector<std::string>{}); });

    ASSERT_TRUE(parsed.password_hash.has_value());
    EXPECT_EQ(*parsed.password_hash, "argon-hash");
}

TEST(AdminApprovalUtils, ReturnsEmptyForUnknownMethodOrMissingPayload) {
    auto empty = admin_approval_utils::parse_credential_data(
        "unknown", "", []() { return std::make_pair(std::vector<std::string>{}, std::vector<std::string>{}); });
    EXPECT_FALSE(empty.pki_public_key.has_value());
    EXPECT_FALSE(empty.passkey_credential_id.has_value());
    EXPECT_FALSE(empty.password_hash.has_value());
}

TEST(AdminApprovalUtils, ThrowsForMalformedPayload) {
    EXPECT_THROW(
        admin_approval_utils::parse_credential_data(
            "passkey", "not-json",
            []() { return std::make_pair(std::vector<std::string>{}, std::vector<std::string>{}); }),
        std::exception);
}
