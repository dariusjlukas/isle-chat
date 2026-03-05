#pragma once
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
        c.max_file_size = std::stoll(env("MAX_FILE_SIZE", "1073741824"));
        c.ssl_cert_path = env("SSL_CERT_PATH", "");
        c.ssl_key_path = env("SSL_KEY_PATH", "");
        return c;
    }

    std::string pg_connection_string() const {
        return "host=" + pg_host + " port=" + pg_port +
               " dbname=" + pg_dbname + " user=" + pg_user +
               " password=" + pg_password;
    }

private:
    static std::string env(const char* name, const char* fallback) {
        const char* val = std::getenv(name);
        return val ? val : fallback;
    }
};
