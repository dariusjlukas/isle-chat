#pragma once

#include <string>

// ECDSA P-256 key pair for PKI authentication.
// Matches the frontend's Web Crypto API format:
//   - Public key: SPKI DER bytes, base64url-encoded
//   - Signature: raw r||s (IEEE P1363, 64 bytes), base64url-encoded
class PkiIdentity {
 public:
  PkiIdentity();
  ~PkiIdentity();

  PkiIdentity(const PkiIdentity&) = delete;
  PkiIdentity& operator=(const PkiIdentity&) = delete;
  PkiIdentity(PkiIdentity&& other) noexcept;
  PkiIdentity& operator=(PkiIdentity&& other) noexcept;

  // Base64url-encoded SPKI DER public key
  const std::string& public_key_b64url() const { return pub_key_b64url_; }

  // Sign a message with ECDSA P-256 SHA-256, return base64url raw r||s
  std::string sign(const std::string& message) const;

 private:
  void* pkey_ = nullptr;  // EVP_PKEY*
  std::string pub_key_b64url_;
};

// Base64url encode (no padding)
std::string base64url_encode(const unsigned char* data, size_t len);

// Generate a random hex string of the given length (in characters)
std::string random_hex(int len);

// Generate a unique username for load test users
std::string unique_username();
