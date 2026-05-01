#include "auth/pki.h"
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include "logging/logger.h"

namespace pki {

std::vector<unsigned char> base64_decode(const std::string& b64) {
  // Handle URL-safe base64
  std::string input = b64;
  for (auto& c : input) {
    if (c == '-')
      c = '+';
    else if (c == '_')
      c = '/';
  }
  // Add padding if needed
  while (input.size() % 4 != 0) input += '=';

  BIO* bio = BIO_new_mem_buf(input.data(), static_cast<int>(input.size()));
  BIO* b64_bio = BIO_new(BIO_f_base64());
  bio = BIO_push(b64_bio, bio);
  BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);

  std::vector<unsigned char> out(input.size());
  int len = BIO_read(bio, out.data(), static_cast<int>(out.size()));
  BIO_free_all(bio);

  if (len < 0) return {};
  out.resize(len);
  return out;
}

std::string base64_encode(const unsigned char* data, size_t len) {
  BIO* bio = BIO_new(BIO_s_mem());
  BIO* b64_bio = BIO_new(BIO_f_base64());
  bio = BIO_push(b64_bio, bio);
  BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
  BIO_write(bio, data, static_cast<int>(len));
  BIO_flush(bio);

  BUF_MEM* bptr;
  BIO_get_mem_ptr(bio, &bptr);
  std::string result(bptr->data, bptr->length);
  BIO_free_all(bio);
  return result;
}

std::string base64_encode(const std::vector<unsigned char>& data) {
  return base64_encode(data.data(), data.size());
}

bool verify_signature(
  const std::string& public_key_pem, const std::string& message, const std::string& signature_b64) {
  // Decode the signature from base64
  auto sig_bytes = base64_decode(signature_b64);
  if (sig_bytes.empty()) {
    LOG_ERROR_N("pki", nullptr, "Failed to decode signature from base64");
    return false;
  }

  // Load the public key from PEM
  BIO* bio = BIO_new_mem_buf(public_key_pem.data(), static_cast<int>(public_key_pem.size()));
  if (!bio) {
    LOG_ERROR_N("pki", nullptr, "Failed to create BIO");
    return false;
  }

  EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);

  if (!pkey) {
    // Try reading as raw Ed25519 public key (base64 of 32 bytes)
    // The frontend sends the raw key in SPKI/PEM format, so this path
    // is mainly a fallback
    LOG_ERROR_N("pki", nullptr, "Failed to read PEM public key");
    ERR_print_errors_fp(stderr);
    return false;
  }

  // Verify the signature
  EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
  bool valid = false;

  if (EVP_DigestVerifyInit(md_ctx, nullptr, nullptr, nullptr, pkey) == 1) {
    int rc = EVP_DigestVerify(
      md_ctx,
      sig_bytes.data(),
      sig_bytes.size(),
      reinterpret_cast<const unsigned char*>(message.data()),
      message.size());
    valid = (rc == 1);
    if (!valid) {
      // Demoted to debug: this is expected during normal authentication flow
      // (e.g. wrong key). Logging at error/info would be a noise/info-leak risk.
      LOG_DEBUG_N("pki", nullptr, "Signature verification failed (rc=" + std::to_string(rc) + ")");
    }
  }

  EVP_MD_CTX_free(md_ctx);
  EVP_PKEY_free(pkey);
  return valid;
}

std::string generate_challenge() {
  unsigned char buf[16];
  if (RAND_bytes(buf, sizeof(buf)) != 1) {
    throw std::runtime_error("RAND_bytes failed");
  }
  std::ostringstream ss;
  for (unsigned char b : buf) {
    ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
  }
  return ss.str();
}

}  // namespace pki
