#include "db/database.h"
#include <iostream>
#include <random>
#include <sstream>
#include <iomanip>

Database::Database(const std::string& connection_string)
    : conn_string_(connection_string) {
    conn_ = std::make_unique<pqxx::connection>(conn_string_);
    std::cout << "[DB] Connected to PostgreSQL" << std::endl;
}

pqxx::connection& Database::get_conn() {
    if (!conn_ || !conn_->is_open()) {
        conn_ = std::make_unique<pqxx::connection>(conn_string_);
    }
    return *conn_;
}

static std::string random_hex(int bytes) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, 255);
    std::ostringstream ss;
    for (int i = 0; i < bytes; i++)
        ss << std::hex << std::setfill('0') << std::setw(2) << dist(gen);
    return ss.str();
}

void Database::run_migrations() {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());

    txn.exec(R"SQL(
        CREATE EXTENSION IF NOT EXISTS "pgcrypto";

        CREATE TABLE IF NOT EXISTS users (
            id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            username VARCHAR(50) UNIQUE NOT NULL,
            display_name VARCHAR(100) NOT NULL,
            public_key TEXT NOT NULL,
            role VARCHAR(20) DEFAULT 'user',
            is_online BOOLEAN DEFAULT FALSE,
            last_seen TIMESTAMPTZ,
            created_at TIMESTAMPTZ DEFAULT NOW()
        );

        CREATE TABLE IF NOT EXISTS channels (
            id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            name VARCHAR(100),
            description TEXT,
            is_direct BOOLEAN DEFAULT FALSE,
            created_by UUID REFERENCES users(id),
            created_at TIMESTAMPTZ DEFAULT NOW()
        );

        CREATE TABLE IF NOT EXISTS channel_members (
            channel_id UUID REFERENCES channels(id) ON DELETE CASCADE,
            user_id UUID REFERENCES users(id) ON DELETE CASCADE,
            joined_at TIMESTAMPTZ DEFAULT NOW(),
            PRIMARY KEY (channel_id, user_id)
        );

        CREATE TABLE IF NOT EXISTS messages (
            id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            channel_id UUID REFERENCES channels(id) ON DELETE CASCADE,
            user_id UUID REFERENCES users(id),
            content TEXT NOT NULL,
            created_at TIMESTAMPTZ DEFAULT NOW()
        );

        CREATE INDEX IF NOT EXISTS idx_messages_channel_time ON messages(channel_id, created_at);

        CREATE TABLE IF NOT EXISTS invite_tokens (
            id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            token VARCHAR(64) UNIQUE NOT NULL,
            created_by UUID REFERENCES users(id),
            used_by UUID REFERENCES users(id),
            expires_at TIMESTAMPTZ NOT NULL,
            created_at TIMESTAMPTZ DEFAULT NOW()
        );

        CREATE TABLE IF NOT EXISTS join_requests (
            id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            username VARCHAR(50) NOT NULL,
            display_name VARCHAR(100) NOT NULL,
            public_key TEXT NOT NULL,
            status VARCHAR(20) DEFAULT 'pending',
            reviewed_by UUID REFERENCES users(id),
            created_at TIMESTAMPTZ DEFAULT NOW()
        );

        CREATE TABLE IF NOT EXISTS sessions (
            id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            user_id UUID REFERENCES users(id) ON DELETE CASCADE,
            token VARCHAR(128) UNIQUE NOT NULL,
            expires_at TIMESTAMPTZ NOT NULL,
            created_at TIMESTAMPTZ DEFAULT NOW()
        );

        CREATE TABLE IF NOT EXISTS auth_challenges (
            public_key TEXT PRIMARY KEY,
            challenge VARCHAR(128) NOT NULL,
            created_at TIMESTAMPTZ DEFAULT NOW()
        );

        CREATE TABLE IF NOT EXISTS user_keys (
            id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
            public_key TEXT NOT NULL UNIQUE,
            device_name VARCHAR(100) DEFAULT 'Primary Device',
            created_at TIMESTAMPTZ DEFAULT NOW()
        );
        CREATE INDEX IF NOT EXISTS idx_user_keys_public_key ON user_keys(public_key);

        INSERT INTO user_keys (user_id, public_key, device_name)
        SELECT id, public_key, 'Primary Device' FROM users
        WHERE public_key IS NOT NULL AND public_key != ''
          AND NOT EXISTS (
            SELECT 1 FROM user_keys WHERE user_keys.public_key = users.public_key
          );

        CREATE TABLE IF NOT EXISTS device_tokens (
            id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
            token VARCHAR(64) UNIQUE NOT NULL,
            expires_at TIMESTAMPTZ NOT NULL,
            used BOOLEAN DEFAULT FALSE,
            created_at TIMESTAMPTZ DEFAULT NOW()
        );
    )SQL");

    // Add bio and status columns to users
    txn.exec(R"SQL(
        ALTER TABLE users ADD COLUMN IF NOT EXISTS bio TEXT DEFAULT '';
        ALTER TABLE users ADD COLUMN IF NOT EXISTS status VARCHAR(100) DEFAULT '';
    )SQL");

    // Add edit/delete support for messages
    txn.exec(R"SQL(
        ALTER TABLE messages ADD COLUMN IF NOT EXISTS edited_at TIMESTAMPTZ;
        ALTER TABLE messages ADD COLUMN IF NOT EXISTS is_deleted BOOLEAN DEFAULT FALSE;
    )SQL");

    // Channel permissions
    txn.exec(R"SQL(
        ALTER TABLE channels ADD COLUMN IF NOT EXISTS is_public BOOLEAN DEFAULT TRUE;
        ALTER TABLE channels ADD COLUMN IF NOT EXISTS default_role VARCHAR(20) DEFAULT 'write';
        ALTER TABLE channel_members ADD COLUMN IF NOT EXISTS role VARCHAR(20) DEFAULT 'write';

        UPDATE channel_members SET role = 'admin'
        WHERE role = 'write'
          AND EXISTS (
            SELECT 1 FROM channels c
            WHERE c.id = channel_members.channel_id
              AND c.created_by = channel_members.user_id
              AND c.is_direct = false
          );

        UPDATE channels SET default_role = 'read'
        WHERE name = 'general' AND is_direct = false AND default_role = 'write';
    )SQL");

    // File attachment support
    txn.exec(R"SQL(
        ALTER TABLE messages ADD COLUMN IF NOT EXISTS file_id TEXT;
        ALTER TABLE messages ADD COLUMN IF NOT EXISTS file_name TEXT;
        ALTER TABLE messages ADD COLUMN IF NOT EXISTS file_size BIGINT;
        ALTER TABLE messages ADD COLUMN IF NOT EXISTS file_type TEXT;
    )SQL");

    // Server settings (admin-configurable at runtime)
    txn.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS server_settings (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );
    )SQL");

    txn.commit();
    std::cout << "[DB] Migrations complete" << std::endl;
}

