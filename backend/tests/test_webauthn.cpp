#include <gtest/gtest.h>
#include "auth/webauthn.h"
#include <nlohmann/json.hpp>
#include <set>
#include <regex>
#include <stdexcept>
#include <memory>
#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

using json = nlohmann::json;

namespace {

using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;

struct EcKeyMaterial {
    EvpPkeyPtr pkey;
    std::vector<unsigned char> x;
    std::vector<unsigned char> y;
    std::vector<unsigned char> public_key_xy;
    std::string public_key_spki_b64url;

    EcKeyMaterial() : pkey(nullptr, EVP_PKEY_free) {}
};

std::vector<unsigned char> sha256_bytes(const void* data, size_t len) {
    std::vector<unsigned char> hash(SHA256_DIGEST_LENGTH);
    SHA256(static_cast<const unsigned char*>(data), len, hash.data());
    return hash;
}

std::vector<unsigned char> sha256_bytes(const std::string& value) {
    return sha256_bytes(value.data(), value.size());
}

EcKeyMaterial generate_p256_key() {
    EcKeyMaterial material;
    material.pkey.reset(EVP_EC_gen("prime256v1"));
    if (!material.pkey) {
        throw std::runtime_error("Failed to generate EC key");
    }

    size_t point_len = 0;
    if (EVP_PKEY_get_octet_string_param(material.pkey.get(), OSSL_PKEY_PARAM_PUB_KEY,
                                        nullptr, 0, &point_len) != 1 ||
        point_len != 65) {
        throw std::runtime_error("Unexpected public key length");
    }

    std::vector<unsigned char> uncompressed(point_len);
    if (EVP_PKEY_get_octet_string_param(material.pkey.get(), OSSL_PKEY_PARAM_PUB_KEY,
                                        uncompressed.data(), uncompressed.size(),
                                        &point_len) != 1 ||
        uncompressed[0] != 0x04) {
        throw std::runtime_error("Failed to export public key");
    }

    material.x.assign(uncompressed.begin() + 1, uncompressed.begin() + 33);
    material.y.assign(uncompressed.begin() + 33, uncompressed.end());
    material.public_key_xy.reserve(64);
    material.public_key_xy.insert(material.public_key_xy.end(),
                                  material.x.begin(), material.x.end());
    material.public_key_xy.insert(material.public_key_xy.end(),
                                  material.y.begin(), material.y.end());

    int spki_len = i2d_PUBKEY(material.pkey.get(), nullptr);
    if (spki_len <= 0) {
        throw std::runtime_error("Failed to encode SPKI");
    }

    std::vector<unsigned char> spki(spki_len);
    unsigned char* spki_ptr = spki.data();
    if (i2d_PUBKEY(material.pkey.get(), &spki_ptr) != spki_len) {
        throw std::runtime_error("Failed to serialize SPKI");
    }

    material.public_key_spki_b64url = webauthn::base64url_encode(spki);
    return material;
}

std::string make_client_data_b64(const std::string& type,
                                 const std::string& challenge,
                                 const std::string& origin) {
    std::string client_data = json{
        {"type", type},
        {"challenge", challenge},
        {"origin", origin}
    }.dump();
    return webauthn::base64url_encode(
        reinterpret_cast<const unsigned char*>(client_data.data()), client_data.size());
}

void append_cbor_int(std::vector<unsigned char>& out, int value) {
    if (value >= 0 && value < 24) {
        out.push_back(static_cast<unsigned char>(value));
        return;
    }
    if (value >= 24 && value <= 0xff) {
        out.push_back(0x18);
        out.push_back(static_cast<unsigned char>(value));
        return;
    }
    if (value < 0 && value >= -24) {
        out.push_back(static_cast<unsigned char>(0x20 + (-1 - value)));
        return;
    }
    throw std::runtime_error("Unsupported CBOR integer for test helper");
}

void append_cbor_bstr(std::vector<unsigned char>& out,
                      const std::vector<unsigned char>& value) {
    if (value.size() < 24) {
        out.push_back(static_cast<unsigned char>(0x40 + value.size()));
    } else if (value.size() <= 0xff) {
        out.push_back(0x58);
        out.push_back(static_cast<unsigned char>(value.size()));
    } else {
        throw std::runtime_error("Unsupported CBOR byte string length for test helper");
    }
    out.insert(out.end(), value.begin(), value.end());
}

std::vector<unsigned char> make_cose_key(const std::vector<unsigned char>& x,
                                         const std::vector<unsigned char>& y,
                                         int alg = -7,
                                         int crv = 1,
                                         bool add_unknown_label = false) {
    std::vector<unsigned char> cose;
    cose.push_back(static_cast<unsigned char>(add_unknown_label ? 0xa6 : 0xa5));

    append_cbor_int(cose, 1);
    append_cbor_int(cose, 2);

    append_cbor_int(cose, 3);
    append_cbor_int(cose, alg);

    append_cbor_int(cose, -1);
    append_cbor_int(cose, crv);

    append_cbor_int(cose, -2);
    append_cbor_bstr(cose, x);

    append_cbor_int(cose, -3);
    append_cbor_bstr(cose, y);

    if (add_unknown_label) {
        append_cbor_int(cose, 4);
        append_cbor_int(cose, 99);
    }

    return cose;
}

std::vector<unsigned char> make_registration_auth_data(
    const std::string& rp_id,
    uint8_t flags,
    uint32_t sign_count,
    const std::vector<unsigned char>& credential_id,
    const std::vector<unsigned char>& cose_key) {
    std::vector<unsigned char> auth_data = sha256_bytes(rp_id);
    auth_data.push_back(flags);
    auth_data.push_back(static_cast<unsigned char>((sign_count >> 24) & 0xff));
    auth_data.push_back(static_cast<unsigned char>((sign_count >> 16) & 0xff));
    auth_data.push_back(static_cast<unsigned char>((sign_count >> 8) & 0xff));
    auth_data.push_back(static_cast<unsigned char>(sign_count & 0xff));
    auth_data.insert(auth_data.end(), 16, 0x00);
    auth_data.push_back(static_cast<unsigned char>((credential_id.size() >> 8) & 0xff));
    auth_data.push_back(static_cast<unsigned char>(credential_id.size() & 0xff));
    auth_data.insert(auth_data.end(), credential_id.begin(), credential_id.end());
    auth_data.insert(auth_data.end(), cose_key.begin(), cose_key.end());
    return auth_data;
}

std::vector<unsigned char> make_authentication_auth_data(const std::string& rp_id,
                                                         uint8_t flags,
                                                         uint32_t sign_count) {
    std::vector<unsigned char> auth_data = sha256_bytes(rp_id);
    auth_data.push_back(flags);
    auth_data.push_back(static_cast<unsigned char>((sign_count >> 24) & 0xff));
    auth_data.push_back(static_cast<unsigned char>((sign_count >> 16) & 0xff));
    auth_data.push_back(static_cast<unsigned char>((sign_count >> 8) & 0xff));
    auth_data.push_back(static_cast<unsigned char>(sign_count & 0xff));
    return auth_data;
}

std::string make_attestation_object_b64(const std::vector<unsigned char>& auth_data) {
    json attestation = {
        {"fmt", "none"},
        {"attStmt", json::object()}
    };
    attestation["authData"] = json::binary(auth_data);
    return webauthn::base64url_encode(json::to_cbor(attestation));
}

std::vector<unsigned char> sign_es256_der(EVP_PKEY* pkey,
                                          const std::vector<unsigned char>& payload) {
    EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
    if (!md_ctx) {
        throw std::runtime_error("Failed to create digest context");
    }

    if (EVP_DigestSignInit(md_ctx, nullptr, EVP_sha256(), nullptr, pkey) != 1) {
        EVP_MD_CTX_free(md_ctx);
        throw std::runtime_error("Failed to initialize signature");
    }

    size_t sig_len = 0;
    if (EVP_DigestSign(md_ctx, nullptr, &sig_len, payload.data(), payload.size()) != 1) {
        EVP_MD_CTX_free(md_ctx);
        throw std::runtime_error("Failed to size signature");
    }

    std::vector<unsigned char> signature(sig_len);
    if (EVP_DigestSign(md_ctx, signature.data(), &sig_len,
                       payload.data(), payload.size()) != 1) {
        EVP_MD_CTX_free(md_ctx);
        throw std::runtime_error("Failed to sign payload");
    }

    signature.resize(sig_len);
    EVP_MD_CTX_free(md_ctx);
    return signature;
}

std::vector<unsigned char> der_signature_to_p1363(const std::vector<unsigned char>& der_signature) {
    const unsigned char* sig_ptr = der_signature.data();
    ECDSA_SIG* ec_sig = d2i_ECDSA_SIG(nullptr, &sig_ptr, der_signature.size());
    if (!ec_sig) {
        throw std::runtime_error("Failed to decode DER signature");
    }

    const BIGNUM* r = nullptr;
    const BIGNUM* s = nullptr;
    ECDSA_SIG_get0(ec_sig, &r, &s);

    std::vector<unsigned char> raw(64);
    if (BN_bn2binpad(r, raw.data(), 32) != 32 ||
        BN_bn2binpad(s, raw.data() + 32, 32) != 32) {
        ECDSA_SIG_free(ec_sig);
        throw std::runtime_error("Failed to convert signature to P1363");
    }

    ECDSA_SIG_free(ec_sig);
    return raw;
}

std::vector<unsigned char> make_signed_auth_payload(const std::vector<unsigned char>& auth_data,
                                                    const std::string& client_data_b64) {
    auto client_data_json = webauthn::base64url_decode(client_data_b64);
    auto client_hash = sha256_bytes(client_data_json.data(), client_data_json.size());

    std::vector<unsigned char> payload = auth_data;
    payload.insert(payload.end(), client_hash.begin(), client_hash.end());
    return payload;
}

}  // namespace

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

