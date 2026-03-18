#include "pki_identity.h"

#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/x509.h>

#include <cstring>
#include <stdexcept>
#include <vector>

static const char B64URL_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string base64url_encode(const unsigned char* data, size_t len) {
  std::string result;
  result.reserve(((len + 2) / 3) * 4);

  for (size_t i = 0; i < len; i += 3) {
    unsigned int n = (unsigned int)data[i] << 16;
    if (i + 1 < len) n |= (unsigned int)data[i + 1] << 8;
    if (i + 2 < len) n |= (unsigned int)data[i + 2];

    result += B64URL_CHARS[(n >> 18) & 0x3F];
    result += B64URL_CHARS[(n >> 12) & 0x3F];
    if (i + 1 < len)
      result += B64URL_CHARS[(n >> 6) & 0x3F];
    if (i + 2 < len)
      result += B64URL_CHARS[n & 0x3F];
  }
  return result;
}

std::string random_hex(int len) {
  static const char hex[] = "0123456789abcdef";
  int bytes_needed = (len + 1) / 2;
  std::vector<unsigned char> buf(bytes_needed);
  RAND_bytes(buf.data(), bytes_needed);

  std::string result;
  result.reserve(len);
  for (int i = 0; i < bytes_needed && (int)result.size() < len; i++) {
    result += hex[(buf[i] >> 4) & 0xF];
    if ((int)result.size() < len) result += hex[buf[i] & 0xF];
  }
  return result;
}

std::string unique_username() { return "lt_" + random_hex(12); }

PkiIdentity::PkiIdentity() {
  // Generate EC P-256 key pair
  EVP_PKEY* pkey = EVP_EC_gen("P-256");
  if (!pkey) throw std::runtime_error("Failed to generate EC P-256 key");
  pkey_ = pkey;

  // Export SPKI DER public key
  unsigned char* der = nullptr;
  int der_len = i2d_PUBKEY(pkey, &der);
  if (der_len <= 0 || !der) {
    EVP_PKEY_free(pkey);
    pkey_ = nullptr;
    throw std::runtime_error("Failed to export SPKI DER");
  }

  pub_key_b64url_ = base64url_encode(der, der_len);
  OPENSSL_free(der);
}

PkiIdentity::~PkiIdentity() {
  if (pkey_) EVP_PKEY_free(static_cast<EVP_PKEY*>(pkey_));
}

PkiIdentity::PkiIdentity(PkiIdentity&& other) noexcept
    : pkey_(other.pkey_), pub_key_b64url_(std::move(other.pub_key_b64url_)) {
  other.pkey_ = nullptr;
}

PkiIdentity& PkiIdentity::operator=(PkiIdentity&& other) noexcept {
  if (this != &other) {
    if (pkey_) EVP_PKEY_free(static_cast<EVP_PKEY*>(pkey_));
    pkey_ = other.pkey_;
    pub_key_b64url_ = std::move(other.pub_key_b64url_);
    other.pkey_ = nullptr;
  }
  return *this;
}

std::string PkiIdentity::sign(const std::string& message) const {
  EVP_PKEY* pkey = static_cast<EVP_PKEY*>(pkey_);
  if (!pkey) throw std::runtime_error("No key available for signing");

  // Sign with ECDSA SHA-256 (produces DER-encoded signature)
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (!ctx) throw std::runtime_error("Failed to create MD context");

  if (EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) != 1) {
    EVP_MD_CTX_free(ctx);
    throw std::runtime_error("EVP_DigestSignInit failed");
  }

  if (EVP_DigestSignUpdate(ctx, message.data(), message.size()) != 1) {
    EVP_MD_CTX_free(ctx);
    throw std::runtime_error("EVP_DigestSignUpdate failed");
  }

  // Get DER signature size
  size_t sig_len = 0;
  if (EVP_DigestSignFinal(ctx, nullptr, &sig_len) != 1) {
    EVP_MD_CTX_free(ctx);
    throw std::runtime_error("EVP_DigestSignFinal (size) failed");
  }

  std::vector<unsigned char> der_sig(sig_len);
  if (EVP_DigestSignFinal(ctx, der_sig.data(), &sig_len) != 1) {
    EVP_MD_CTX_free(ctx);
    throw std::runtime_error("EVP_DigestSignFinal failed");
  }
  EVP_MD_CTX_free(ctx);
  der_sig.resize(sig_len);

  // Parse DER to extract r and s BIGNUMs
  const unsigned char* p = der_sig.data();
  ECDSA_SIG* ec_sig = d2i_ECDSA_SIG(nullptr, &p, (long)der_sig.size());
  if (!ec_sig) throw std::runtime_error("Failed to parse DER signature");

  const BIGNUM* r = nullptr;
  const BIGNUM* s = nullptr;
  ECDSA_SIG_get0(ec_sig, &r, &s);

  // Convert r and s to 32-byte big-endian (IEEE P1363 format: r || s)
  unsigned char raw_sig[64];
  memset(raw_sig, 0, 64);
  BN_bn2binpad(r, raw_sig, 32);
  BN_bn2binpad(s, raw_sig + 32, 32);

  ECDSA_SIG_free(ec_sig);

  return base64url_encode(raw_sig, 64);
}
