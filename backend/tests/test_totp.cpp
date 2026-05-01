#include <gtest/gtest.h>
#include "auth/totp.h"
#include <optional>
#include <regex>
#include <set>
#include <ctime>

// --- Base32 Encode/Decode Tests ---

TEST(Base32, RoundTrip) {
    std::string original = "Hello, World!";
    std::string encoded = totp::base32_encode(original);
    EXPECT_FALSE(encoded.empty());
    std::string decoded = totp::base32_decode(encoded);
    EXPECT_EQ(decoded, original);
}

TEST(Base32, EmptyInput) {
    EXPECT_EQ(totp::base32_encode(""), "");
    EXPECT_EQ(totp::base32_decode(""), "");
}

TEST(Base32, KnownVectors) {
    // RFC 4648 test vectors
    struct TestVector { std::string decoded; std::string encoded; };
    std::vector<TestVector> vectors = {
        {"f",      "MY"},
        {"fo",     "MZXQ"},
        {"foo",    "MZXW6"},
        {"foob",   "MZXW6YQ"},
        {"fooba",  "MZXW6YTB"},
        {"foobar", "MZXW6YTBOI"},
    };

    for (const auto& v : vectors) {
        std::string encoded = totp::base32_encode(v.decoded);
        EXPECT_EQ(encoded, v.encoded) << "Failed encoding: " << v.decoded;

        std::string decoded = totp::base32_decode(v.encoded);
        EXPECT_EQ(decoded, v.decoded) << "Failed decoding: " << v.encoded;
    }
}

TEST(Base32, CaseInsensitiveDecode) {
    // base32_decode should handle lowercase
    std::string upper = totp::base32_decode("MZXW6YTBOI");
    std::string lower = totp::base32_decode("mzxw6ytboi");
    EXPECT_EQ(upper, lower);
    EXPECT_EQ(upper, "foobar");
}

TEST(Base32, PaddingIgnored) {
    // Padding characters should be ignored during decode
    std::string with_padding = totp::base32_decode("MY======");
    std::string without_padding = totp::base32_decode("MY");
    EXPECT_EQ(with_padding, without_padding);
    EXPECT_EQ(with_padding, "f");
}

TEST(Base32, SpacesIgnored) {
    // Spaces should be stripped during decode
    std::string result = totp::base32_decode("MZXW 6YTB OI");
    EXPECT_EQ(result, "foobar");
}

TEST(Base32, BinaryData) {
    // Test with binary data (all byte values 0x00-0xFF)
    std::string binary;
    for (int i = 0; i < 256; i++) {
        binary += static_cast<char>(i);
    }
    std::string encoded = totp::base32_encode(binary);
    std::string decoded = totp::base32_decode(encoded);
    EXPECT_EQ(decoded, binary);
}

// --- TOTP Code Computation Tests ---

TEST(TotpCompute, RFC6238TestVectors) {
    // RFC 6238 uses the ASCII string "12345678901234567890" as the SHA1 secret
    // That's 20 bytes: 0x31 0x32 ... 0x30
    // Base32 of "12345678901234567890" is "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ"
    std::string secret_raw = "12345678901234567890";
    std::string secret_b32 = totp::base32_encode(secret_raw);

    // RFC 6238 Appendix B test vectors for SHA1, 30-second period:
    // Time = 59,          step = 1,          code = 287082
    // Time = 1111111109,  step = 37037036,   code = 081804
    // Time = 1111111111,  step = 37037037,   code = 050471
    // Time = 1234567890,  step = 41152263,   code = 005924
    // Time = 2000000000,  step = 66666666,   code = 279037
    // Time = 20000000000, step = 666666666,  code = 353130

    EXPECT_EQ(totp::compute_code(secret_b32, 1),         "287082");
    EXPECT_EQ(totp::compute_code(secret_b32, 37037036),  "081804");
    EXPECT_EQ(totp::compute_code(secret_b32, 37037037),  "050471");
    EXPECT_EQ(totp::compute_code(secret_b32, 41152263),  "005924");
    EXPECT_EQ(totp::compute_code(secret_b32, 66666666),  "279037");
    EXPECT_EQ(totp::compute_code(secret_b32, 666666666), "353130");
}