TEST(Base64url, IgnoresNewlinesAndInvalidCharacters) {
    auto decoded = webauthn::base64url_decode("Zg==\n!!");
    ASSERT_EQ(decoded.size(), 1u);
    EXPECT_EQ(decoded[0], 'f');
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

// --- Registration Verification Tests ---

TEST(WebAuthnRegistration, VerifyRegistrationParsesValidCredential) {
    auto key = generate_p256_key();
    std::string challenge = "registration-challenge";
    std::string client_data_b64 = make_client_data_b64(
        "webauthn.create", challenge, "http://localhost:5173");

    std::vector<unsigned char> credential_id = {0x01, 0x02, 0x03, 0x04, 0x05};
    auto cose_key = make_cose_key(key.x, key.y, -7, 1, true);
    auto auth_data = make_registration_auth_data(
        "localhost", 0x41, 7, credential_id, cose_key);

    auto result = webauthn::verify_registration(
        make_attestation_object_b64(auth_data),
        client_data_b64,
        challenge,
        "http://localhost:5173",
        "localhost");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->credential_id, webauthn::base64url_encode(credential_id));
    EXPECT_EQ(result->public_key, key.public_key_xy);
    EXPECT_EQ(result->sign_count, 7u);
}

TEST(WebAuthnRegistration, RejectsRpIdHashMismatch) {
    auto key = generate_p256_key();
    std::string challenge = "registration-challenge";
    std::string client_data_b64 = make_client_data_b64(
        "webauthn.create", challenge, "https://chat.example");

    std::vector<unsigned char> credential_id = {0xaa, 0xbb, 0xcc};
    auto auth_data = make_registration_auth_data(
        "wrong.example", 0x41, 1, credential_id, make_cose_key(key.x, key.y));

    EXPECT_THROW(
        webauthn::verify_registration(
            make_attestation_object_b64(auth_data),
            client_data_b64,
            challenge,
            "https://chat.example",
            "chat.example"),
        std::runtime_error);
}

