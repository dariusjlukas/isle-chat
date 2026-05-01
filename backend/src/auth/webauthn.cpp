#include "auth/webauthn.h"
#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#include <cstring>
#include <nlohmann/json.hpp>
#include "logging/logger.h"

using json = nlohmann::json;

namespace webauthn {

// --- Base64url encoding/decoding ---

static const char B64_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64_char_value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+' || c == '-') return 62;
  if (c == '/' || c == '_') return 63;
  return -1;
}

std::vector<unsigned char> base64url_decode(const std::string& input) {
  std::vector<unsigned char> out;
  out.reserve(input.size() * 3 / 4);

  unsigned int val = 0;
  int bits = 0;
  for (char c : input) {
    if (c == '=' || c == '\n' || c == '\r') continue;
    int v = b64_char_value(c);
    if (v < 0) continue;
    val = (val << 6) | static_cast<unsigned int>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<unsigned char>((val >> bits) & 0xFF));
    }
  }
  return out;
}

std::string base64url_encode(const unsigned char* data, size_t len) {
  std::string result;
  result.reserve((len + 2) / 3 * 4);

  unsigned int val = 0;
  int bits = 0;
  for (size_t i = 0; i < len; i++) {
    val = (val << 8) | data[i];
    bits += 8;
    while (bits >= 6) {
      bits -= 6;
      result += B64_CHARS[(val >> bits) & 0x3F];
    }
  }
  if (bits > 0) {
    result += B64_CHARS[(val << (6 - bits)) & 0x3F];
  }

  // Convert to base64url: replace + with -, / with _
  for (auto& c : result) {
    if (c == '+')
      c = '-';
    else if (c == '/')
      c = '_';
  }
  // No padding for base64url
  return result;
}

std::string base64url_encode(const std::vector<unsigned char>& data) {
  return base64url_encode(data.data(), data.size());
}

std::string generate_challenge() {
  unsigned char buf[32];
  RAND_bytes(buf, sizeof(buf));
  return base64url_encode(buf, sizeof(buf));
}

// --- Internal helpers ---

// Compute SHA-256 hash
static std::vector<unsigned char> sha256(const void* data, size_t len) {
  std::vector<unsigned char> hash(SHA256_DIGEST_LENGTH);
  SHA256(static_cast<const unsigned char*>(data), len, hash.data());
  return hash;
}

// --- Minimal CBOR parser for COSE keys (handles integer map keys) ---

struct CborReader {
  const unsigned char* data;
  size_t size;
  size_t pos = 0;

  CborReader(const std::vector<unsigned char>& v) : data(v.data()), size(v.size()), pos(0) {}

  void ensure(size_t n) const {
    if (pos + n > size)
      throw std::runtime_error(
        "CBOR: unexpected end of data at pos " + std::to_string(pos) + " need " +
        std::to_string(n));
  }

  uint8_t read_u8() {
    ensure(1);
    return data[pos++];
  }

  // Read the "additional info" value (may consume 0, 1, 2, 4, or 8 extra bytes)
  uint64_t read_additional(uint8_t additional) {
    if (additional < 24) return additional;
    if (additional == 24) {
      return read_u8();
    }
    if (additional == 25) {
      ensure(2);
      uint64_t v = (uint64_t(data[pos]) << 8) | data[pos + 1];
      pos += 2;
      return v;
    }
    if (additional == 26) {
      ensure(4);
      uint64_t v = (uint64_t(data[pos]) << 24) | (uint64_t(data[pos + 1]) << 16) |
                   (uint64_t(data[pos + 2]) << 8) | data[pos + 3];
      pos += 4;
      return v;
    }
    throw std::runtime_error("CBOR: unsupported additional value " + std::to_string(additional));
  }