// --- Users ---

std::optional<User> Database::find_user_by_public_key(const std::string& public_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT u.id, u.username, u.display_name, u.public_key, u.role, u.is_online, "
        "u.last_seen::text, u.created_at::text, u.bio, u.status "
        "FROM users u JOIN user_keys uk ON u.id = uk.user_id "
        "WHERE uk.public_key = $1",
        public_key);
    txn.commit();
    if (r.empty()) return std::nullopt;
    return User{r[0][0].as<std::string>(), r[0][1].as<std::string>(),
                r[0][2].as<std::string>(), r[0][3].as<std::string>(),
                r[0][4].as<std::string>(), r[0][5].as<bool>(),
                r[0][6].is_null() ? "" : r[0][6].as<std::string>(),
                r[0][7].as<std::string>(),
                r[0][8].is_null() ? "" : r[0][8].as<std::string>(),
                r[0][9].is_null() ? "" : r[0][9].as<std::string>()};
}

std::optional<User> Database::find_user_by_id(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT id, username, display_name, public_key, role, is_online, "
        "last_seen::text, created_at::text, bio, status FROM users WHERE id = $1",
        id);
    txn.commit();
    if (r.empty()) return std::nullopt;
    return User{r[0][0].as<std::string>(), r[0][1].as<std::string>(),
                r[0][2].as<std::string>(), r[0][3].as<std::string>(),
                r[0][4].as<std::string>(), r[0][5].as<bool>(),
                r[0][6].is_null() ? "" : r[0][6].as<std::string>(),
                r[0][7].as<std::string>(),
                r[0][8].is_null() ? "" : r[0][8].as<std::string>(),
                r[0][9].is_null() ? "" : r[0][9].as<std::string>()};
}

