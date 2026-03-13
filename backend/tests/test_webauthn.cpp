#include <gtest/gtest.h>
#include "auth/webauthn.h"
#include <set>
#include <regex>
#include <openssl/sha.h>

// --- Base64url Encode/Decode Tests ---

TEST(Base64url, RoundTrip) {
    std::string original = "Hello, World!";
    std::vector<unsigned char> data(original.begin(), original.end());
    std::string encoded = webauthn::base64url_encode(data);
    auto decoded = webauthn::base64url_decode(encoded);
    std::string result(decoded.begin(), decoded.end());
    EXPECT_EQ(result, original);
}

TEST(Base64url, EmptyInput) {
    auto decoded = webauthn::base64url_decode("");
    EXPECT_TRUE(decoded.empty());

    std::string encoded = webauthn::base64url_encode(nullptr, 0);
    EXPECT_TRUE(encoded.empty());
}

TEST(Base64url, NoPadding) {
    // base64url should NOT include padding characters
    std::vector<unsigned char> data = {'f'};  // standard base64: "Zg=="
    std::string encoded = webauthn::base64url_encode(data);
    EXPECT_EQ(encoded.find('='), std::string::npos);
}

TEST(Base64url, UrlSafeCharacters) {
    // base64url uses - instead of + and _ instead of /
    // Data that would produce + and / in standard base64
    std::vector<unsigned char> data = {0xfb, 0xff, 0xfe};
    std::string encoded = webauthn::base64url_encode(data);
    EXPECT_EQ(encoded.find('+'), std::string::npos);
    EXPECT_EQ(encoded.find('/'), std::string::npos);
}

TEST(Base64url, DecodesStandardBase64) {
    // base64url_decode should also handle standard base64 with + and /
    auto from_url = webauthn::base64url_decode("u_--");  // url-safe
    auto from_std = webauthn::base64url_decode("u/++");  // standard
    EXPECT_EQ(from_url, from_std);
}

TEST(Base64url, DecodesPaddedInput) {
    // Should handle input with padding
    auto with_pad = webauthn::base64url_decode("Zg==");
    auto without_pad = webauthn::base64url_decode("Zg");
    EXPECT_EQ(with_pad, without_pad);
    EXPECT_EQ(with_pad.size(), 1u);
    EXPECT_EQ(with_pad[0], 'f');
}

TEST(Base64url, KnownVectors) {
    // Test with known base64url values
    struct TestVector { std::string decoded; std::string encoded; };
    std::vector<TestVector> vectors = {
        {"",       ""},
        {"f",      "Zg"},
        {"fo",     "Zm8"},
        {"foo",    "Zm9v"},
        {"foob",   "Zm9vYg"},
        {"fooba",  "Zm9vYmE"},
        {"foobar", "Zm9vYmFy"},
    };

    for (const auto& v : vectors) {
        std::vector<unsigned char> data(v.decoded.begin(), v.decoded.end());
        std::string encoded = webauthn::base64url_encode(data);
        EXPECT_EQ(encoded, v.encoded) << "Failed encoding: " << v.decoded;

        auto decoded = webauthn::base64url_decode(v.encoded);
        std::string result(decoded.begin(), decoded.end());
        EXPECT_EQ(result, v.decoded) << "Failed decoding: " << v.encoded;
    }
}

TEST(Base64url, BinaryData) {
    // Round-trip binary data with all byte values
    std::vector<unsigned char> data;
    for (int i = 0; i < 256; i++) {
        data.push_back(static_cast<unsigned char>(i));
    }
    std::string encoded = webauthn::base64url_encode(data);
    auto decoded = webauthn::base64url_decode(encoded);
    EXPECT_EQ(decoded, data);
}

TEST(Base64url, VectorOverload) {
    std::vector<unsigned char> data = {0x48, 0x65, 0x6c, 0x6c, 0x6f}; // "Hello"
    std::string from_vec = webauthn::base64url_encode(data);
    std::string from_ptr = webauthn::base64url_encode(data.data(), data.size());
    EXPECT_EQ(from_vec, from_ptr);
}

// --- Challenge Generation Tests ---

TEST(WebAuthnChallenge, NonEmpty) {
    std::string challenge = webauthn::generate_challenge();
    EXPECT_FALSE(challenge.empty());
}