  // Read integer (major type 0 or 1), returns int64_t
  int64_t read_int() {
    uint8_t b = read_u8();
    uint8_t major = b >> 5;
    uint8_t additional = b & 0x1f;
    uint64_t val = read_additional(additional);
    if (major == 0) return static_cast<int64_t>(val);
    if (major == 1) return -1 - static_cast<int64_t>(val);
    throw std::runtime_error("CBOR: expected integer, got major type " + std::to_string(major));
  }

  // Read byte string (major type 2)
  std::vector<unsigned char> read_bstr() {
    uint8_t b = read_u8();
    uint8_t major = b >> 5;
    uint8_t additional = b & 0x1f;
    if (major != 2)
      throw std::runtime_error(
        "CBOR: expected byte string, got major type " + std::to_string(major));
    uint64_t len = read_additional(additional);
    ensure(len);
    std::vector<unsigned char> result(data + pos, data + pos + len);
    pos += len;
    return result;
  }

  // Read map header (major type 5), returns item count
  size_t read_map_header() {
    uint8_t b = read_u8();
    uint8_t major = b >> 5;
    uint8_t additional = b & 0x1f;
    if (major != 5)
      throw std::runtime_error("CBOR: expected map, got major type " + std::to_string(major));
    return static_cast<size_t>(read_additional(additional));
  }

  // Skip any CBOR value
  void skip() {
    uint8_t b = read_u8();
    uint8_t major = b >> 5;
    uint8_t additional = b & 0x1f;
    uint64_t val = read_additional(additional);
    switch (major) {
    case 0:
    case 1:
      break;  // integers already fully read
    case 2:
    case 3:
      ensure(val);
      pos += val;
      break;  // byte/text string
    case 4:
      for (uint64_t i = 0; i < val; i++) skip();
      break;  // array
    case 5:
      for (uint64_t i = 0; i < val * 2; i++) skip();
      break;  // map
    case 7:
      break;  // simple/float
    default:
      throw std::runtime_error("CBOR: cannot skip major type " + std::to_string(major));
    }
  }
};

// Parse a COSE EC key (ES256/P-256) from raw CBOR bytes
// Extracts kty, alg, crv, and x/y coordinates
struct CoseKeyData {
  int kty = 0, alg = 0, crv = 0;
  std::vector<unsigned char> x, y;
};

static CoseKeyData parse_cose_ec_key(const std::vector<unsigned char>& cose_bytes) {
  CborReader reader(cose_bytes);
  size_t map_len = reader.read_map_header();

  CoseKeyData key;
  for (size_t i = 0; i < map_len; i++) {
    int64_t label = reader.read_int();
    switch (label) {
    case 1:
      key.kty = static_cast<int>(reader.read_int());
      break;  // kty
    case 3:
      key.alg = static_cast<int>(reader.read_int());
      break;  // alg
    case -1:
      key.crv = static_cast<int>(reader.read_int());
      break;  // crv
    case -2:
      key.x = reader.read_bstr();
      break;  // x
    case -3:
      key.y = reader.read_bstr();
      break;  // y
    default:
      reader.skip();
      break;  // skip unknown labels
    }
  }
  return key;
}

// Parse authenticator data (binary format per WebAuthn spec)
// Returns false on failure, fills out parameters on success
struct AuthDataParsed {
  std::vector<unsigned char> rp_id_hash;  // 32 bytes
  uint8_t flags;
  uint32_t sign_count;
  // Only present if AT flag is set (registration)
  std::string credential_id_b64url;
  std::vector<unsigned char> public_key_xy;  // x || y (64 bytes for P-256)
  bool has_attested_credential = false;
};

