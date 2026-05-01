#include "auth/totp.h"
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace totp {

static const char BASE32_ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

std::string base32_encode(const std::string& data) {
  std::string result;
  int bits = 0;
  int buffer = 0;

  for (unsigned char c : data) {
    buffer = (buffer << 8) | c;
    bits += 8;
    while (bits >= 5) {
      bits -= 5;
      result += BASE32_ALPHABET[(buffer >> bits) & 0x1F];
    }
  }
  if (bits > 0) {
    result += BASE32_ALPHABET[(buffer << (5 - bits)) & 0x1F];
  }
  return result;
}

std::string base32_decode(const std::string& encoded) {
  std::string result;
  int bits = 0;
  int buffer = 0;

  for (char c : encoded) {
    if (c == '=' || c == ' ') continue;
    int val;
    if (c >= 'A' && c <= 'Z')
      val = c - 'A';
    else if (c >= 'a' && c <= 'z')
      val = c - 'a';
    else if (c >= '2' && c <= '7')
      val = c - '2' + 26;
    else
      continue;

    buffer = (buffer << 5) | val;
    bits += 5;
    if (bits >= 8) {
      bits -= 8;
      result += static_cast<char>((buffer >> bits) & 0xFF);
    }
  }
  return result;
}

std::string generate_secret() {
  unsigned char raw[20];
  if (RAND_bytes(raw, sizeof(raw)) != 1) {
    throw std::runtime_error("Failed to generate random bytes for TOTP secret");
  }
  return base32_encode(std::string(reinterpret_cast<char*>(raw), sizeof(raw)));
}

static uint32_t dynamic_truncate(const unsigned char* hmac_result) {
  int offset = hmac_result[19] & 0x0F;
  uint32_t code = ((hmac_result[offset] & 0x7F) << 24) | ((hmac_result[offset + 1] & 0xFF) << 16) |
                  ((hmac_result[offset + 2] & 0xFF) << 8) | (hmac_result[offset + 3] & 0xFF);
  return code % 1000000;
}

std::string compute_code(const std::string& base32_secret, uint64_t time_step) {
  std::string key = base32_decode(base32_secret);

  // Convert time_step to big-endian 8 bytes
  unsigned char msg[8];
  for (int i = 7; i >= 0; i--) {
    msg[i] = time_step & 0xFF;
    time_step >>= 8;
  }

  unsigned char hmac_result[20];
  unsigned int hmac_len = 0;
  HMAC(
    EVP_sha1(), key.data(), static_cast<int>(key.size()), msg, sizeof(msg), hmac_result, &hmac_len);

  uint32_t otp = dynamic_truncate(hmac_result);

  std::ostringstream oss;
  oss << std::setfill('0') << std::setw(6) << otp;
  return oss.str();
}

std::optional<uint64_t> verify_code(
  const std::string& base32_secret,
  const std::string& code,
  std::optional<uint64_t> last_used_step,
  int window) {
  uint64_t current_step = static_cast<uint64_t>(std::time(nullptr)) / 30;

  for (int i = -window; i <= window; i++) {
    uint64_t step = current_step + i;
    // Replay prevention: skip steps that are <= the last-consumed step.
    if (last_used_step.has_value() && step <= *last_used_step) {
      continue;
    }
    if (compute_code(base32_secret, step) == code) {
      return step;
    }
  }
  return std::nullopt;
}

static std::string url_encode(const std::string& str) {
  std::ostringstream oss;
  for (unsigned char c : str) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      oss << c;
    } else {
      oss << '%' << std::setfill('0') << std::setw(2) << std::uppercase << std::hex
          << static_cast<int>(c);
    }
  }
  return oss.str();
}

std::string build_uri(
  const std::string& base32_secret, const std::string& username, const std::string& issuer) {
  std::string label = url_encode(issuer) + ":" + url_encode(username);
  return "otpauth://totp/" + label + "?secret=" + base32_secret + "&issuer=" + url_encode(issuer) +
         "&algorithm=SHA1&digits=6&period=30";
}

}  // namespace totp