TEST(TotpCompute, AlwaysSixDigits) {
    // Even codes with leading zeros should be 6 characters
    std::string secret_raw = "12345678901234567890";
    std::string secret_b32 = totp::base32_encode(secret_raw);

    // step=41152263 produces "005924" which has leading zeros
    std::string code = totp::compute_code(secret_b32, 41152263);
    EXPECT_EQ(code.size(), 6u);
    EXPECT_TRUE(std::regex_match(code, std::regex("^[0-9]{6}$")));
}

TEST(TotpCompute, DifferentStepsDifferentCodes) {
    std::string secret_raw = "12345678901234567890";
    std::string secret_b32 = totp::base32_encode(secret_raw);

    std::set<std::string> codes;
    for (uint64_t step = 0; step < 20; step++) {
        codes.insert(totp::compute_code(secret_b32, step));
    }
    // At least most codes should be different (theoretically could collide, but very unlikely for 20)
    EXPECT_GT(codes.size(), 15u);
}

TEST(TotpCompute, Deterministic) {
    std::string secret_raw = "testsecretvalue1";
    std::string secret_b32 = totp::base32_encode(secret_raw);

    std::string code1 = totp::compute_code(secret_b32, 12345);
    std::string code2 = totp::compute_code(secret_b32, 12345);
    EXPECT_EQ(code1, code2);
}

// --- TOTP Verify Tests ---

TEST(TotpVerify, CorrectCodeAccepted) {
    std::string secret_raw = "12345678901234567890";
    std::string secret_b32 = totp::base32_encode(secret_raw);

    // Compute the current code
    uint64_t current_step = static_cast<uint64_t>(std::time(nullptr)) / 30;
    std::string code = totp::compute_code(secret_b32, current_step);

    auto matched = totp::verify_code(secret_b32, code, std::nullopt, 1);
    ASSERT_TRUE(matched.has_value());
    EXPECT_EQ(*matched, current_step);
}

TEST(TotpVerify, WrongCodeRejected) {
    std::string secret_raw = "12345678901234567890";
    std::string secret_b32 = totp::base32_encode(secret_raw);

    EXPECT_FALSE(totp::verify_code(secret_b32, "000000", std::nullopt, 0).has_value());
    EXPECT_FALSE(totp::verify_code(secret_b32, "999999", std::nullopt, 0).has_value());
}

TEST(TotpVerify, WindowAllowsAdjacentSteps) {
    std::string secret_raw = "12345678901234567890";
    std::string secret_b32 = totp::base32_encode(secret_raw);

    uint64_t current_step = static_cast<uint64_t>(std::time(nullptr)) / 30;

    // Code for previous step should work with window=1
    std::string prev_code = totp::compute_code(secret_b32, current_step - 1);
    auto prev_matched = totp::verify_code(secret_b32, prev_code, std::nullopt, 1);
    ASSERT_TRUE(prev_matched.has_value());
    EXPECT_EQ(*prev_matched, current_step - 1);

    // Code for next step should work with window=1
    std::string next_code = totp::compute_code(secret_b32, current_step + 1);
    auto next_matched = totp::verify_code(secret_b32, next_code, std::nullopt, 1);
    ASSERT_TRUE(next_matched.has_value());
    EXPECT_EQ(*next_matched, current_step + 1);
}

TEST(TotpVerify, ZeroWindowOnlyCurrentStep) {
    std::string secret_raw = "12345678901234567890";
    std::string secret_b32 = totp::base32_encode(secret_raw);

    uint64_t current_step = static_cast<uint64_t>(std::time(nullptr)) / 30;

    // Current step should work
    std::string current_code = totp::compute_code(secret_b32, current_step);
    auto matched = totp::verify_code(secret_b32, current_code, std::nullopt, 0);
    ASSERT_TRUE(matched.has_value());
    EXPECT_EQ(*matched, current_step);
}

// --- TOTP Replay Prevention Tests ---

TEST(TotpVerify, ReplayRejected) {
    std::string secret_raw = "12345678901234567890";
    std::string secret_b32 = totp::base32_encode(secret_raw);

    uint64_t current_step = static_cast<uint64_t>(std::time(nullptr)) / 30;
    std::string code = totp::compute_code(secret_b32, current_step);

    // Passing last_used_step = current_step should reject the code for current_step.
    auto matched = totp::verify_code(secret_b32, code, current_step, 1);
    EXPECT_FALSE(matched.has_value());
}