static void parse_auth_data(const std::vector<unsigned char>& auth_data, AuthDataParsed& out) {
  if (auth_data.size() < 37) {
    throw std::runtime_error("Auth data too short: " + std::to_string(auth_data.size()));
  }

  out.rp_id_hash.assign(auth_data.begin(), auth_data.begin() + 32);
  out.flags = auth_data[32];
  out.sign_count =
    (static_cast<uint32_t>(auth_data[33]) << 24) | (static_cast<uint32_t>(auth_data[34]) << 16) |
    (static_cast<uint32_t>(auth_data[35]) << 8) | static_cast<uint32_t>(auth_data[36]);

  // AT flag (bit 6) = attested credential data present
  if (out.flags & 0x40) {
    out.has_attested_credential = true;

    if (auth_data.size() < 55) {
      throw std::runtime_error("Auth data too short for attested credential");
    }

    // Skip aaguid (16 bytes at offset 37)
    // Credential ID length (2 bytes big-endian at offset 53)
    uint16_t cred_id_len =
      (static_cast<uint16_t>(auth_data[53]) << 8) | static_cast<uint16_t>(auth_data[54]);

    size_t cred_id_start = 55;
    if (auth_data.size() < cred_id_start + cred_id_len) {
      throw std::runtime_error("Auth data too short for credential ID");
    }

    out.credential_id_b64url = base64url_encode(auth_data.data() + cred_id_start, cred_id_len);

    // COSE key follows credential ID — parse with manual CBOR reader
    // (nlohmann::json::from_cbor doesn't reliably handle integer map keys)
    size_t cose_start = cred_id_start + cred_id_len;
    std::vector<unsigned char> cose_bytes(auth_data.begin() + cose_start, auth_data.end());

    auto cose = parse_cose_ec_key(cose_bytes);

    if (cose.kty != 2 || cose.alg != -7) {
      throw std::runtime_error(
        "Unsupported COSE key type: kty=" + std::to_string(cose.kty) +
        ", alg=" + std::to_string(cose.alg) + " (only ES256 supported)");
    }
    if (cose.crv != 1) {
      throw std::runtime_error("Unsupported EC curve: " + std::to_string(cose.crv));
    }
    if (cose.x.size() != 32 || cose.y.size() != 32) {
      throw std::runtime_error(
        "Invalid EC key coordinate size: x=" + std::to_string(cose.x.size()) +
        ", y=" + std::to_string(cose.y.size()));
    }

    out.public_key_xy.resize(64);
    std::memcpy(out.public_key_xy.data(), cose.x.data(), 32);
    std::memcpy(out.public_key_xy.data() + 32, cose.y.data(), 32);
  }
}

// Verify ES256 (ECDSA P-256 with SHA-256) signature
static bool verify_es256(
  const std::vector<unsigned char>& public_key_xy,
  const std::vector<unsigned char>& signed_data,
  const std::vector<unsigned char>& signature) {
  if (public_key_xy.size() != 64) return false;

  // Create EC key from x,y coordinates using OpenSSL 3.0 EVP API
  // Build uncompressed point: 0x04 || x || y
  std::vector<unsigned char> uncompressed(65);
  uncompressed[0] = 0x04;
  std::memcpy(uncompressed.data() + 1, public_key_xy.data(), 64);

  OSSL_PARAM_BLD* param_bld = OSSL_PARAM_BLD_new();
  if (!param_bld) return false;

  OSSL_PARAM_BLD_push_utf8_string(param_bld, OSSL_PKEY_PARAM_GROUP_NAME, "prime256v1", 0);
  OSSL_PARAM_BLD_push_octet_string(
    param_bld, OSSL_PKEY_PARAM_PUB_KEY, uncompressed.data(), uncompressed.size());

  OSSL_PARAM* params = OSSL_PARAM_BLD_to_param(param_bld);
  OSSL_PARAM_BLD_free(param_bld);
  if (!params) return false;

  EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
  if (!pctx) {
    OSSL_PARAM_free(params);
    return false;
  }

  EVP_PKEY* pkey = nullptr;
  if (
    EVP_PKEY_fromdata_init(pctx) <= 0 ||
    EVP_PKEY_fromdata(pctx, &pkey, EVP_PKEY_PUBLIC_KEY, params) <= 0) {
    LOG_ERROR_N("webauthn", nullptr, "Failed to create EC key from point");
    ERR_print_errors_fp(stderr);
    EVP_PKEY_CTX_free(pctx);
    OSSL_PARAM_free(params);
    return false;
  }

  EVP_PKEY_CTX_free(pctx);
  OSSL_PARAM_free(params);

  // Verify signature using EVP_DigestVerify
  EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
  bool valid = false;

  if (EVP_DigestVerifyInit(md_ctx, nullptr, EVP_sha256(), nullptr, pkey) == 1) {
    // WebAuthn signatures are DER-encoded ECDSA signatures
    int rc = EVP_DigestVerify(
      md_ctx, signature.data(), signature.size(), signed_data.data(), signed_data.size());
    valid = (rc == 1);
    if (!valid) {
      LOG_DEBUG_N(
        "webauthn", nullptr, "Signature verification failed (rc=" + std::to_string(rc) + ")");
    }
  }

  EVP_MD_CTX_free(md_ctx);
  EVP_PKEY_free(pkey);
  return valid;
}