User Database::create_user(const std::string& username, const std::string& display_name,
                           const std::string& public_key, const std::string& role) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "INSERT INTO users (username, display_name, public_key, role) "
        "VALUES ($1, $2, $3, $4) "
        "RETURNING id, username, display_name, public_key, role, is_online, "
        "last_seen::text, created_at::text, bio, status",
        username, display_name, public_key, role);

    // Also register in user_keys table
    txn.exec_params(
        "INSERT INTO user_keys (user_id, public_key, device_name) VALUES ($1, $2, 'Primary Device')",
        r[0][0].as<std::string>(), public_key);

    txn.commit();
    return User{r[0][0].as<std::string>(), r[0][1].as<std::string>(),
                r[0][2].as<std::string>(), r[0][3].as<std::string>(),
                r[0][4].as<std::string>(), r[0][5].as<bool>(),
                r[0][6].is_null() ? "" : r[0][6].as<std::string>(),
                r[0][7].as<std::string>(),
                r[0][8].is_null() ? "" : r[0][8].as<std::string>(),
                r[0][9].is_null() ? "" : r[0][9].as<std::string>()};
}

std::vector<User> Database::list_users() {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec(
        "SELECT id, username, display_name, public_key, role, is_online, "
        "last_seen::text, created_at::text, bio, status FROM users ORDER BY username");
    txn.commit();
    std::vector<User> users;
    for (const auto& row : r) {
        users.push_back({row[0].as<std::string>(), row[1].as<std::string>(),
                         row[2].as<std::string>(), row[3].as<std::string>(),
                         row[4].as<std::string>(), row[5].as<bool>(),
                         row[6].is_null() ? "" : row[6].as<std::string>(),
                         row[7].as<std::string>(),
                         row[8].is_null() ? "" : row[8].as<std::string>(),
                         row[9].is_null() ? "" : row[9].as<std::string>()});
    }
    return users;
}

void Database::set_user_online(const std::string& user_id, bool online) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    if (online) {
        txn.exec_params("UPDATE users SET is_online = true WHERE id = $1", user_id);
    } else {
        txn.exec_params("UPDATE users SET is_online = false, last_seen = NOW() WHERE id = $1", user_id);
    }
    txn.commit();
}

int Database::count_users() {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec("SELECT COUNT(*) FROM users");
    txn.commit();
    return r[0][0].as<int>();
}

User Database::update_user_profile(const std::string& user_id, const std::string& display_name,
                                    const std::string& bio, const std::string& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "UPDATE users SET display_name = $2, bio = $3, status = $4 WHERE id = $1 "
        "RETURNING id, username, display_name, public_key, role, is_online, "
        "last_seen::text, created_at::text, bio, status",
        user_id, display_name, bio, status);
    txn.commit();
    if (r.empty()) throw std::runtime_error("User not found");
    return User{r[0][0].as<std::string>(), r[0][1].as<std::string>(),
                r[0][2].as<std::string>(), r[0][3].as<std::string>(),
                r[0][4].as<std::string>(), r[0][5].as<bool>(),
                r[0][6].is_null() ? "" : r[0][6].as<std::string>(),
                r[0][7].as<std::string>(),
                r[0][8].is_null() ? "" : r[0][8].as<std::string>(),
                r[0][9].is_null() ? "" : r[0][9].as<std::string>()};
}

void Database::delete_user(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    // Messages don't cascade on user delete, so delete explicitly
    txn.exec_params("DELETE FROM messages WHERE user_id = $1", user_id);
    // The rest (sessions, user_keys, device_tokens, channel_members) cascade
    txn.exec_params("DELETE FROM users WHERE id = $1", user_id);
    txn.commit();
}

// --- Sessions ---

std::string Database::create_session(const std::string& user_id, int expiry_hours) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string token = random_hex(64);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "INSERT INTO sessions (user_id, token, expires_at) "
        "VALUES ($1, $2, NOW() + ($3 || ' hours')::interval)",
        user_id, token, std::to_string(expiry_hours));
    txn.commit();
    return token;
}

std::optional<std::string> Database::validate_session(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT user_id FROM sessions WHERE token = $1 AND expires_at > NOW()",
        token);
    txn.commit();
    if (r.empty()) return std::nullopt;
    return r[0][0].as<std::string>();
}

void Database::delete_session(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params("DELETE FROM sessions WHERE token = $1", token);
    txn.commit();
}

// --- Challenges ---

void Database::store_challenge(const std::string& public_key, const std::string& challenge) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "INSERT INTO auth_challenges (public_key, challenge) VALUES ($1, $2) "
        "ON CONFLICT (public_key) DO UPDATE SET challenge = $2, created_at = NOW()",
        public_key, challenge);
    txn.commit();
}