TEST(WebAuthnRegistration, RejectsMissingUserPresenceFlag) {
    auto key = generate_p256_key();
    std::string challenge = "registration-challenge";
    std::string client_data_b64 = make_client_data_b64(
        "webauthn.create", challenge, "https://chat.example");

    std::vector<unsigned char> credential_id = {0x10, 0x20, 0x30};
    auto auth_data = make_registration_auth_data(
        "chat.example", 0x40, 1, credential_id, make_cose_key(key.x, key.y));

    EXPECT_THROW(
        webauthn::verify_registration(
            make_attestation_object_b64(auth_data),
            client_data_b64,
            challenge,
            "https://chat.example",
            "chat.example"),
        std::runtime_error);
}

TEST(WebAuthnRegistration, RejectsMissingAttestedCredentialData) {
    std::string challenge = "registration-challenge";
    std::string client_data_b64 = make_client_data_b64(
        "webauthn.create", challenge, "https://chat.example");

    auto auth_data = make_authentication_auth_data("chat.example", 0x01, 1);

    EXPECT_THROW(
        webauthn::verify_registration(
            make_attestation_object_b64(auth_data),
            client_data_b64,
            challenge,
            "https://chat.example",
            "chat.example"),
        std::runtime_error);
}

TEST(WebAuthnRegistration, RejectsUnsupportedCoseAlgorithm) {
    auto key = generate_p256_key();
    std::string challenge = "registration-challenge";
    std::string client_data_b64 = make_client_data_b64(
        "webauthn.create", challenge, "https://chat.example");

    std::vector<unsigned char> credential_id = {0x99, 0x88};
    auto auth_data = make_registration_auth_data(
        "chat.example", 0x41, 1, credential_id, make_cose_key(key.x, key.y, -8));

    EXPECT_THROW(
        webauthn::verify_registration(
            make_attestation_object_b64(auth_data),
            client_data_b64,
            challenge,
            "https://chat.example",
            "chat.example"),
        std::runtime_error);
}