// Verify clientDataJSON common fields
static bool verify_client_data(
  const std::string& client_data_json_str,
  const std::string& expected_type,
  const std::string& expected_challenge,
  const std::string& expected_origin) {
  try {
    auto client_data = json::parse(client_data_json_str);

    std::string type = client_data.at("type");
    if (type != expected_type) {
      LOG_WARN_N("webauthn", nullptr, "Unexpected type: " + type);
      return false;
    }

    std::string challenge = client_data.at("challenge");
    if (challenge != expected_challenge) {
      LOG_WARN_N("webauthn", nullptr, "Challenge mismatch");
      return false;
    }

    std::string origin = client_data.at("origin");
    if (origin != expected_origin) {
      // Allow localhost with any port for development
      bool both_localhost = false;
      if (
        origin.find("://localhost") != std::string::npos &&
        expected_origin.find("://localhost") != std::string::npos) {
        // Extract scheme from both - must match
        auto orig_scheme = origin.substr(0, origin.find("://"));
        auto exp_scheme = expected_origin.substr(0, expected_origin.find("://"));
        both_localhost = (orig_scheme == exp_scheme);
      }
      if (!both_localhost) {
        LOG_WARN_N(
          "webauthn",
          nullptr,
          "Origin mismatch: got '" + origin + "', expected '" + expected_origin + "'");
        return false;
      }
    }

    return true;
  } catch (const std::exception& e) {
    LOG_ERROR_N("webauthn", nullptr, std::string("Failed to parse clientDataJSON: ") + e.what());
    return false;
  }
}

// --- Public API ---

std::optional<ParsedCredential> verify_registration(
  const std::string& attestation_object_b64,
  const std::string& client_data_json_b64,
  const std::string& expected_challenge,
  const std::string& expected_origin,
  const std::string& rp_id) {
  // 1. Decode and verify clientDataJSON
  auto client_data_bytes = base64url_decode(client_data_json_b64);
  if (client_data_bytes.empty()) {
    throw std::runtime_error("Failed to decode clientDataJSON from base64url");
  }
  std::string client_data_str(client_data_bytes.begin(), client_data_bytes.end());

  if (!verify_client_data(
        client_data_str, "webauthn.create", expected_challenge, expected_origin)) {
    // verify_client_data logs the specific reason to stderr
    throw std::runtime_error("clientDataJSON verification failed (check server logs for details)");
  }

  // 2. Decode attestation object (CBOR)
  auto att_bytes = base64url_decode(attestation_object_b64);
  if (att_bytes.empty()) {
    throw std::runtime_error("Failed to decode attestation object from base64url");
  }

  auto att_obj = json::from_cbor(att_bytes);

  // 3. Extract authData
  if (!att_obj.contains("authData") || !att_obj.at("authData").is_binary()) {
    throw std::runtime_error("Attestation object missing binary authData");
  }
  std::vector<unsigned char> auth_data;
  auto& bin = att_obj.at("authData").get_binary();
  auth_data.assign(bin.begin(), bin.end());

  // 4. Parse authenticator data
  AuthDataParsed parsed;
  parse_auth_data(auth_data, parsed);  // throws on failure

  // 5. Verify RP ID hash
  auto expected_rp_hash = sha256(rp_id.data(), rp_id.size());
  if (parsed.rp_id_hash != expected_rp_hash) {
    throw std::runtime_error("RP ID hash mismatch (rp_id='" + rp_id + "')");
  }

  // 6. Verify UP (user present) flag is set
  if (!(parsed.flags & 0x01)) {
    throw std::runtime_error("User presence flag not set");
  }

  // 7. Verify attested credential data was present
  if (!parsed.has_attested_credential) {
    throw std::runtime_error("No attested credential data in registration response");
  }

  // 8. We use attestation "none" so we don't verify the attestation statement

  ParsedCredential result;
  result.credential_id = parsed.credential_id_b64url;
  result.public_key = parsed.public_key_xy;
  result.sign_count = parsed.sign_count;
  return result;
}