std::optional<std::string> Database::get_challenge(const std::string& public_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT challenge FROM auth_challenges WHERE public_key = $1 "
        "AND created_at > NOW() - INTERVAL '5 minutes'",
        public_key);
    txn.commit();
    if (r.empty()) return std::nullopt;
    return r[0][0].as<std::string>();
}

void Database::delete_challenge(const std::string& public_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params("DELETE FROM auth_challenges WHERE public_key = $1", public_key);
    txn.commit();
}

// --- Channels ---

Channel Database::create_channel(const std::string& name, const std::string& description,
                                  bool is_direct, const std::string& created_by,
                                  const std::vector<std::string>& member_ids,
                                  bool is_public, const std::string& default_role) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());

    auto r = txn.exec_params(
        "INSERT INTO channels (name, description, is_direct, is_public, default_role, created_by) "
        "VALUES (NULLIF($1, ''), NULLIF($2, ''), $3, $4, $5, $6) "
        "RETURNING id, name, description, is_direct, is_public, default_role, created_by, created_at::text",
        name, description, is_direct, is_public, default_role, created_by);

    std::string channel_id = r[0][0].as<std::string>();

    for (const auto& mid : member_ids) {
        std::string member_role = (!is_direct && mid == created_by) ? "admin" : default_role;
        txn.exec_params(
            "INSERT INTO channel_members (channel_id, user_id, role) VALUES ($1, $2, $3) "
            "ON CONFLICT DO NOTHING",
            channel_id, mid, member_role);
    }

    txn.commit();

    Channel ch;
    ch.id = channel_id;
    ch.name = r[0][1].is_null() ? "" : r[0][1].as<std::string>();
    ch.description = r[0][2].is_null() ? "" : r[0][2].as<std::string>();
    ch.is_direct = r[0][3].as<bool>();
    ch.is_public = r[0][4].as<bool>();
    ch.default_role = r[0][5].as<std::string>();
    ch.created_by = r[0][6].is_null() ? "" : r[0][6].as<std::string>();
    ch.created_at = r[0][7].as<std::string>();
    ch.member_ids = member_ids;
    return ch;
}

std::vector<Channel> Database::list_user_channels(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT c.id, c.name, c.description, c.is_direct, c.is_public, c.default_role, "
        "c.created_by, c.created_at::text "
        "FROM channels c "
        "JOIN channel_members cm ON c.id = cm.channel_id "
        "WHERE cm.user_id = $1 ORDER BY c.created_at",
        user_id);
    txn.commit();

    std::vector<Channel> channels;
    for (const auto& row : r) {
        Channel ch;
        ch.id = row[0].as<std::string>();
        ch.name = row[1].is_null() ? "" : row[1].as<std::string>();
        ch.description = row[2].is_null() ? "" : row[2].as<std::string>();
        ch.is_direct = row[3].as<bool>();
        ch.is_public = row[4].as<bool>();
        ch.default_role = row[5].as<std::string>();
        ch.created_by = row[6].is_null() ? "" : row[6].as<std::string>();
        ch.created_at = row[7].as<std::string>();
        channels.push_back(ch);
    }
    return channels;
}

std::optional<Channel> Database::find_dm_channel(const std::string& user1_id, const std::string& user2_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT c.id, c.name, c.description, c.is_direct, c.is_public, c.default_role, "
        "c.created_by, c.created_at::text "
        "FROM channels c "
        "WHERE c.is_direct = true "
        "AND EXISTS (SELECT 1 FROM channel_members WHERE channel_id = c.id AND user_id = $1) "
        "AND EXISTS (SELECT 1 FROM channel_members WHERE channel_id = c.id AND user_id = $2) "
        "AND (SELECT COUNT(*) FROM channel_members WHERE channel_id = c.id) = "
        "    CASE WHEN $1 = $2 THEN 1 ELSE 2 END",
        user1_id, user2_id);
    txn.commit();
    if (r.empty()) return std::nullopt;
    Channel ch;
    ch.id = r[0][0].as<std::string>();
    ch.name = r[0][1].is_null() ? "" : r[0][1].as<std::string>();
    ch.description = r[0][2].is_null() ? "" : r[0][2].as<std::string>();
    ch.is_direct = r[0][3].as<bool>();
    ch.is_public = r[0][4].as<bool>();
    ch.default_role = r[0][5].as<std::string>();
    ch.created_by = r[0][6].is_null() ? "" : r[0][6].as<std::string>();
    ch.created_at = r[0][7].as<std::string>();
    return ch;
}

