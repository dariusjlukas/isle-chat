#include <gtest/gtest.h>
#include "config.h"
#include <map>
#include <vector>

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        env_names_ = {"POSTGRES_HOST", "POSTGRES_PORT", "POSTGRES_USER",
                      "POSTGRES_PASSWORD", "POSTGRES_DB", "BACKEND_PORT",
                      "SESSION_EXPIRY_HOURS", "PUBLIC_URL", "UPLOAD_DIR",
                      "MAX_FILE_SIZE"};
        for (const auto& name : env_names_) {
            const char* val = std::getenv(name.c_str());
            if (val) saved_[name] = val;
        }
    }

    void TearDown() override {
        for (const auto& name : env_names_) {
            auto it = saved_.find(name);
            if (it != saved_.end()) {
                setenv(name.c_str(), it->second.c_str(), 1);
            } else {
                unsetenv(name.c_str());
            }
        }
    }

    void clearAllEnv() {
        for (const auto& name : env_names_) {
            unsetenv(name.c_str());
        }
    }

    std::vector<std::string> env_names_;
    std::map<std::string, std::string> saved_;
};

TEST_F(ConfigTest, DefaultValues) {
    clearAllEnv();
    auto c = Config::from_env();

    EXPECT_EQ(c.pg_host, "localhost");
    EXPECT_EQ(c.pg_port, "5432");
    EXPECT_EQ(c.pg_user, "chatapp");
    EXPECT_EQ(c.pg_password, "changeme_in_production");
    EXPECT_EQ(c.pg_dbname, "chatapp");
    EXPECT_EQ(c.server_port, 9001);
    EXPECT_EQ(c.session_expiry_hours, 168);
    EXPECT_EQ(c.public_url, "");
    EXPECT_EQ(c.upload_dir, "/data/uploads");
    EXPECT_EQ(c.max_file_size, 1073741824);
}

TEST_F(ConfigTest, EnvironmentOverrides) {
    setenv("POSTGRES_HOST", "dbhost", 1);
    setenv("POSTGRES_PORT", "5433", 1);
    setenv("POSTGRES_USER", "myuser", 1);
    setenv("POSTGRES_PASSWORD", "secret", 1);
    setenv("POSTGRES_DB", "mydb", 1);
    setenv("BACKEND_PORT", "8080", 1);
    setenv("SESSION_EXPIRY_HOURS", "24", 1);
    setenv("PUBLIC_URL", "https://example.com", 1);
    setenv("UPLOAD_DIR", "/tmp/uploads", 1);
    setenv("MAX_FILE_SIZE", "1048576", 1);

    auto c = Config::from_env();

    EXPECT_EQ(c.pg_host, "dbhost");
    EXPECT_EQ(c.pg_port, "5433");
    EXPECT_EQ(c.pg_user, "myuser");
    EXPECT_EQ(c.pg_password, "secret");
    EXPECT_EQ(c.pg_dbname, "mydb");
    EXPECT_EQ(c.server_port, 8080);
    EXPECT_EQ(c.session_expiry_hours, 24);
    EXPECT_EQ(c.public_url, "https://example.com");
    EXPECT_EQ(c.upload_dir, "/tmp/uploads");
    EXPECT_EQ(c.max_file_size, 1048576);
}

TEST_F(ConfigTest, ConnectionString) {
    setenv("POSTGRES_HOST", "myhost", 1);
    setenv("POSTGRES_PORT", "5433", 1);
    setenv("POSTGRES_USER", "myuser", 1);
    setenv("POSTGRES_PASSWORD", "mypass", 1);
    setenv("POSTGRES_DB", "mydb", 1);

    auto c = Config::from_env();
    std::string expected = "host=myhost port=5433 dbname=mydb user=myuser password=mypass";
    EXPECT_EQ(c.pg_connection_string(), expected);
}

TEST_F(ConfigTest, InvalidPortThrows) {
    setenv("BACKEND_PORT", "abc", 1);
    EXPECT_THROW(Config::from_env(), std::invalid_argument);
}