std::optional<uint32_t> verify_authentication(
  const std::string& auth_data_b64,
  const std::string& client_data_json_b64,
  const std::string& signature_b64,
  const std::vector<unsigned char>& stored_public_key,
  uint32_t stored_sign_count,
  const std::string& expected_challenge,
  const std::string& expected_origin,
  const std::string& rp_id) {
  // 1. Decode and verify clientDataJSON
  auto client_data_bytes = base64url_decode(client_data_json_b64);
  if (client_data_bytes.empty()) {
    throw std::runtime_error("Failed to decode clientDataJSON");
  }
  std::string client_data_str(client_data_bytes.begin(), client_data_bytes.end());

  if (!verify_client_data(client_data_str, "webauthn.get", expected_challenge, expected_origin)) {
    throw std::runtime_error("clientDataJSON verification failed");
  }

  // 2. Decode authenticator data
  auto auth_data = base64url_decode(auth_data_b64);
  if (auth_data.empty()) {
    throw std::runtime_error("Failed to decode authenticator data");
  }

  // 3. Parse authenticator data
  AuthDataParsed parsed;
  parse_auth_data(auth_data, parsed);  // throws on failure

  // 4. Verify RP ID hash
  auto expected_rp_hash = sha256(rp_id.data(), rp_id.size());
  if (parsed.rp_id_hash != expected_rp_hash) {
    throw std::runtime_error("RP ID hash mismatch (rp_id='" + rp_id + "')");
  }

  // 5. Verify UP flag
  if (!(parsed.flags & 0x01)) {
    throw std::runtime_error("User presence flag not set");
  }

  // 6. Verify sign count (if authenticator supports it)
  if (parsed.sign_count > 0 || stored_sign_count > 0) {
    if (parsed.sign_count <= stored_sign_count) {
      throw std::runtime_error("Sign count not incremented (possible cloned authenticator)");
    }
  }

  // 7. Compute signed data: authenticatorData || SHA-256(clientDataJSON)
  auto client_data_hash = sha256(client_data_bytes.data(), client_data_bytes.size());
  std::vector<unsigned char> signed_data;
  signed_data.insert(signed_data.end(), auth_data.begin(), auth_data.end());
  signed_data.insert(signed_data.end(), client_data_hash.begin(), client_data_hash.end());

  // 8. Verify signature
  auto sig_bytes = base64url_decode(signature_b64);
  if (sig_bytes.empty()) {
    throw std::runtime_error("Failed to decode signature");
  }

  if (!verify_es256(stored_public_key, signed_data, sig_bytes)) {
    throw std::runtime_error("ES256 signature verification failed");
  }

  return parsed.sign_count;
}

// --- PKI signature verification ---

