#pragma once
#include <cstdint>
#include <cstdlib>
#include <string>

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
    c.server_port = std::stoi(env("BACKEND_PORT", "9001"));
    c.session_expiry_hours = std::stoi(env("SESSION_EXPIRY_HOURS", "168"));
    c.public_url = env("PUBLIC_URL", "");
    c.upload_dir = env("UPLOAD_DIR", "/data/uploads");
    c.max_file_size = std::stoll(env("MAX_FILE_SIZE", "0"));
    c.ssl_cert_path = env("SSL_CERT_PATH", "");
    c.ssl_key_path = env("SSL_KEY_PATH", "");
    c.db_pool_size = std::stoi(env("DB_POOL_SIZE", "10"));
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
};
