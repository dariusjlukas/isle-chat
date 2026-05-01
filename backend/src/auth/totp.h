#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace totp {

// Generate a random 20-byte secret, returned as base32-encoded string
std::string generate_secret();

// Compute a 6-digit TOTP code for the given base32 secret and time step
std::string compute_code(const std::string& base32_secret, uint64_t time_step);

// Verify a TOTP code against a base32 secret with ±window time steps.
// Returns the matching step on success, or std::nullopt on failure.
// If last_used_step is provided, any step <= last_used_step is skipped (replay prevention).
std::optional<uint64_t> verify_code(
  const std::string& base32_secret,
  const std::string& code,
  std::optional<uint64_t> last_used_step = std::nullopt,
  int window = 1);

// Build an otpauth:// URI for QR code generation
std::string build_uri(
  const std::string& base32_secret, const std::string& username, const std::string& issuer);

// Base32 encode/decode (RFC 4648)
std::string base32_encode(const std::string& data);
std::string base32_decode(const std::string& encoded);

}  // namespace totp
