#pragma once
#include <openssl/rand.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

#include "logging/logger.h"

struct Config {
  std::string pg_host;
  std::string pg_port;
  std::string pg_user;
  std::string pg_password;
  std::string pg_dbname;
  int server_port;
  int session_expiry_hours;
  std::string public_url;
  std::string upload_dir;
  int64_t max_file_size;
  std::string ssl_cert_path;
  std::string ssl_key_path;
  std::string webauthn_rp_id;
  std::string webauthn_rp_name;
  std::string webauthn_origin;
  int db_pool_size;
  int db_thread_pool_size;
  bool enable_sqitch_only;
  // Redis URL for cross-instance WS broadcast (e.g. "redis://host:6379").
  // Empty = local-only mode; no Redis publisher/subscriber is started.
  std::string redis_url;
  // Stable per-process identifier used to filter self-published messages
  // when the local broadcast loops back through Redis. Defaults to a random
  // UUID v4 generated at boot.
  std::string instance_id;

  bool has_ssl() const {
    return !ssl_cert_path.empty() && !ssl_key_path.empty();
  }

  static Config from_env() {
    Config c;
    c.pg_host = env("POSTGRES_HOST", "localhost");
    c.pg_port = env("POSTGRES_PORT", "5432");
    c.pg_user = env("POSTGRES_USER", "chatapp");
    c.pg_password = env("POSTGRES_PASSWORD", "changeme_in_production");
    c.pg_dbname = env("POSTGRES_DB", "chatapp");
    c.server_port = parse_int_env("BACKEND_PORT", "9001");
    c.session_expiry_hours = parse_int_env("SESSION_EXPIRY_HOURS", "168");
    c.public_url = env("PUBLIC_URL", "");
    c.upload_dir = env("UPLOAD_DIR", "/data/uploads");
    c.max_file_size = parse_i64_env("MAX_FILE_SIZE", "0");
    c.ssl_cert_path = env("SSL_CERT_PATH", "");
    c.ssl_key_path = env("SSL_KEY_PATH", "");
    c.db_pool_size = parse_int_env("DB_POOL_SIZE", "10");
    c.db_thread_pool_size = parse_int_env("DB_THREAD_POOL_SIZE", "32");
    {
      std::string v = env("ENABLE_SQITCH_ONLY", "0");
      c.enable_sqitch_only = (v == "1" || v == "true");
    }
    c.redis_url = env("REDIS_URL", "");
    {
      const char* iid = std::getenv("INSTANCE_ID");
      if (iid && *iid) {
        c.instance_id = iid;
      } else {
        unsigned char buf[16];
        if (RAND_bytes(buf, sizeof(buf)) != 1) {
          throw std::runtime_error("RAND_bytes failed while generating INSTANCE_ID");
        }
        // Set RFC 4122 v4 variant bits.
        buf[6] = static_cast<unsigned char>((buf[6] & 0x0f) | 0x40);
        buf[8] = static_cast<unsigned char>((buf[8] & 0x3f) | 0x80);
        char out[37];
        std::snprintf(
          out,
          sizeof(out),
          "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
          buf[0],
          buf[1],
          buf[2],
          buf[3],
          buf[4],
          buf[5],
          buf[6],
          buf[7],
          buf[8],
          buf[9],
          buf[10],
          buf[11],
          buf[12],
          buf[13],
          buf[14],
          buf[15]);
        c.instance_id = out;
      }
    }
    c.webauthn_rp_name = env("WEBAUTHN_RP_NAME", "EnclaveStation");

    // Derive WebAuthn RP ID and origin from PUBLIC_URL if not explicitly set
    std::string rp_id_env = env("WEBAUTHN_RP_ID", "");
    if (!rp_id_env.empty()) {
      c.webauthn_rp_id = rp_id_env;
    } else if (!c.public_url.empty()) {
      // Extract hostname from PUBLIC_URL (e.g. "https://chat.example.com" -> "chat.example.com")
      std::string url = c.public_url;
      auto scheme_end = url.find("://");
      if (scheme_end != std::string::npos) url = url.substr(scheme_end + 3);
      auto port_or_path = url.find_first_of(":/");
      if (port_or_path != std::string::npos) url = url.substr(0, port_or_path);
      c.webauthn_rp_id = url;
    } else {
      c.webauthn_rp_id = "localhost";
    }

    // Origin for WebAuthn verification
    if (!c.public_url.empty()) {
      c.webauthn_origin = c.public_url;
      // Remove trailing slash
      while (!c.webauthn_origin.empty() && c.webauthn_origin.back() == '/')
        c.webauthn_origin.pop_back();
    } else {
      c.webauthn_origin = "http://localhost:" + std::to_string(c.server_port);
    }

    return c;
  }

  std::string pg_connection_string() const {
    return "host=" + pg_host + " port=" + pg_port + " dbname=" + pg_dbname + " user=" + pg_user +
           " password=" + pg_password;
  }

private:
  static std::string env(const char* name, const char* fallback) {
    const char* val = std::getenv(name);
    return val ? val : fallback;
  }

  // Fail-fast env parsing: throw std::invalid_argument on malformed numeric
  // env vars. main() does not catch this — the process dies with a clear
  // terminate message, which is the desired behavior at boot.
  static int parse_int_env(const char* name, const char* fallback) {
    std::string value = env(name, fallback);
    size_t pos = 0;
    int v = 0;
    try {
      v = std::stoi(value, &pos);
    } catch (const std::exception&) {
      LOG_ERROR_N(
        "config",
        nullptr,
        std::string("FATAL: Invalid env var ") + name + "=" + value + " (expected integer)");
      throw std::invalid_argument(std::string("Invalid env var ") + name + "=" + value);
    }
    if (pos != value.size()) {
      LOG_ERROR_N(
        "config",
        nullptr,
        std::string("FATAL: Invalid env var ") + name + "=" + value +
          " (trailing non-numeric characters)");
      throw std::invalid_argument(std::string("Invalid env var ") + name + "=" + value);
    }
    return v;
  }

  static int64_t parse_i64_env(const char* name, const char* fallback) {
    std::string value = env(name, fallback);
    size_t pos = 0;
    int64_t v = 0;
    try {
      v = std::stoll(value, &pos);
    } catch (const std::exception&) {
      LOG_ERROR_N(
        "config",
        nullptr,
        std::string("FATAL: Invalid env var ") + name + "=" + value + " (expected 64-bit integer)");
      throw std::invalid_argument(std::string("Invalid env var ") + name + "=" + value);
    }
    if (pos != value.size()) {
      LOG_ERROR_N(
        "config",
        nullptr,
        std::string("FATAL: Invalid env var ") + name + "=" + value +
          " (trailing non-numeric characters)");
      throw std::invalid_argument(std::string("Invalid env var ") + name + "=" + value);
    }
    return v;
  }
};