TEST(TotpVerify, StepAdvances) {
    std::string secret_raw = "12345678901234567890";
    std::string secret_b32 = totp::base32_encode(secret_raw);

    uint64_t current_step = static_cast<uint64_t>(std::time(nullptr)) / 30;
    std::string code = totp::compute_code(secret_b32, current_step);

    // First verify with no prior step: should accept and return the step.
    auto first = totp::verify_code(secret_b32, code, std::nullopt, 1);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(*first, current_step);

    // Second verify with last_used_step = returned step: replay, rejected.
    auto second = totp::verify_code(secret_b32, code, *first, 1);
    EXPECT_FALSE(second.has_value());
}

TEST(TotpVerify, PastWindowStepAcceptedOnce) {
    std::string secret_raw = "12345678901234567890";
    std::string secret_b32 = totp::base32_encode(secret_raw);

    uint64_t current_step = static_cast<uint64_t>(std::time(nullptr)) / 30;
    std::string prev_code = totp::compute_code(secret_b32, current_step - 1);

    // With last_used_step = current_step - 2, step-1 is still > last_used_step, so accepted.
    auto accepted = totp::verify_code(secret_b32, prev_code, current_step - 2, 1);
    ASSERT_TRUE(accepted.has_value());
    EXPECT_EQ(*accepted, current_step - 1);

    // Now with last_used_step = step-1, replay of the same code is rejected.
    auto rejected = totp::verify_code(secret_b32, prev_code, current_step - 1, 1);
    EXPECT_FALSE(rejected.has_value());
}

// --- Secret Generation Tests ---

TEST(TotpSecret, GeneratesNonEmpty) {
    std::string secret = totp::generate_secret();
    EXPECT_FALSE(secret.empty());
}

TEST(TotpSecret, GeneratesValidBase32) {
    std::string secret = totp::generate_secret();
    // Should only contain valid base32 characters
    EXPECT_TRUE(std::regex_match(secret, std::regex("^[A-Z2-7]+$")));
}

TEST(TotpSecret, GeneratesUniqueSecrets) {
    std::set<std::string> secrets;
    for (int i = 0; i < 50; i++) {
        secrets.insert(totp::generate_secret());
    }
    EXPECT_EQ(secrets.size(), 50u);
}

TEST(TotpSecret, RoundTripsTo20Bytes) {
    std::string secret = totp::generate_secret();
    std::string decoded = totp::base32_decode(secret);
    EXPECT_EQ(decoded.size(), 20u);
}

// --- URI Builder Tests ---

TEST(TotpUri, BasicFormat) {
    std::string uri = totp::build_uri("JBSWY3DPEHPK3PXP", "alice", "MyChatApp");
    EXPECT_EQ(uri, "otpauth://totp/MyChatApp:alice?secret=JBSWY3DPEHPK3PXP&issuer=MyChatApp&algorithm=SHA1&digits=6&period=30");
}

TEST(TotpUri, SpecialCharactersEncoded) {
    std::string uri = totp::build_uri("JBSWY3DPEHPK3PXP", "user@example.com", "My App");
    // @ should be encoded, space should be encoded
    EXPECT_NE(uri.find("user%40example.com"), std::string::npos);
    EXPECT_NE(uri.find("My%20App"), std::string::npos);
    // Should still start with otpauth://
    EXPECT_EQ(uri.substr(0, 15), "otpauth://totp/");
}

TEST(TotpUri, ContainsRequiredParams) {
    std::string uri = totp::build_uri("ABCDEFGH", "bob", "TestIssuer");
    EXPECT_NE(uri.find("secret=ABCDEFGH"), std::string::npos);
    EXPECT_NE(uri.find("issuer=TestIssuer"), std::string::npos);
    EXPECT_NE(uri.find("algorithm=SHA1"), std::string::npos);
    EXPECT_NE(uri.find("digits=6"), std::string::npos);
    EXPECT_NE(uri.find("period=30"), std::string::npos);
}