std::optional<Channel> Database::find_channel_by_id(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT id, name, description, is_direct, is_public, default_role, "
        "created_by, created_at::text FROM channels WHERE id = $1", id);
    txn.commit();
    if (r.empty()) return std::nullopt;
    Channel ch;
    ch.id = r[0][0].as<std::string>();
    ch.name = r[0][1].is_null() ? "" : r[0][1].as<std::string>();
    ch.description = r[0][2].is_null() ? "" : r[0][2].as<std::string>();
    ch.is_direct = r[0][3].as<bool>();
    ch.is_public = r[0][4].as<bool>();
    ch.default_role = r[0][5].as<std::string>();
    ch.created_by = r[0][6].is_null() ? "" : r[0][6].as<std::string>();
    ch.created_at = r[0][7].as<std::string>();
    return ch;
}

bool Database::is_channel_member(const std::string& channel_id, const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT 1 FROM channel_members WHERE channel_id = $1 AND user_id = $2",
        channel_id, user_id);
    txn.commit();
    return !r.empty();
}

void Database::add_channel_member(const std::string& channel_id, const std::string& user_id,
                                   const std::string& role) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "INSERT INTO channel_members (channel_id, user_id, role) VALUES ($1, $2, $3) ON CONFLICT DO NOTHING",
        channel_id, user_id, role);
    txn.commit();
}

std::string Database::get_member_role(const std::string& channel_id, const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT role FROM channel_members WHERE channel_id = $1 AND user_id = $2",
        channel_id, user_id);
    txn.commit();
    if (r.empty()) return "";
    return r[0][0].as<std::string>();
}

std::string Database::get_effective_role(const std::string& channel_id, const std::string& user_id) {
    auto user = find_user_by_id(user_id);
    if (user && user->role == "admin") {
        return "admin";
    }
    return get_member_role(channel_id, user_id);
}

void Database::remove_channel_member(const std::string& channel_id, const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "DELETE FROM channel_members WHERE channel_id = $1 AND user_id = $2",
        channel_id, user_id);
    txn.commit();
}

void Database::update_member_role(const std::string& channel_id, const std::string& user_id,
                                   const std::string& role) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "UPDATE channel_members SET role = $3 WHERE channel_id = $1 AND user_id = $2",
        channel_id, user_id, role);
    txn.commit();
}

Channel Database::update_channel(const std::string& channel_id, const std::string& name,
                                  const std::string& description, bool is_public,
                                  const std::string& default_role) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "UPDATE channels SET name = $2, description = $3, is_public = $4, default_role = $5 "
        "WHERE id = $1 "
        "RETURNING id, name, description, is_direct, is_public, default_role, created_by, created_at::text",
        channel_id, name, description, is_public, default_role);
    txn.commit();
    if (r.empty()) throw std::runtime_error("Channel not found");
    Channel ch;
    ch.id = r[0][0].as<std::string>();
    ch.name = r[0][1].is_null() ? "" : r[0][1].as<std::string>();
    ch.description = r[0][2].is_null() ? "" : r[0][2].as<std::string>();
    ch.is_direct = r[0][3].as<bool>();
    ch.is_public = r[0][4].as<bool>();
    ch.default_role = r[0][5].as<std::string>();
    ch.created_by = r[0][6].is_null() ? "" : r[0][6].as<std::string>();
    ch.created_at = r[0][7].as<std::string>();
    return ch;
}

std::vector<Channel> Database::list_public_channels(const std::string& user_id,
                                                     const std::string& search) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    pqxx::result r;
    if (search.empty()) {
        r = txn.exec_params(
            "SELECT c.id, c.name, c.description, c.is_direct, c.is_public, c.default_role, "
            "c.created_by, c.created_at::text "
            "FROM channels c "
            "WHERE c.is_public = true AND c.is_direct = false "
            "AND NOT EXISTS (SELECT 1 FROM channel_members cm WHERE cm.channel_id = c.id AND cm.user_id = $1) "
            "ORDER BY c.name",
            user_id);
    } else {
        r = txn.exec_params(
            "SELECT c.id, c.name, c.description, c.is_direct, c.is_public, c.default_role, "
            "c.created_by, c.created_at::text "
            "FROM channels c "
            "WHERE c.is_public = true AND c.is_direct = false "
            "AND NOT EXISTS (SELECT 1 FROM channel_members cm WHERE cm.channel_id = c.id AND cm.user_id = $1) "
            "AND (c.name ILIKE '%' || $2 || '%' OR c.description ILIKE '%' || $2 || '%') "
            "ORDER BY c.name",
            user_id, search);
    }
    txn.commit();
    std::vector<Channel> channels;
    for (const auto& row : r) {
        Channel ch;
        ch.id = row[0].as<std::string>();
        ch.name = row[1].is_null() ? "" : row[1].as<std::string>();
        ch.description = row[2].is_null() ? "" : row[2].as<std::string>();
        ch.is_direct = row[3].as<bool>();
        ch.is_public = row[4].as<bool>();
        ch.default_role = row[5].as<std::string>();
        ch.created_by = row[6].is_null() ? "" : row[6].as<std::string>();
        ch.created_at = row[7].as<std::string>();
        channels.push_back(ch);
    }
    return channels;
}