TEST(WebAuthnChallenge, IsValidBase64url) {
    std::string challenge = webauthn::generate_challenge();
    // base64url chars: A-Z, a-z, 0-9, -, _
    EXPECT_TRUE(std::regex_match(challenge, std::regex("^[A-Za-z0-9_-]+$")));
}

TEST(WebAuthnChallenge, DecodesTo32Bytes) {
    std::string challenge = webauthn::generate_challenge();
    auto decoded = webauthn::base64url_decode(challenge);
    EXPECT_EQ(decoded.size(), 32u);
}

TEST(WebAuthnChallenge, Unique) {
    std::set<std::string> challenges;
    for (int i = 0; i < 100; i++) {
        challenges.insert(webauthn::generate_challenge());
    }
    EXPECT_EQ(challenges.size(), 100u);
}

// --- Recovery Key Hash Tests ---

TEST(RecoveryKeyHash, KnownValue) {
    // SHA-256 of empty string
    std::string hash = webauthn::hash_recovery_key("");
    EXPECT_EQ(hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(RecoveryKeyHash, DeterministicOutput) {
    std::string hash1 = webauthn::hash_recovery_key("test-key");
    std::string hash2 = webauthn::hash_recovery_key("test-key");
    EXPECT_EQ(hash1, hash2);
}

TEST(RecoveryKeyHash, DifferentInputsDifferentHashes) {
    std::string hash1 = webauthn::hash_recovery_key("key-one");
    std::string hash2 = webauthn::hash_recovery_key("key-two");
    EXPECT_NE(hash1, hash2);
}

TEST(RecoveryKeyHash, ProducesHex64Chars) {
    std::string hash = webauthn::hash_recovery_key("some-recovery-key");
    EXPECT_EQ(hash.size(), 64u);
    EXPECT_TRUE(std::regex_match(hash, std::regex("^[0-9a-f]{64}$")));
}

TEST(RecoveryKeyHash, MatchesOpenSSL) {
    // Verify against known SHA-256: SHA-256("hello") = "2cf24dba..."
    std::string hash = webauthn::hash_recovery_key("hello");
    EXPECT_EQ(hash, "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
}

// --- Recovery Key Generation Tests ---

TEST(RecoveryKeyGen, GeneratesEightKeys) {
    auto [keys, hashes] = webauthn::generate_recovery_keys();
    EXPECT_EQ(keys.size(), 8u);
    EXPECT_EQ(hashes.size(), 8u);
}

TEST(RecoveryKeyGen, KeyFormat) {
    auto [keys, hashes] = webauthn::generate_recovery_keys();
    // Format: XXXX-XXXX-XXXX-XXXX-XXXX (24 chars including dashes)
    std::regex key_pattern("^[A-Z0-9]{4}-[A-Z0-9]{4}-[A-Z0-9]{4}-[A-Z0-9]{4}-[A-Z0-9]{4}$");
    for (const auto& key : keys) {
        EXPECT_TRUE(std::regex_match(key, key_pattern)) << "Bad format: " << key;
        EXPECT_EQ(key.size(), 24u);
    }
}

TEST(RecoveryKeyGen, HashesMatchKeys) {
    auto [keys, hashes] = webauthn::generate_recovery_keys();
    for (size_t i = 0; i < keys.size(); i++) {
        EXPECT_EQ(hashes[i], webauthn::hash_recovery_key(keys[i]));
    }
}

TEST(RecoveryKeyGen, HashesAreValidHex) {
    auto [keys, hashes] = webauthn::generate_recovery_keys();
    std::regex hex_pattern("^[0-9a-f]{64}$");
    for (const auto& hash : hashes) {
        EXPECT_TRUE(std::regex_match(hash, hex_pattern));
    }
}

TEST(RecoveryKeyGen, AllKeysUnique) {
    auto [keys, hashes] = webauthn::generate_recovery_keys();
    std::set<std::string> unique_keys(keys.begin(), keys.end());
    EXPECT_EQ(unique_keys.size(), 8u);
}

TEST(RecoveryKeyGen, DifferentCallsDifferentKeys) {
    auto [keys1, hashes1] = webauthn::generate_recovery_keys();
    auto [keys2, hashes2] = webauthn::generate_recovery_keys();
    // At least some keys should differ between calls
    int same_count = 0;
    for (size_t i = 0; i < keys1.size(); i++) {
        if (keys1[i] == keys2[i]) same_count++;
    }
    EXPECT_LT(same_count, 8);
}
