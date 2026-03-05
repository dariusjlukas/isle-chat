#include <gtest/gtest.h>
#include "auth/pki.h"
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <set>
#include <regex>

// --- Helpers for Ed25519 key generation and signing ---

struct KeyPair {
    std::string public_pem;
    std::string private_pem;
};

static KeyPair generate_ed25519_keypair() {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_keygen_init(ctx);
    EVP_PKEY_keygen(ctx, &pkey);
    EVP_PKEY_CTX_free(ctx);

    // Export public key PEM
    BIO* pub_bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PUBKEY(pub_bio, pkey);
    BUF_MEM* pub_buf;
    BIO_get_mem_ptr(pub_bio, &pub_buf);
    std::string pub_pem(pub_buf->data, pub_buf->length);
    BIO_free(pub_bio);

    // Export private key PEM
    BIO* priv_bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(priv_bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    BUF_MEM* priv_buf;
    BIO_get_mem_ptr(priv_bio, &priv_buf);
    std::string priv_pem(priv_buf->data, priv_buf->length);
    BIO_free(priv_bio);

    EVP_PKEY_free(pkey);
    return {pub_pem, priv_pem};
}

static std::string sign_message(const std::string& priv_pem, const std::string& message) {
    BIO* bio = BIO_new_mem_buf(priv_pem.data(), static_cast<int>(priv_pem.size()));
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
    EVP_DigestSignInit(md_ctx, nullptr, nullptr, nullptr, pkey);

    size_t sig_len = 0;
    EVP_DigestSign(md_ctx, nullptr, &sig_len,
                   reinterpret_cast<const unsigned char*>(message.data()), message.size());

    std::vector<unsigned char> sig(sig_len);
    EVP_DigestSign(md_ctx, sig.data(), &sig_len,
                   reinterpret_cast<const unsigned char*>(message.data()), message.size());
    sig.resize(sig_len);

    EVP_MD_CTX_free(md_ctx);
    EVP_PKEY_free(pkey);

    return pki::base64_encode(sig);
}

// --- Base64 Tests ---

TEST(Base64, RoundTrip) {
    std::string original = "Hello, World!";
    std::vector<unsigned char> data(original.begin(), original.end());

    std::string encoded = pki::base64_encode(data);
    EXPECT_FALSE(encoded.empty());

    auto decoded = pki::base64_decode(encoded);
    std::string result(decoded.begin(), decoded.end());
    EXPECT_EQ(result, original);
}

TEST(Base64, EmptyInput) {
    auto decoded = pki::base64_decode("");
    EXPECT_TRUE(decoded.empty());

    std::string encoded = pki::base64_encode(nullptr, 0);
    EXPECT_TRUE(encoded.empty());
}

TEST(Base64, UrlSafeCharacters) {
    // Standard base64 uses + and /, URL-safe uses - and _
    // Create data that produces + and / in standard base64
    std::vector<unsigned char> data = {0xfb, 0xff, 0xfe};
    std::string standard_encoded = pki::base64_encode(data);

    // Replace + with - and / with _ to make URL-safe
    std::string url_safe = standard_encoded;
    for (auto& c : url_safe) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }

    // Both should decode to the same bytes
    auto from_standard = pki::base64_decode(standard_encoded);
    auto from_url_safe = pki::base64_decode(url_safe);
    EXPECT_EQ(from_standard, from_url_safe);
}

TEST(Base64, PaddingHandling) {
    // "f" encodes to "Zg==" - test without padding
    auto decoded = pki::base64_decode("Zg");
    EXPECT_EQ(decoded.size(), 1u);
    EXPECT_EQ(decoded[0], 'f');

    // "fo" encodes to "Zm8=" - test without padding
    decoded = pki::base64_decode("Zm8");
    EXPECT_EQ(decoded.size(), 2u);
    std::string result(decoded.begin(), decoded.end());
    EXPECT_EQ(result, "fo");
}

TEST(Base64, KnownVectors) {
    // RFC 4648 test vectors
    struct TestVector { std::string decoded; std::string encoded; };
    std::vector<TestVector> vectors = {
        {"f",      "Zg=="},
        {"fo",     "Zm8="},
        {"foo",    "Zm9v"},
        {"foob",   "Zm9vYg=="},
        {"fooba",  "Zm9vYmE="},
        {"foobar", "Zm9vYmFy"},
    };

    for (const auto& v : vectors) {
        // Test decode
        auto decoded = pki::base64_decode(v.encoded);
        std::string result(decoded.begin(), decoded.end());
        EXPECT_EQ(result, v.decoded) << "Failed decoding: " << v.encoded;

        // Test encode
        std::vector<unsigned char> data(v.decoded.begin(), v.decoded.end());
        std::string encoded = pki::base64_encode(data);
        EXPECT_EQ(encoded, v.encoded) << "Failed encoding: " << v.decoded;
    }
}

// --- Challenge Generation Tests ---

TEST(GenerateChallenge, Length) {
    std::string challenge = pki::generate_challenge();
    EXPECT_EQ(challenge.size(), 64u); // 32 bytes * 2 hex chars
}

TEST(GenerateChallenge, HexOnly) {
    std::string challenge = pki::generate_challenge();
    EXPECT_TRUE(std::regex_match(challenge, std::regex("^[0-9a-f]{64}$")));
}

TEST(GenerateChallenge, Uniqueness) {
    std::set<std::string> challenges;
    for (int i = 0; i < 100; i++) {
        challenges.insert(pki::generate_challenge());
    }
    EXPECT_EQ(challenges.size(), 100u);
}

// --- Signature Verification Tests ---

TEST(VerifySignature, ValidSignature) {
    auto kp = generate_ed25519_keypair();
    std::string message = "test message to sign";
    std::string sig = sign_message(kp.private_pem, message);

    EXPECT_TRUE(pki::verify_signature(kp.public_pem, message, sig));
}

TEST(VerifySignature, InvalidSignature) {
    auto kp = generate_ed25519_keypair();
    std::string message = "test message";
    std::string sig = sign_message(kp.private_pem, message);

    // Corrupt the signature
    auto sig_bytes = pki::base64_decode(sig);
    sig_bytes[0] ^= 0xff;
    std::string corrupted = pki::base64_encode(sig_bytes);

    EXPECT_FALSE(pki::verify_signature(kp.public_pem, message, corrupted));
}

TEST(VerifySignature, WrongKey) {
    auto kp1 = generate_ed25519_keypair();
    auto kp2 = generate_ed25519_keypair();
    std::string message = "test message";
    std::string sig = sign_message(kp1.private_pem, message);

    EXPECT_FALSE(pki::verify_signature(kp2.public_pem, message, sig));
}

TEST(VerifySignature, WrongMessage) {
    auto kp = generate_ed25519_keypair();
    std::string sig = sign_message(kp.private_pem, "original message");

    EXPECT_FALSE(pki::verify_signature(kp.public_pem, "different message", sig));
}

TEST(VerifySignature, InvalidBase64Signature) {
    auto kp = generate_ed25519_keypair();
    EXPECT_FALSE(pki::verify_signature(kp.public_pem, "message", "!!!not-base64!!!"));
}

TEST(VerifySignature, InvalidPEM) {
    EXPECT_FALSE(pki::verify_signature("not a PEM key", "message", "dGVzdA=="));
}

TEST(VerifySignature, EmptySignature) {
    auto kp = generate_ed25519_keypair();
    EXPECT_FALSE(pki::verify_signature(kp.public_pem, "message", ""));
}