std::vector<Channel> Database::list_all_channels() {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec(
        "SELECT id, name, description, is_direct, is_public, default_role, "
        "created_by, created_at::text FROM channels WHERE is_direct = false ORDER BY name");
    txn.commit();
    std::vector<Channel> channels;
    for (const auto& row : r) {
        Channel ch;
        ch.id = row[0].as<std::string>();
        ch.name = row[1].is_null() ? "" : row[1].as<std::string>();
        ch.description = row[2].is_null() ? "" : row[2].as<std::string>();
        ch.is_direct = row[3].as<bool>();
        ch.is_public = row[4].as<bool>();
        ch.default_role = row[5].as<std::string>();
        ch.created_by = row[6].is_null() ? "" : row[6].as<std::string>();
        ch.created_at = row[7].as<std::string>();
        channels.push_back(ch);
    }
    return channels;
}

std::vector<ChannelMember> Database::get_channel_members_with_roles(const std::string& channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT u.id, u.username, u.display_name, cm.role, u.is_online "
        "FROM channel_members cm JOIN users u ON cm.user_id = u.id "
        "WHERE cm.channel_id = $1 ORDER BY cm.role, u.username",
        channel_id);
    txn.commit();
    std::vector<ChannelMember> members;
    for (const auto& row : r) {
        members.push_back({row[0].as<std::string>(), row[1].as<std::string>(),
                           row[2].as<std::string>(), row[3].as<std::string>(),
                           row[4].as<bool>()});
    }
    return members;
}

std::optional<Channel> Database::find_general_channel() {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec(
        "SELECT id, name, description, is_direct, is_public, default_role, "
        "created_by, created_at::text FROM channels WHERE name = 'general' AND is_direct = false LIMIT 1");
    txn.commit();
    if (r.empty()) return std::nullopt;
    Channel ch;
    ch.id = r[0][0].as<std::string>();
    ch.name = r[0][1].is_null() ? "" : r[0][1].as<std::string>();
    ch.description = r[0][2].is_null() ? "" : r[0][2].as<std::string>();
    ch.is_direct = r[0][3].as<bool>();
    ch.is_public = r[0][4].as<bool>();
    ch.default_role = r[0][5].as<std::string>();
    ch.created_by = r[0][6].is_null() ? "" : r[0][6].as<std::string>();
    ch.created_at = r[0][7].as<std::string>();
    return ch;
}

std::vector<std::string> Database::get_channel_member_ids(const std::string& channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT user_id FROM channel_members WHERE channel_id = $1", channel_id);
    txn.commit();
    std::vector<std::string> ids;
    for (const auto& row : r) ids.push_back(row[0].as<std::string>());
    return ids;
}

// --- Messages ---

static Message row_to_message(const pqxx::row& r) {
    return Message{
        r[0].as<std::string>(), r[1].as<std::string>(),
        r[2].as<std::string>(), r[3].as<std::string>(),
        r[4].as<std::string>(), r[5].as<std::string>(),
        r[6].is_null() ? "" : r[6].as<std::string>(),
        r[7].as<bool>(),
        r[8].is_null() ? "" : r[8].as<std::string>(),
        r[9].is_null() ? "" : r[9].as<std::string>(),
        r[10].is_null() ? 0 : r[10].as<int64_t>(),
        r[11].is_null() ? "" : r[11].as<std::string>()
    };
}

static const char* MSG_COLS = "id, channel_id, user_id, "
    "(SELECT username FROM users WHERE id = user_id), content, created_at::text, "
    "edited_at::text, is_deleted, file_id, file_name, file_size, file_type";

static const char* MSG_COLS_JOINED = "m.id, m.channel_id, m.user_id, u.username, m.content, "
    "m.created_at::text, m.edited_at::text, m.is_deleted, "
    "m.file_id, m.file_name, m.file_size, m.file_type";