bool verify_pki_signature(
  const std::string& public_key_spki_b64url,
  const std::string& challenge,
  const std::string& signature_b64url) {
  // 1. Decode SPKI public key
  auto spki_bytes = base64url_decode(public_key_spki_b64url);
  if (spki_bytes.empty()) return false;

  // 2. Parse SPKI DER into EVP_PKEY
  const unsigned char* p = spki_bytes.data();
  EVP_PKEY* pkey = d2i_PUBKEY(nullptr, &p, static_cast<long>(spki_bytes.size()));
  if (!pkey) {
    LOG_ERROR_N("pki", nullptr, "Failed to parse SPKI public key");
    ERR_print_errors_fp(stderr);
    return false;
  }

  // 3. Verify it's an EC key on P-256
  if (EVP_PKEY_id(pkey) != EVP_PKEY_EC) {
    LOG_ERROR_N("pki", nullptr, "Key is not EC");
    EVP_PKEY_free(pkey);
    return false;
  }

  // 4. Web Crypto ECDSA uses IEEE P1363 (raw r||s) format, not DER.
  // Try DER first, then raw r||s.
  auto sig_bytes = base64url_decode(signature_b64url);
  if (sig_bytes.empty()) {
    EVP_PKEY_free(pkey);
    return false;
  }

  // Prepare challenge data
  auto challenge_bytes = reinterpret_cast<const unsigned char*>(challenge.data());
  auto challenge_len = challenge.size();

  EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
  bool valid = false;

  if (EVP_DigestVerifyInit(md_ctx, nullptr, EVP_sha256(), nullptr, pkey) == 1) {
    // First try the signature as-is (DER format)
    int rc =
      EVP_DigestVerify(md_ctx, sig_bytes.data(), sig_bytes.size(), challenge_bytes, challenge_len);
    if (rc == 1) {
      valid = true;
    } else if (sig_bytes.size() == 64) {
      // Web Crypto uses IEEE P1363 format (raw r||s, 32 bytes each for P-256)
      // Convert to DER
      ECDSA_SIG* ec_sig = ECDSA_SIG_new();
      BIGNUM* r = BN_bin2bn(sig_bytes.data(), 32, nullptr);
      BIGNUM* s = BN_bin2bn(sig_bytes.data() + 32, 32, nullptr);
      ECDSA_SIG_set0(ec_sig, r, s);

      unsigned char* der_sig = nullptr;
      int der_len = i2d_ECDSA_SIG(ec_sig, &der_sig);
      ECDSA_SIG_free(ec_sig);

      if (der_len > 0 && der_sig) {
        // Re-init and verify with DER-encoded signature
        EVP_MD_CTX_reset(md_ctx);
        if (EVP_DigestVerifyInit(md_ctx, nullptr, EVP_sha256(), nullptr, pkey) == 1) {
          rc = EVP_DigestVerify(md_ctx, der_sig, der_len, challenge_bytes, challenge_len);
          valid = (rc == 1);
        }
        OPENSSL_free(der_sig);
      }
    }
  }

  EVP_MD_CTX_free(md_ctx);
  EVP_PKEY_free(pkey);
  return valid;
}

// --- Recovery keys ---

std::string hash_recovery_key(const std::string& key) {
  auto hash = sha256(key.data(), key.size());
  std::string hex;
  hex.reserve(64);
  for (unsigned char b : hash) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02x", b);
    hex += buf;
  }
  return hex;
}

std::pair<std::vector<std::string>, std::vector<std::string>> generate_recovery_keys() {
  static const char ALPHANUM[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  std::vector<std::string> plaintext, hashes;

  for (int i = 0; i < 8; i++) {
    unsigned char buf[20];
    RAND_bytes(buf, sizeof(buf));

    // Format: XXXX-XXXX-XXXX-XXXX-XXXX (20 chars + 4 dashes)
    std::string key;
    for (int j = 0; j < 20; j++) {
      if (j > 0 && j % 4 == 0) key += '-';
      key += ALPHANUM[buf[j] % 36];
    }

    plaintext.push_back(key);
    hashes.push_back(hash_recovery_key(key));
  }

  return {plaintext, hashes};
}

}  // namespace webauthn