TEST(WebAuthnRegistration, RejectsWrongClientDataType) {
    auto key = generate_p256_key();
    std::string challenge = "registration-challenge";
    std::string client_data_b64 = make_client_data_b64(
        "webauthn.get", challenge, "https://chat.example");

    std::vector<unsigned char> credential_id = {0x44, 0x55};
    auto auth_data = make_registration_auth_data(
        "chat.example", 0x41, 1, credential_id, make_cose_key(key.x, key.y));

    EXPECT_THROW(
        webauthn::verify_registration(
            make_attestation_object_b64(auth_data),
            client_data_b64,
            challenge,
            "https://chat.example",
            "chat.example"),
        std::runtime_error);
}

// --- Authentication Verification Tests ---

TEST(WebAuthnAuthentication, VerifyAuthenticationAcceptsValidAssertion) {
    auto key = generate_p256_key();
    std::string challenge = "authentication-challenge";
    std::string client_data_b64 = make_client_data_b64(
        "webauthn.get", challenge, "https://chat.example");

    auto auth_data = make_authentication_auth_data("chat.example", 0x01, 2);
    auto signature = sign_es256_der(
        key.pkey.get(), make_signed_auth_payload(auth_data, client_data_b64));

    auto result = webauthn::verify_authentication(
        webauthn::base64url_encode(auth_data),
        client_data_b64,
        webauthn::base64url_encode(signature),
        key.public_key_xy,
        1,
        challenge,
        "https://chat.example",
        "chat.example");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 2u);
}

TEST(WebAuthnAuthentication, AllowsAuthenticatorsWithoutSignCounter) {
    auto key = generate_p256_key();
    std::string challenge = "authentication-challenge";
    std::string client_data_b64 = make_client_data_b64(
        "webauthn.get", challenge, "http://localhost:5173");

    auto auth_data = make_authentication_auth_data("localhost", 0x01, 0);
    auto signature = sign_es256_der(
        key.pkey.get(), make_signed_auth_payload(auth_data, client_data_b64));

    auto result = webauthn::verify_authentication(
        webauthn::base64url_encode(auth_data),
        client_data_b64,
        webauthn::base64url_encode(signature),
        key.public_key_xy,
        0,
        challenge,
        "http://localhost:5173",
        "localhost");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0u);
}