Message Database::create_message(const std::string& channel_id, const std::string& user_id,
                                  const std::string& content) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        std::string("INSERT INTO messages (channel_id, user_id, content) VALUES ($1, $2, $3) RETURNING ") + MSG_COLS,
        channel_id, user_id, content);
    txn.commit();
    return row_to_message(r[0]);
}

Message Database::create_file_message(const std::string& channel_id, const std::string& user_id,
                                       const std::string& content, const std::string& file_id,
                                       const std::string& file_name, int64_t file_size,
                                       const std::string& file_type) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        std::string("INSERT INTO messages (channel_id, user_id, content, file_id, file_name, file_size, file_type) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7) RETURNING ") + MSG_COLS,
        channel_id, user_id, content, file_id, file_name, file_size, file_type);
    txn.commit();
    return row_to_message(r[0]);
}

std::vector<Message> Database::get_messages(const std::string& channel_id, int limit,
                                             const std::string& before) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    pqxx::result r;
    if (before.empty()) {
        r = txn.exec_params(
            std::string("SELECT ") + MSG_COLS_JOINED +
            " FROM messages m JOIN users u ON m.user_id = u.id "
            "WHERE m.channel_id = $1 ORDER BY m.created_at DESC LIMIT $2",
            channel_id, limit);
    } else {
        r = txn.exec_params(
            std::string("SELECT ") + MSG_COLS_JOINED +
            " FROM messages m JOIN users u ON m.user_id = u.id "
            "WHERE m.channel_id = $1 AND m.created_at < $3 "
            "ORDER BY m.created_at DESC LIMIT $2",
            channel_id, limit, before);
    }
    txn.commit();

    std::vector<Message> msgs;
    for (const auto& row : r) {
        msgs.push_back(row_to_message(row));
    }
    // Reverse so messages are in chronological order
    std::reverse(msgs.begin(), msgs.end());
    return msgs;
}

Message Database::edit_message(const std::string& message_id, const std::string& user_id,
                                const std::string& new_content) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        std::string("UPDATE messages SET content = $3, edited_at = NOW() "
        "WHERE id = $1 AND user_id = $2 AND is_deleted = false RETURNING ") + MSG_COLS,
        message_id, user_id, new_content);
    txn.commit();
    if (r.empty()) throw std::runtime_error("Message not found or not owned by user");
    return row_to_message(r[0]);
}

Message Database::delete_message(const std::string& message_id, const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        std::string("UPDATE messages SET is_deleted = true, content = '' "
        "WHERE id = $1 AND user_id = $2 RETURNING ") + MSG_COLS,
        message_id, user_id);
    txn.commit();
    if (r.empty()) throw std::runtime_error("Message not found or not owned by user");
    return row_to_message(r[0]);
}

std::optional<Database::FileInfo> Database::get_file_info(const std::string& file_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT file_name, file_type FROM messages WHERE file_id = $1 LIMIT 1",
        file_id);
    txn.commit();
    if (r.empty()) return std::nullopt;
    return FileInfo{r[0][0].as<std::string>(), r[0][1].as<std::string>()};
}

// --- Invites ---

std::string Database::create_invite(const std::string& created_by, int expiry_hours) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string token = random_hex(32);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "INSERT INTO invite_tokens (token, created_by, expires_at) "
        "VALUES ($1, $2, NOW() + ($3 || ' hours')::interval)",
        token, created_by, std::to_string(expiry_hours));
    txn.commit();
    return token;
}

bool Database::validate_invite(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT 1 FROM invite_tokens WHERE token = $1 AND used_by IS NULL AND expires_at > NOW()",
        token);
    txn.commit();
    return !r.empty();
}

void Database::use_invite(const std::string& token, const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params("UPDATE invite_tokens SET used_by = $2 WHERE token = $1", token, user_id);
    txn.commit();
}

std::vector<Database::InviteInfo> Database::list_invites() {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec(
        "SELECT i.id, i.token, COALESCE(u.username, 'system'), "
        "i.used_by IS NOT NULL, i.expires_at::text, i.created_at::text "
        "FROM invite_tokens i LEFT JOIN users u ON i.created_by = u.id "
        "ORDER BY i.created_at DESC");
    txn.commit();
    std::vector<InviteInfo> invites;
    for (const auto& row : r) {
        invites.push_back({row[0].as<std::string>(), row[1].as<std::string>(),
                           row[2].as<std::string>(), row[3].as<bool>(),
                           row[4].as<std::string>(), row[5].as<std::string>()});
    }
    return invites;
}