TEST(WebAuthnAuthentication, RejectsNonIncrementedSignCount) {
    auto key = generate_p256_key();
    std::string challenge = "authentication-challenge";
    std::string client_data_b64 = make_client_data_b64(
        "webauthn.get", challenge, "https://chat.example");

    auto auth_data = make_authentication_auth_data("chat.example", 0x01, 4);
    auto signature = sign_es256_der(
        key.pkey.get(), make_signed_auth_payload(auth_data, client_data_b64));

    EXPECT_THROW(
        webauthn::verify_authentication(
            webauthn::base64url_encode(auth_data),
            client_data_b64,
            webauthn::base64url_encode(signature),
            key.public_key_xy,
            4,
            challenge,
            "https://chat.example",
            "chat.example"),
        std::runtime_error);
}

TEST(WebAuthnAuthentication, RejectsInvalidSignature) {
    auto key = generate_p256_key();
    auto wrong_key = generate_p256_key();
    std::string challenge = "authentication-challenge";
    std::string client_data_b64 = make_client_data_b64(
        "webauthn.get", challenge, "https://chat.example");

    auto auth_data = make_authentication_auth_data("chat.example", 0x01, 5);
    auto signature = sign_es256_der(
        wrong_key.pkey.get(), make_signed_auth_payload(auth_data, client_data_b64));

    EXPECT_THROW(
        webauthn::verify_authentication(
            webauthn::base64url_encode(auth_data),
            client_data_b64,
            webauthn::base64url_encode(signature),
            key.public_key_xy,
            1,
            challenge,
            "https://chat.example",
            "chat.example"),
        std::runtime_error);
}

TEST(WebAuthnAuthentication, RejectsMissingUserPresenceFlag) {
    auto key = generate_p256_key();
    std::string challenge = "authentication-challenge";
    std::string client_data_b64 = make_client_data_b64(
        "webauthn.get", challenge, "https://chat.example");

    auto auth_data = make_authentication_auth_data("chat.example", 0x00, 3);
    auto signature = sign_es256_der(
        key.pkey.get(), make_signed_auth_payload(auth_data, client_data_b64));

    EXPECT_THROW(
        webauthn::verify_authentication(
            webauthn::base64url_encode(auth_data),
            client_data_b64,
            webauthn::base64url_encode(signature),
            key.public_key_xy,
            1,
            challenge,
            "https://chat.example",
            "chat.example"),
        std::runtime_error);
}

// --- PKI Verification Tests ---

TEST(WebAuthnPki, VerifiesDerEncodedSignature) {
    auto key = generate_p256_key();
    std::string challenge = "server-issued-challenge";
    std::vector<unsigned char> payload(challenge.begin(), challenge.end());
    auto signature = sign_es256_der(key.pkey.get(), payload);

    EXPECT_TRUE(webauthn::verify_pki_signature(
        key.public_key_spki_b64url,
        challenge,
        webauthn::base64url_encode(signature)));
}

TEST(WebAuthnPki, VerifiesRawP1363Signature) {
    auto key = generate_p256_key();
    std::string challenge = "server-issued-challenge";
    std::vector<unsigned char> payload(challenge.begin(), challenge.end());
    auto der_signature = sign_es256_der(key.pkey.get(), payload);
    auto raw_signature = der_signature_to_p1363(der_signature);

    EXPECT_TRUE(webauthn::verify_pki_signature(
        key.public_key_spki_b64url,
        challenge,
        webauthn::base64url_encode(raw_signature)));
}

TEST(WebAuthnPki, RejectsWrongChallenge) {
    auto key = generate_p256_key();
    std::vector<unsigned char> payload({'c', 'o', 'r', 'r', 'e', 'c', 't'});
    auto signature = sign_es256_der(key.pkey.get(), payload);

    EXPECT_FALSE(webauthn::verify_pki_signature(
        key.public_key_spki_b64url,
        "incorrect",
        webauthn::base64url_encode(signature)));
}