// --- Join Requests ---

std::string Database::create_join_request(const std::string& username, const std::string& display_name,
                                           const std::string& public_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "INSERT INTO join_requests (username, display_name, public_key) "
        "VALUES ($1, $2, $3) RETURNING id",
        username, display_name, public_key);
    txn.commit();
    return r[0][0].as<std::string>();
}

std::vector<Database::JoinRequest> Database::list_pending_requests() {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec(
        "SELECT id, username, display_name, public_key, status, created_at::text "
        "FROM join_requests WHERE status = 'pending' ORDER BY created_at");
    txn.commit();
    std::vector<JoinRequest> requests;
    for (const auto& row : r) {
        requests.push_back({row[0].as<std::string>(), row[1].as<std::string>(),
                            row[2].as<std::string>(), row[3].as<std::string>(),
                            row[4].as<std::string>(), row[5].as<std::string>()});
    }
    return requests;
}

std::optional<Database::JoinRequest> Database::get_join_request(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT id, username, display_name, public_key, status, created_at::text "
        "FROM join_requests WHERE id = $1", id);
    txn.commit();
    if (r.empty()) return std::nullopt;
    return JoinRequest{r[0][0].as<std::string>(), r[0][1].as<std::string>(),
                       r[0][2].as<std::string>(), r[0][3].as<std::string>(),
                       r[0][4].as<std::string>(), r[0][5].as<std::string>()};
}

void Database::update_join_request(const std::string& id, const std::string& status,
                                    const std::string& reviewed_by) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "UPDATE join_requests SET status = $2, reviewed_by = $3 WHERE id = $1",
        id, status, reviewed_by);
    txn.commit();
}

// --- Device / Multi-key Management ---

std::string Database::create_device_token(const std::string& user_id, int expiry_minutes) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string token = random_hex(32);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "INSERT INTO device_tokens (user_id, token, expires_at) "
        "VALUES ($1, $2, NOW() + ($3 || ' minutes')::interval)",
        user_id, token, std::to_string(expiry_minutes));
    txn.commit();
    return token;
}

std::optional<std::string> Database::validate_device_token(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT user_id FROM device_tokens "
        "WHERE token = $1 AND expires_at > NOW() AND used = false",
        token);
    txn.commit();
    if (r.empty()) return std::nullopt;
    return r[0][0].as<std::string>();
}

void Database::mark_device_token_used(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params("UPDATE device_tokens SET used = true WHERE token = $1", token);
    txn.commit();
}

void Database::add_user_key(const std::string& user_id, const std::string& public_key,
                             const std::string& device_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "INSERT INTO user_keys (user_id, public_key, device_name) VALUES ($1, $2, $3)",
        user_id, public_key, device_name);
    txn.commit();
}

std::vector<Database::UserKey> Database::list_user_keys(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT id, user_id, public_key, device_name, created_at::text "
        "FROM user_keys WHERE user_id = $1 ORDER BY created_at",
        user_id);
    txn.commit();
    std::vector<UserKey> keys;
    for (const auto& row : r) {
        keys.push_back({row[0].as<std::string>(), row[1].as<std::string>(),
                        row[2].as<std::string>(), row[3].as<std::string>(),
                        row[4].as<std::string>()});
    }
    return keys;
}

void Database::remove_user_key(const std::string& key_id, const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto count_r = txn.exec_params(
        "SELECT COUNT(*) FROM user_keys WHERE user_id = $1", user_id);
    if (count_r[0][0].as<int>() <= 1) {
        txn.abort();
        throw std::runtime_error("Cannot remove the last device key");
    }
    txn.exec_params(
        "DELETE FROM user_keys WHERE id = $1 AND user_id = $2",
        key_id, user_id);
    txn.commit();
}

// --- Server Settings ---

std::optional<std::string> Database::get_setting(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT value FROM server_settings WHERE key = $1", key);
    txn.commit();
    if (r.empty()) return std::nullopt;
    return r[0][0].as<std::string>();
}

void Database::set_setting(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "INSERT INTO server_settings (key, value) VALUES ($1, $2) "
        "ON CONFLICT (key) DO UPDATE SET value = $2",
        key, value);
    txn.commit();
}

int64_t Database::get_total_file_size() {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec(
        "SELECT COALESCE(SUM(file_size), 0) FROM messages WHERE file_id IS NOT NULL");
    txn.commit();
    return r[0][0].as<int64_t>();
}
