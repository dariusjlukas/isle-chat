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
        ALTER TABLE users ADD COLUMN IF NOT EXISTS avatar_file_id TEXT DEFAULT '';
        ALTER TABLE users ADD COLUMN IF NOT EXISTS profile_color VARCHAR(7) DEFAULT '';
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

    // WebAuthn tables
    txn.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS webauthn_credentials (
            id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
            credential_id TEXT NOT NULL UNIQUE,
            public_key BYTEA NOT NULL,
            sign_count INTEGER NOT NULL DEFAULT 0,
            device_name VARCHAR(100) DEFAULT 'Passkey',
            transports TEXT,
            created_at TIMESTAMPTZ DEFAULT NOW()
        );
        CREATE INDEX IF NOT EXISTS idx_webauthn_cred_id ON webauthn_credentials(credential_id);

        CREATE TABLE IF NOT EXISTS webauthn_challenges (
            id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            challenge TEXT NOT NULL UNIQUE,
            extra_data TEXT,
            created_at TIMESTAMPTZ DEFAULT NOW()
        );

        ALTER TABLE users ALTER COLUMN public_key DROP NOT NULL;
        ALTER TABLE users ALTER COLUMN public_key SET DEFAULT '';
        ALTER TABLE join_requests ALTER COLUMN public_key DROP NOT NULL;
        ALTER TABLE join_requests ALTER COLUMN public_key SET DEFAULT '';
    )SQL");

    // PKI credentials and recovery keys
    txn.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS pki_credentials (
            id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
            public_key TEXT NOT NULL,
            device_name VARCHAR(100) DEFAULT 'Browser Key',
            created_at TIMESTAMPTZ DEFAULT NOW()
        );
        CREATE INDEX IF NOT EXISTS idx_pki_user ON pki_credentials(user_id);
        CREATE INDEX IF NOT EXISTS idx_pki_pubkey ON pki_credentials(public_key);

        CREATE TABLE IF NOT EXISTS recovery_keys (
            id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
            key_hash TEXT NOT NULL,
            used BOOLEAN NOT NULL DEFAULT false,
            created_at TIMESTAMPTZ DEFAULT NOW()
        );
        CREATE INDEX IF NOT EXISTS idx_recovery_user ON recovery_keys(user_id);
    )SQL");

    // Join request credential storage
    txn.exec(R"SQL(
        ALTER TABLE join_requests ADD COLUMN IF NOT EXISTS auth_method TEXT DEFAULT '';
        ALTER TABLE join_requests ADD COLUMN IF NOT EXISTS credential_data TEXT DEFAULT '';
        ALTER TABLE join_requests ADD COLUMN IF NOT EXISTS session_token TEXT DEFAULT '';
    )SQL");

    // Recovery tokens (admin-generated account recovery)
    txn.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS recovery_tokens (
            id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            token VARCHAR(64) UNIQUE NOT NULL,
            created_by UUID REFERENCES users(id),
            for_user_id UUID NOT NULL REFERENCES users(id),
            used BOOLEAN DEFAULT FALSE,
            used_at TIMESTAMPTZ,
            expires_at TIMESTAMPTZ NOT NULL,
            created_at TIMESTAMPTZ DEFAULT NOW()
        );
    )SQL");
    txn.exec(R"SQL(
        ALTER TABLE recovery_tokens ADD COLUMN IF NOT EXISTS used_at TIMESTAMPTZ;
    )SQL");
    txn.exec(R"SQL(
        ALTER TABLE invite_tokens ADD COLUMN IF NOT EXISTS used_at TIMESTAMPTZ;
    )SQL");
    // Multi-use invite tokens
    txn.exec(R"SQL(
        ALTER TABLE invite_tokens ADD COLUMN IF NOT EXISTS max_uses INTEGER NOT NULL DEFAULT 1;
        ALTER TABLE invite_tokens ADD COLUMN IF NOT EXISTS use_count INTEGER NOT NULL DEFAULT 0;
    )SQL");
    // Backfill use_count for already-used single-use tokens
    txn.exec(R"SQL(
        UPDATE invite_tokens SET use_count = 1 WHERE used_by IS NOT NULL AND use_count = 0;
    )SQL");
    // Track individual uses of multi-use tokens
    txn.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS invite_uses (
            id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            invite_id UUID NOT NULL REFERENCES invite_tokens(id) ON DELETE CASCADE,
            used_by UUID NOT NULL REFERENCES users(id),
            used_at TIMESTAMPTZ DEFAULT NOW()
        );
    )SQL");

    // User bans
    txn.exec(R"SQL(
        ALTER TABLE users ADD COLUMN IF NOT EXISTS is_banned BOOLEAN NOT NULL DEFAULT FALSE;
        ALTER TABLE users ADD COLUMN IF NOT EXISTS banned_at TIMESTAMPTZ;
        ALTER TABLE users ADD COLUMN IF NOT EXISTS banned_by UUID REFERENCES users(id);
    )SQL");

    // Spaces
    txn.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS spaces (
            id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            name VARCHAR(100) NOT NULL,
            description TEXT,
            icon VARCHAR(10) DEFAULT '',
            is_public BOOLEAN DEFAULT TRUE,
            default_role VARCHAR(20) DEFAULT 'write',
            created_by UUID REFERENCES users(id),
            created_at TIMESTAMPTZ DEFAULT NOW()
        );

        CREATE TABLE IF NOT EXISTS space_members (
            space_id UUID REFERENCES spaces(id) ON DELETE CASCADE,
            user_id UUID REFERENCES users(id) ON DELETE CASCADE,
            role VARCHAR(20) DEFAULT 'write',
            joined_at TIMESTAMPTZ DEFAULT NOW(),
            PRIMARY KEY (space_id, user_id)
        );

        ALTER TABLE channels ADD COLUMN IF NOT EXISTS space_id UUID REFERENCES spaces(id) ON DELETE CASCADE;
        CREATE INDEX IF NOT EXISTS idx_channels_space ON channels(space_id);
        ALTER TABLE channels ADD COLUMN IF NOT EXISTS conversation_name VARCHAR(200);

        CREATE TABLE IF NOT EXISTS channel_read_state (
            channel_id UUID REFERENCES channels(id) ON DELETE CASCADE,
            user_id UUID REFERENCES users(id) ON DELETE CASCADE,
            last_read_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
            last_read_message_id UUID,
            PRIMARY KEY (channel_id, user_id)
        );

        CREATE TABLE IF NOT EXISTS mentions (
            id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            message_id UUID REFERENCES messages(id) ON DELETE CASCADE,
            channel_id UUID NOT NULL,
            mentioned_user_id UUID REFERENCES users(id) ON DELETE CASCADE,
            is_channel_mention BOOLEAN DEFAULT FALSE,
            created_at TIMESTAMPTZ DEFAULT NOW()
        );
        CREATE INDEX IF NOT EXISTS idx_mentions_user ON mentions(mentioned_user_id);

        CREATE TABLE IF NOT EXISTS space_invites (
            id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            space_id UUID NOT NULL REFERENCES spaces(id) ON DELETE CASCADE,
            invited_user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
            invited_by UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
            role VARCHAR(20) DEFAULT 'write',
            status VARCHAR(20) DEFAULT 'pending',
            created_at TIMESTAMPTZ DEFAULT NOW()
        );
        CREATE INDEX IF NOT EXISTS idx_space_invites_user ON space_invites(invited_user_id, status);

        CREATE TABLE IF NOT EXISTS notifications (
            id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
            type VARCHAR(50) NOT NULL,
            source_user_id UUID REFERENCES users(id) ON DELETE SET NULL,
            channel_id UUID REFERENCES channels(id) ON DELETE CASCADE,
            message_id UUID,
            content TEXT,
            is_read BOOLEAN NOT NULL DEFAULT FALSE,
            created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
        );
        CREATE INDEX IF NOT EXISTS idx_notifications_user ON notifications(user_id, created_at DESC);
        CREATE INDEX IF NOT EXISTS idx_notifications_unread ON notifications(user_id) WHERE is_read = false;
    )SQL");

    // Data migration: create a "General" space for existing channels
    txn.exec(R"SQL(
        INSERT INTO spaces (id, name, description, icon, is_public, created_by)
        SELECT gen_random_uuid(), 'General', 'Default space', '', true,
               (SELECT id FROM users WHERE role = 'admin' LIMIT 1)
        WHERE EXISTS (SELECT 1 FROM channels WHERE is_direct = false AND space_id IS NULL)
          AND NOT EXISTS (SELECT 1 FROM spaces WHERE name = 'General');

        UPDATE channels SET space_id = (SELECT id FROM spaces WHERE name = 'General' LIMIT 1)
        WHERE is_direct = false AND space_id IS NULL
          AND EXISTS (SELECT 1 FROM spaces WHERE name = 'General');

        INSERT INTO space_members (space_id, user_id, role)
        SELECT DISTINCT c.space_id, cm.user_id, MAX(cm.role)
        FROM channel_members cm
        JOIN channels c ON c.id = cm.channel_id
        WHERE c.is_direct = false AND c.space_id IS NOT NULL
        GROUP BY c.space_id, cm.user_id
        ON CONFLICT DO NOTHING;
    )SQL");

    // Full-text search on message content
    txn.exec(R"SQL(
        ALTER TABLE messages ADD COLUMN IF NOT EXISTS content_tsv tsvector
          GENERATED ALWAYS AS (to_tsvector('english', coalesce(content, ''))) STORED;
        CREATE INDEX IF NOT EXISTS idx_messages_fts ON messages USING GIN (content_tsv);
    )SQL");

    // Archival and owner role support
    txn.exec(R"SQL(
        ALTER TABLE channels ADD COLUMN IF NOT EXISTS is_archived BOOLEAN DEFAULT FALSE;
        ALTER TABLE spaces ADD COLUMN IF NOT EXISTS is_archived BOOLEAN DEFAULT FALSE;

        -- Promote first server admin to owner if no owner exists
        UPDATE users SET role = 'owner'
        WHERE id = (SELECT id FROM users WHERE role = 'admin' ORDER BY created_at ASC LIMIT 1)
        AND NOT EXISTS (SELECT 1 FROM users WHERE role = 'owner');

        -- Promote space creators to owner if no owner exists per space
        UPDATE space_members SET role = 'owner'
        WHERE (space_id, user_id) IN (
            SELECT sm.space_id, sm.user_id FROM space_members sm
            JOIN spaces s ON s.id = sm.space_id AND s.created_by = sm.user_id
            WHERE sm.role = 'admin'
            AND NOT EXISTS (SELECT 1 FROM space_members sm2 WHERE sm2.space_id = sm.space_id AND sm2.role = 'owner')
        );
    )SQL");

    // Reactions table
    txn.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS reactions (
            id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            message_id UUID NOT NULL REFERENCES messages(id) ON DELETE CASCADE,
            user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
            emoji TEXT NOT NULL,
            created_at TIMESTAMPTZ DEFAULT NOW(),
            UNIQUE(message_id, user_id, emoji)
        );
        CREATE INDEX IF NOT EXISTS idx_reactions_message ON reactions(message_id);
    )SQL");

    // Reply-to-message support
    txn.exec(R"SQL(
        ALTER TABLE messages ADD COLUMN IF NOT EXISTS reply_to_message_id UUID REFERENCES messages(id) ON DELETE SET NULL;
    )SQL");

    // Space avatars and profile color
    txn.exec(R"SQL(
        ALTER TABLE spaces ADD COLUMN IF NOT EXISTS avatar_file_id TEXT DEFAULT '';
        ALTER TABLE spaces ADD COLUMN IF NOT EXISTS profile_color VARCHAR(7) DEFAULT '';
    )SQL");

    // Password credentials
    txn.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS password_credentials (
            id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
            password_hash TEXT NOT NULL,
            created_at TIMESTAMPTZ DEFAULT NOW()
        );
        CREATE INDEX IF NOT EXISTS idx_password_credentials_user ON password_credentials(user_id);
    )SQL");

    // Password history (for preventing reuse)
    txn.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS password_history (
            id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
            password_hash TEXT NOT NULL,
            created_at TIMESTAMPTZ DEFAULT NOW()
        );
        CREATE INDEX IF NOT EXISTS idx_password_history_user ON password_history(user_id);
    )SQL");

    // TOTP credentials
    txn.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS totp_credentials (
            id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
            secret TEXT NOT NULL,
            verified BOOLEAN DEFAULT FALSE,
            created_at TIMESTAMPTZ DEFAULT NOW(),
            UNIQUE(user_id)
        );
    )SQL");

    // MFA pending tokens (short-lived, for two-step login)
    txn.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS mfa_pending_tokens (
            id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
            auth_method TEXT NOT NULL,
            created_at TIMESTAMPTZ DEFAULT NOW(),
            expires_at TIMESTAMPTZ NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_mfa_pending_user ON mfa_pending_tokens(user_id);
    )SQL");

    // Default-join channels for spaces
    txn.exec(R"SQL(
        ALTER TABLE channels ADD COLUMN IF NOT EXISTS default_join BOOLEAN DEFAULT FALSE;
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
        "u.last_seen::text, u.created_at::text, u.bio, u.status, u.avatar_file_id, u.profile_color, u.is_banned "
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
                r[0][9].is_null() ? "" : r[0][9].as<std::string>(),
                r[0][10].is_null() ? "" : r[0][10].as<std::string>(),
                r[0][11].is_null() ? "" : r[0][11].as<std::string>(),
                r[0][12].as<bool>()};
}

std::optional<User> Database::find_user_by_id(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT id, username, display_name, public_key, role, is_online, "
        "last_seen::text, created_at::text, bio, status, avatar_file_id, profile_color, is_banned FROM users WHERE id = $1",
        id);
    txn.commit();
    if (r.empty()) return std::nullopt;
    return User{r[0][0].as<std::string>(), r[0][1].as<std::string>(),
                r[0][2].as<std::string>(), r[0][3].as<std::string>(),
                r[0][4].as<std::string>(), r[0][5].as<bool>(),
                r[0][6].is_null() ? "" : r[0][6].as<std::string>(),
                r[0][7].as<std::string>(),
                r[0][8].is_null() ? "" : r[0][8].as<std::string>(),
                r[0][9].is_null() ? "" : r[0][9].as<std::string>(),
                r[0][10].is_null() ? "" : r[0][10].as<std::string>(),
                r[0][11].is_null() ? "" : r[0][11].as<std::string>(),
                r[0][12].as<bool>()};
}

User Database::create_user(const std::string& username, const std::string& display_name,
                           const std::string& public_key, const std::string& role) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "INSERT INTO users (username, display_name, public_key, role) "
        "VALUES ($1, $2, $3, $4) "
        "RETURNING id, username, display_name, public_key, role, is_online, "
        "last_seen::text, created_at::text, bio, status, avatar_file_id, profile_color, is_banned",
        username, display_name, public_key, role);

    // Also register in user_keys table (only if using legacy PKI auth)
    if (!public_key.empty()) {
        txn.exec_params(
            "INSERT INTO user_keys (user_id, public_key, device_name) VALUES ($1, $2, 'Primary Device')",
            r[0][0].as<std::string>(), public_key);
    }

    txn.commit();
    return User{r[0][0].as<std::string>(), r[0][1].as<std::string>(),
                r[0][2].as<std::string>(), r[0][3].as<std::string>(),
                r[0][4].as<std::string>(), r[0][5].as<bool>(),
                r[0][6].is_null() ? "" : r[0][6].as<std::string>(),
                r[0][7].as<std::string>(),
                r[0][8].is_null() ? "" : r[0][8].as<std::string>(),
                r[0][9].is_null() ? "" : r[0][9].as<std::string>(),
                r[0][10].is_null() ? "" : r[0][10].as<std::string>(),
                r[0][11].is_null() ? "" : r[0][11].as<std::string>(),
                r[0][12].as<bool>()};
}

std::vector<User> Database::list_users() {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec(
        "SELECT id, username, display_name, public_key, role, is_online, "
        "last_seen::text, created_at::text, bio, status, avatar_file_id, profile_color, is_banned FROM users ORDER BY username");
    txn.commit();
    std::vector<User> users;
    for (const auto& row : r) {
        users.push_back({row[0].as<std::string>(), row[1].as<std::string>(),
                         row[2].as<std::string>(), row[3].as<std::string>(),
                         row[4].as<std::string>(), row[5].as<bool>(),
                         row[6].is_null() ? "" : row[6].as<std::string>(),
                         row[7].as<std::string>(),
                         row[8].is_null() ? "" : row[8].as<std::string>(),
                         row[9].is_null() ? "" : row[9].as<std::string>(),
                         row[10].is_null() ? "" : row[10].as<std::string>(),
                         row[11].is_null() ? "" : row[11].as<std::string>(),
                         row[12].as<bool>()});
    }
    return users;
}

void Database::set_all_users_offline() {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec("UPDATE users SET is_online = false WHERE is_online = true");
    txn.commit();
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
                                    const std::string& bio, const std::string& status,
                                    const std::string& profile_color) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "UPDATE users SET display_name = $2, bio = $3, status = $4, profile_color = $5 WHERE id = $1 "
        "RETURNING id, username, display_name, public_key, role, is_online, "
        "last_seen::text, created_at::text, bio, status, avatar_file_id, profile_color, is_banned",
        user_id, display_name, bio, status, profile_color);
    txn.commit();
    if (r.empty()) throw std::runtime_error("User not found");
    return User{r[0][0].as<std::string>(), r[0][1].as<std::string>(),
                r[0][2].as<std::string>(), r[0][3].as<std::string>(),
                r[0][4].as<std::string>(), r[0][5].as<bool>(),
                r[0][6].is_null() ? "" : r[0][6].as<std::string>(),
                r[0][7].as<std::string>(),
                r[0][8].is_null() ? "" : r[0][8].as<std::string>(),
                r[0][9].is_null() ? "" : r[0][9].as<std::string>(),
                r[0][10].is_null() ? "" : r[0][10].as<std::string>(),
                r[0][11].is_null() ? "" : r[0][11].as<std::string>(),
                r[0][12].as<bool>()};
}

void Database::set_user_avatar(const std::string& user_id, const std::string& avatar_file_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params("UPDATE users SET avatar_file_id = $2 WHERE id = $1", user_id, avatar_file_id);
    txn.commit();
}

void Database::clear_user_avatar(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params("SELECT avatar_file_id FROM users WHERE id = $1", user_id);
    std::string old_file_id;
    if (!r.empty() && !r[0][0].is_null()) old_file_id = r[0][0].as<std::string>();
    txn.exec_params("UPDATE users SET avatar_file_id = '' WHERE id = $1", user_id);
    txn.commit();
}

void Database::update_user_role(const std::string& user_id, const std::string& role) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params("UPDATE users SET role = $2 WHERE id = $1", user_id, role);
    txn.commit();
}

void Database::ban_user(const std::string& user_id, const std::string& banned_by) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "UPDATE users SET is_banned = true, banned_at = NOW(), banned_by = $2 WHERE id = $1",
        user_id, banned_by);
    // Delete all active sessions so the banned user is immediately logged out
    txn.exec_params("DELETE FROM sessions WHERE user_id = $1", user_id);
    txn.commit();
}

void Database::unban_user(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "UPDATE users SET is_banned = false, banned_at = NULL, banned_by = NULL WHERE id = $1",
        user_id);
    txn.commit();
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
        "SELECT s.user_id FROM sessions s "
        "JOIN users u ON s.user_id = u.id "
        "WHERE s.token = $1 AND s.expires_at > NOW() AND u.is_banned = false",
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
                                  bool is_public, const std::string& default_role,
                                  const std::string& space_id, bool default_join) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());

    pqxx::result r;
    if (space_id.empty()) {
        r = txn.exec_params(
            "INSERT INTO channels (name, description, is_direct, is_public, default_role, created_by, default_join) "
            "VALUES (NULLIF($1, ''), NULLIF($2, ''), $3, $4, $5, $6, $7) "
            "RETURNING id, name, description, is_direct, is_public, default_role, created_by, created_at::text, "
            "space_id, conversation_name, is_archived, default_join",
            name, description, is_direct, is_public, default_role, created_by, default_join);
    } else {
        r = txn.exec_params(
            "INSERT INTO channels (name, description, is_direct, is_public, default_role, created_by, space_id, default_join) "
            "VALUES (NULLIF($1, ''), NULLIF($2, ''), $3, $4, $5, $6, $7, $8) "
            "RETURNING id, name, description, is_direct, is_public, default_role, created_by, created_at::text, "
            "space_id, conversation_name, is_archived, default_join",
            name, description, is_direct, is_public, default_role, created_by, space_id, default_join);
    }

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
    ch.space_id = r[0][8].is_null() ? "" : r[0][8].as<std::string>();
    ch.conversation_name = r[0][9].is_null() ? "" : r[0][9].as<std::string>();
    ch.is_archived = r[0][10].as<bool>();
    ch.default_join = r[0][11].as<bool>();
    ch.member_ids = member_ids;
    return ch;
}

static Channel row_to_channel(const pqxx::row& row) {
    Channel ch;
    ch.id = row[0].as<std::string>();
    ch.name = row[1].is_null() ? "" : row[1].as<std::string>();
    ch.description = row[2].is_null() ? "" : row[2].as<std::string>();
    ch.is_direct = row[3].as<bool>();
    ch.is_public = row[4].as<bool>();
    ch.default_role = row[5].as<std::string>();
    ch.created_by = row[6].is_null() ? "" : row[6].as<std::string>();
    ch.created_at = row[7].as<std::string>();
    ch.space_id = row[8].is_null() ? "" : row[8].as<std::string>();
    ch.conversation_name = row[9].is_null() ? "" : row[9].as<std::string>();
    ch.is_archived = row[10].as<bool>();
    ch.default_join = row[11].as<bool>();
    return ch;
}

static const char* CH_COLS = "c.id, c.name, c.description, c.is_direct, c.is_public, c.default_role, "
    "c.created_by, c.created_at::text, c.space_id, c.conversation_name, c.is_archived, c.default_join";

std::vector<Channel> Database::list_user_channels(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        std::string("SELECT ") + CH_COLS +
        " FROM channels c "
        "JOIN channel_members cm ON c.id = cm.channel_id "
        "WHERE cm.user_id = $1 ORDER BY c.created_at",
        user_id);
    txn.commit();

    std::vector<Channel> channels;
    for (const auto& row : r) {
        channels.push_back(row_to_channel(row));
    }
    return channels;
}

std::vector<Channel> Database::list_space_channels(const std::string& space_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        std::string("SELECT ") + CH_COLS +
        " FROM channels c "
        "WHERE c.space_id = $1 ORDER BY c.created_at",
        space_id);
    txn.commit();

    std::vector<Channel> channels;
    for (const auto& row : r) {
        channels.push_back(row_to_channel(row));
    }
    return channels;
}

std::optional<Channel> Database::find_dm_channel(const std::string& user1_id, const std::string& user2_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        std::string("SELECT ") + CH_COLS +
        " FROM channels c "
        "WHERE c.is_direct = true "
        "AND EXISTS (SELECT 1 FROM channel_members WHERE channel_id = c.id AND user_id = $1) "
        "AND EXISTS (SELECT 1 FROM channel_members WHERE channel_id = c.id AND user_id = $2) "
        "AND (SELECT COUNT(*) FROM channel_members WHERE channel_id = c.id) = "
        "    CASE WHEN $1 = $2 THEN 1 ELSE 2 END",
        user1_id, user2_id);
    txn.commit();
    if (r.empty()) return std::nullopt;
    return row_to_channel(r[0]);
}

std::optional<Channel> Database::find_channel_by_id(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        std::string("SELECT ") + CH_COLS + " FROM channels c WHERE c.id = $1", id);
    txn.commit();
    if (r.empty()) return std::nullopt;
    return row_to_channel(r[0]);
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
    if (user && (user->role == "admin" || user->role == "owner")) {
        return "admin";
    }
    // Space admins/owners get admin access to all channels in their space
    auto ch = find_channel_by_id(channel_id);
    if (ch && !ch->space_id.empty()) {
        std::string space_role = get_space_member_role(ch->space_id, user_id);
        if (space_role == "admin" || space_role == "owner") {
            return "admin";
        }
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
                                  const std::string& default_role, bool default_join) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "UPDATE channels SET name = $2, description = $3, is_public = $4, default_role = $5, default_join = $6 "
        "WHERE id = $1",
        channel_id, name, description, is_public, default_role, default_join);
    auto r = txn.exec_params(
        std::string("SELECT ") + CH_COLS + " FROM channels c WHERE c.id = $1", channel_id);
    txn.commit();
    if (r.empty()) throw std::runtime_error("Channel not found");
    return row_to_channel(r[0]);
}

std::vector<Channel> Database::get_default_join_channels(const std::string& space_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        std::string("SELECT ") + CH_COLS +
        " FROM channels c WHERE c.space_id = $1 AND c.default_join = true AND c.is_archived = false",
        space_id);
    txn.commit();

    std::vector<Channel> channels;
    for (const auto& row : r) {
        channels.push_back(row_to_channel(row));
    }
    return channels;
}

std::vector<Channel> Database::list_public_channels(const std::string& user_id,
                                                     const std::string& search) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    pqxx::result r;
    if (search.empty()) {
        r = txn.exec_params(
            std::string("SELECT ") + CH_COLS +
            " FROM channels c "
            "WHERE c.is_public = true AND c.is_direct = false "
            "AND NOT EXISTS (SELECT 1 FROM channel_members cm WHERE cm.channel_id = c.id AND cm.user_id = $1) "
            "ORDER BY c.name",
            user_id);
    } else {
        r = txn.exec_params(
            std::string("SELECT ") + CH_COLS +
            " FROM channels c "
            "WHERE c.is_public = true AND c.is_direct = false "
            "AND NOT EXISTS (SELECT 1 FROM channel_members cm WHERE cm.channel_id = c.id AND cm.user_id = $1) "
            "AND (c.name ILIKE '%' || $2 || '%' OR c.description ILIKE '%' || $2 || '%') "
            "ORDER BY c.name",
            user_id, search);
    }
    txn.commit();
    std::vector<Channel> channels;
    for (const auto& row : r) {
        channels.push_back(row_to_channel(row));
    }
    return channels;
}

std::vector<Channel> Database::list_browsable_space_channels(const std::string& space_id,
                                                              const std::string& user_id,
                                                              const std::string& search) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    pqxx::result r;
    if (search.empty()) {
        r = txn.exec_params(
            std::string("SELECT ") + CH_COLS +
            " FROM channels c "
            "WHERE c.space_id = $1 AND c.is_direct = false "
            "AND NOT EXISTS (SELECT 1 FROM channel_members cm WHERE cm.channel_id = c.id AND cm.user_id = $2) "
            "ORDER BY c.name",
            space_id, user_id);
    } else {
        r = txn.exec_params(
            std::string("SELECT ") + CH_COLS +
            " FROM channels c "
            "WHERE c.space_id = $1 AND c.is_direct = false "
            "AND NOT EXISTS (SELECT 1 FROM channel_members cm WHERE cm.channel_id = c.id AND cm.user_id = $2) "
            "AND (c.name ILIKE '%' || $3 || '%' OR c.description ILIKE '%' || $3 || '%') "
            "ORDER BY c.name",
            space_id, user_id, search);
    }
    txn.commit();
    std::vector<Channel> channels;
    for (const auto& row : r) {
        channels.push_back(row_to_channel(row));
    }
    return channels;
}

std::vector<Channel> Database::list_all_channels() {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec(
        std::string("SELECT ") + CH_COLS +
        " FROM channels c WHERE c.is_direct = false ORDER BY c.name");
    txn.commit();
    std::vector<Channel> channels;
    for (const auto& row : r) {
        channels.push_back(row_to_channel(row));
    }
    return channels;
}

std::vector<ChannelMember> Database::get_channel_members_with_roles(const std::string& channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT u.id, u.username, u.display_name, cm.role, u.is_online, u.last_seen::text "
        "FROM channel_members cm JOIN users u ON cm.user_id = u.id "
        "WHERE cm.channel_id = $1 ORDER BY cm.role, u.username",
        channel_id);
    txn.commit();
    std::vector<ChannelMember> members;
    for (const auto& row : r) {
        members.push_back({row[0].as<std::string>(), row[1].as<std::string>(),
                           row[2].as<std::string>(), row[3].as<std::string>(),
                           row[4].as<bool>(), row[5].is_null() ? "" : row[5].as<std::string>()});
    }
    return members;
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

// --- Spaces ---

static Space row_to_space(const pqxx::row& row) {
    Space sp;
    sp.id = row[0].as<std::string>();
    sp.name = row[1].is_null() ? "" : row[1].as<std::string>();
    sp.description = row[2].is_null() ? "" : row[2].as<std::string>();
    sp.is_public = row[3].as<bool>();
    sp.default_role = row[4].as<std::string>();
    sp.created_by = row[5].is_null() ? "" : row[5].as<std::string>();
    sp.created_at = row[6].as<std::string>();
    sp.is_archived = row[7].as<bool>();
    sp.avatar_file_id = row[8].is_null() ? "" : row[8].as<std::string>();
    sp.profile_color = row[9].is_null() ? "" : row[9].as<std::string>();
    return sp;
}

static const char* SP_COLS = "s.id, s.name, s.description, s.is_public, s.default_role, "
    "s.created_by, s.created_at::text, s.is_archived, s.avatar_file_id, s.profile_color";

Space Database::create_space(const std::string& name, const std::string& description,
                              bool is_public, const std::string& created_by,
                              const std::string& default_role) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto ins = txn.exec_params(
        "INSERT INTO spaces (name, description, is_public, default_role, created_by) "
        "VALUES ($1, $2, $3, $4, $5) RETURNING id",
        name, description, is_public, default_role, created_by);
    std::string space_id = ins[0][0].as<std::string>();
    // Creator is owner
    txn.exec_params(
        "INSERT INTO space_members (space_id, user_id, role) VALUES ($1, $2, 'owner')",
        space_id, created_by);
    auto r = txn.exec_params(
        std::string("SELECT ") + SP_COLS + " FROM spaces s WHERE s.id = $1", space_id);
    txn.commit();
    return row_to_space(r[0]);
}

std::vector<Space> Database::list_user_spaces(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        std::string("SELECT ") + SP_COLS +
        " FROM spaces s "
        "JOIN space_members sm ON s.id = sm.space_id "
        "WHERE sm.user_id = $1 ORDER BY s.name",
        user_id);
    txn.commit();
    std::vector<Space> spaces;
    for (const auto& row : r) spaces.push_back(row_to_space(row));
    return spaces;
}

std::optional<Space> Database::find_space_by_id(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        std::string("SELECT ") + SP_COLS + " FROM spaces s WHERE s.id = $1", id);
    txn.commit();
    if (r.empty()) return std::nullopt;
    return row_to_space(r[0]);
}

Space Database::update_space(const std::string& space_id, const std::string& name,
                              const std::string& description, bool is_public,
                              const std::string& default_role,
                              const std::string& profile_color) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "UPDATE spaces SET name = $2, description = $3, is_public = $4, default_role = $5, profile_color = $6 "
        "WHERE id = $1",
        space_id, name, description, is_public, default_role, profile_color);
    auto r = txn.exec_params(
        std::string("SELECT ") + SP_COLS + " FROM spaces s WHERE s.id = $1", space_id);
    txn.commit();
    if (r.empty()) throw std::runtime_error("Space not found");
    return row_to_space(r[0]);
}

std::vector<Space> Database::list_public_spaces(const std::string& user_id,
                                                 const std::string& search) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    pqxx::result r;
    if (search.empty()) {
        r = txn.exec_params(
            std::string("SELECT ") + SP_COLS +
            " FROM spaces s "
            "WHERE s.is_public = true "
            "AND NOT EXISTS (SELECT 1 FROM space_members sm WHERE sm.space_id = s.id AND sm.user_id = $1) "
            "ORDER BY s.name",
            user_id);
    } else {
        r = txn.exec_params(
            std::string("SELECT ") + SP_COLS +
            " FROM spaces s "
            "WHERE s.is_public = true "
            "AND NOT EXISTS (SELECT 1 FROM space_members sm WHERE sm.space_id = s.id AND sm.user_id = $1) "
            "AND (s.name ILIKE '%' || $2 || '%' OR s.description ILIKE '%' || $2 || '%') "
            "ORDER BY s.name",
            user_id, search);
    }
    txn.commit();
    std::vector<Space> spaces;
    for (const auto& row : r) spaces.push_back(row_to_space(row));
    return spaces;
}

std::vector<Space> Database::list_all_spaces() {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec(
        std::string("SELECT ") + SP_COLS + " FROM spaces s ORDER BY s.name");
    txn.commit();
    std::vector<Space> spaces;
    for (const auto& row : r) spaces.push_back(row_to_space(row));
    return spaces;
}

void Database::set_space_avatar(const std::string& space_id, const std::string& avatar_file_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params("UPDATE spaces SET avatar_file_id = $2 WHERE id = $1", space_id, avatar_file_id);
    txn.commit();
}

void Database::clear_space_avatar(const std::string& space_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params("UPDATE spaces SET avatar_file_id = '' WHERE id = $1", space_id);
    txn.commit();
}

bool Database::is_space_member(const std::string& space_id, const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT 1 FROM space_members WHERE space_id = $1 AND user_id = $2",
        space_id, user_id);
    txn.commit();
    return !r.empty();
}

void Database::add_space_member(const std::string& space_id, const std::string& user_id,
                                 const std::string& role) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "INSERT INTO space_members (space_id, user_id, role) VALUES ($1, $2, $3) ON CONFLICT DO NOTHING",
        space_id, user_id, role);
    txn.commit();
}

void Database::remove_space_member(const std::string& space_id, const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    // Also remove from all channels in this space
    txn.exec_params(
        "DELETE FROM channel_members WHERE user_id = $2 "
        "AND channel_id IN (SELECT id FROM channels WHERE space_id = $1)",
        space_id, user_id);
    txn.exec_params(
        "DELETE FROM space_members WHERE space_id = $1 AND user_id = $2",
        space_id, user_id);
    txn.commit();
}

void Database::update_space_member_role(const std::string& space_id, const std::string& user_id,
                                         const std::string& role) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "UPDATE space_members SET role = $3 WHERE space_id = $1 AND user_id = $2",
        space_id, user_id, role);
    txn.commit();
}

std::string Database::get_space_member_role(const std::string& space_id, const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT role FROM space_members WHERE space_id = $1 AND user_id = $2",
        space_id, user_id);
    txn.commit();
    if (r.empty()) return "";
    return r[0][0].as<std::string>();
}

std::vector<SpaceMember> Database::get_space_members_with_roles(const std::string& space_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT u.id, u.username, u.display_name, sm.role, u.is_online, u.last_seen::text "
        "FROM space_members sm JOIN users u ON sm.user_id = u.id "
        "WHERE sm.space_id = $1 ORDER BY sm.role, u.username",
        space_id);
    txn.commit();
    std::vector<SpaceMember> members;
    for (const auto& row : r) {
        members.push_back({row[0].as<std::string>(), row[1].as<std::string>(),
                           row[2].as<std::string>(), row[3].as<std::string>(),
                           row[4].as<bool>(), row[5].is_null() ? "" : row[5].as<std::string>()});
    }
    return members;
}

// --- Conversations ---

Channel Database::create_conversation(const std::string& created_by,
                                       const std::vector<std::string>& member_ids,
                                       const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "INSERT INTO channels (name, description, is_direct, is_public, default_role, created_by, conversation_name) "
        "VALUES (NULL, NULL, true, false, 'write', $1, NULLIF($2, '')) "
        "RETURNING id",
        created_by, name);

    std::string channel_id = r[0][0].as<std::string>();
    for (const auto& mid : member_ids) {
        txn.exec_params(
            "INSERT INTO channel_members (channel_id, user_id, role) VALUES ($1, $2, 'write') "
            "ON CONFLICT DO NOTHING",
            channel_id, mid);
    }

    auto r2 = txn.exec_params(
        std::string("SELECT ") + CH_COLS + " FROM channels c WHERE c.id = $1",
        channel_id);
    txn.commit();
    auto ch = row_to_channel(r2[0]);
    ch.member_ids = member_ids;
    return ch;
}

std::vector<Channel> Database::list_user_conversations(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        std::string("SELECT ") + CH_COLS +
        " FROM channels c "
        "JOIN channel_members cm ON c.id = cm.channel_id "
        "WHERE cm.user_id = $1 AND c.is_direct = true "
        "ORDER BY c.created_at DESC",
        user_id);
    txn.commit();
    std::vector<Channel> channels;
    for (const auto& row : r) channels.push_back(row_to_channel(row));
    return channels;
}

void Database::add_conversation_member(const std::string& channel_id, const std::string& user_id) {
    add_channel_member(channel_id, user_id, "write");
}

void Database::rename_conversation(const std::string& channel_id, const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "UPDATE channels SET conversation_name = NULLIF($2, '') WHERE id = $1 AND is_direct = true",
        channel_id, name);
    txn.commit();
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
        r[11].is_null() ? "" : r[11].as<std::string>(),
        r[12].is_null() ? "" : r[12].as<std::string>(),
        r[13].is_null() ? "" : r[13].as<std::string>(),
        r[14].is_null() ? "" : r[14].as<std::string>(),
        !r[15].is_null() && r[15].as<bool>()
    };
}

static const char* MSG_COLS = "id, channel_id, user_id, "
    "(SELECT username FROM users WHERE id = user_id), content, created_at::text, "
    "edited_at::text, is_deleted, file_id, file_name, file_size, file_type, "
    "reply_to_message_id, "
    "(SELECT username FROM users WHERE id = (SELECT user_id FROM messages m2 WHERE m2.id = messages.reply_to_message_id)), "
    "(SELECT LEFT(m2.content, 200) FROM messages m2 WHERE m2.id = messages.reply_to_message_id), "
    "(SELECT m2.is_deleted FROM messages m2 WHERE m2.id = messages.reply_to_message_id)";

static const char* MSG_COLS_JOINED = "m.id, m.channel_id, m.user_id, u.username, m.content, "
    "m.created_at::text, m.edited_at::text, m.is_deleted, "
    "m.file_id, m.file_name, m.file_size, m.file_type, "
    "m.reply_to_message_id, "
    "(SELECT u2.username FROM users u2 WHERE u2.id = (SELECT user_id FROM messages WHERE id = m.reply_to_message_id)), "
    "(SELECT LEFT(content, 200) FROM messages WHERE id = m.reply_to_message_id), "
    "(SELECT is_deleted FROM messages WHERE id = m.reply_to_message_id)";

Message Database::create_message(const std::string& channel_id, const std::string& user_id,
                                  const std::string& content,
                                  const std::string& reply_to_message_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    pqxx::result r;
    if (reply_to_message_id.empty()) {
        r = txn.exec_params(
            std::string("INSERT INTO messages (channel_id, user_id, content) VALUES ($1, $2, $3) RETURNING ") + MSG_COLS,
            channel_id, user_id, content);
    } else {
        r = txn.exec_params(
            std::string("INSERT INTO messages (channel_id, user_id, content, reply_to_message_id) VALUES ($1, $2, $3, $4) RETURNING ") + MSG_COLS,
            channel_id, user_id, content, reply_to_message_id);
    }
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

Message Database::admin_delete_message(const std::string& message_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        std::string("UPDATE messages SET is_deleted = true, content = '' "
        "WHERE id = $1 RETURNING ") + MSG_COLS,
        message_id);
    txn.commit();
    if (r.empty()) throw std::runtime_error("Message not found");
    return row_to_message(r[0]);
}

std::optional<Database::MessageOwnership> Database::get_message_ownership(const std::string& message_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT channel_id, user_id FROM messages WHERE id = $1",
        message_id);
    txn.commit();
    if (r.empty()) return std::nullopt;
    return MessageOwnership{r[0][0].as<std::string>(), r[0][1].as<std::string>()};
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

// --- Reactions ---

void Database::add_reaction(const std::string& message_id, const std::string& user_id,
                             const std::string& emoji) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "INSERT INTO reactions (message_id, user_id, emoji) VALUES ($1, $2, $3) "
        "ON CONFLICT (message_id, user_id, emoji) DO NOTHING",
        message_id, user_id, emoji);
    txn.commit();
}

void Database::remove_reaction(const std::string& message_id, const std::string& user_id,
                                const std::string& emoji) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "DELETE FROM reactions WHERE message_id = $1 AND user_id = $2 AND emoji = $3",
        message_id, user_id, emoji);
    txn.commit();
}

std::vector<Database::Reaction> Database::get_reactions(const std::string& message_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT r.emoji, r.user_id, u.username FROM reactions r "
        "JOIN users u ON r.user_id = u.id WHERE r.message_id = $1 ORDER BY r.created_at",
        message_id);
    txn.commit();
    std::vector<Reaction> reactions;
    for (const auto& row : r) {
        reactions.push_back({row[0].as<std::string>(), row[1].as<std::string>(), row[2].as<std::string>()});
    }
    return reactions;
}

std::map<std::string, std::vector<Database::Reaction>> Database::get_reactions_for_messages(
    const std::vector<std::string>& message_ids) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());

    std::map<std::string, std::vector<Reaction>> result;
    if (message_ids.empty()) return result;

    // Build IN clause
    std::string ids;
    for (size_t i = 0; i < message_ids.size(); ++i) {
        if (i > 0) ids += ',';
        ids += txn.quote(message_ids[i]);
    }

    auto r = txn.exec(
        "SELECT r.message_id, r.emoji, r.user_id, u.username FROM reactions r "
        "JOIN users u ON r.user_id = u.id WHERE r.message_id IN (" + ids + ") ORDER BY r.created_at");
    txn.commit();

    for (const auto& row : r) {
        auto mid = row[0].as<std::string>();
        result[mid].push_back({row[1].as<std::string>(), row[2].as<std::string>(), row[3].as<std::string>()});
    }
    return result;
}

std::string Database::get_message_channel_id(const std::string& message_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT channel_id FROM messages WHERE id = $1", message_id);
    txn.commit();
    if (r.empty()) throw std::runtime_error("Message not found");
    return r[0][0].as<std::string>();
}

// --- Invites ---

std::string Database::create_invite(const std::string& created_by, int expiry_hours, int max_uses) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string token = random_hex(32);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "INSERT INTO invite_tokens (token, created_by, expires_at, max_uses) "
        "VALUES ($1, $2, NOW() + ($3 || ' hours')::interval, $4)",
        token, created_by, std::to_string(expiry_hours), max_uses);
    txn.commit();
    return token;
}

bool Database::validate_invite(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT 1 FROM invite_tokens WHERE token = $1 AND expires_at > NOW() "
        "AND (max_uses = 0 OR use_count < max_uses)",
        token);
    txn.commit();
    return !r.empty();
}

void Database::use_invite(const std::string& token, const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    // Increment use count and set used_by/used_at for the most recent use
    txn.exec_params(
        "UPDATE invite_tokens SET use_count = use_count + 1, used_by = $2, used_at = NOW() "
        "WHERE token = $1",
        token, user_id);
    // Record individual use
    auto r = txn.exec_params(
        "SELECT id FROM invite_tokens WHERE token = $1", token);
    if (!r.empty()) {
        txn.exec_params(
            "INSERT INTO invite_uses (invite_id, used_by) VALUES ($1, $2)",
            r[0][0].as<std::string>(), user_id);
    }
    txn.commit();
}

std::vector<Database::InviteInfo> Database::list_invites() {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec(
        "SELECT i.id, i.token, COALESCE(u.username, 'system'), "
        "i.expires_at::text, i.created_at::text, "
        "i.max_uses, i.use_count "
        "FROM invite_tokens i LEFT JOIN users u ON i.created_by = u.id "
        "ORDER BY i.created_at DESC");

    std::vector<InviteInfo> invites;
    for (const auto& row : r) {
        InviteInfo info;
        info.id = row[0].as<std::string>();
        info.token = row[1].as<std::string>();
        info.created_by_username = row[2].as<std::string>();
        info.expires_at = row[3].as<std::string>();
        info.created_at = row[4].as<std::string>();
        info.max_uses = row[5].as<int>();
        info.use_count = row[6].as<int>();
        info.used = (info.max_uses == 1 && info.use_count >= 1);

        // Fetch individual uses for this invite
        auto uses = txn.exec_params(
            "SELECT COALESCE(u.username, 'unknown'), iu.used_at::text "
            "FROM invite_uses iu LEFT JOIN users u ON iu.used_by = u.id "
            "WHERE iu.invite_id = $1 ORDER BY iu.used_at DESC",
            info.id);
        for (const auto& urow : uses) {
            info.uses.push_back({urow[0].as<std::string>(), urow[1].as<std::string>()});
        }

        invites.push_back(std::move(info));
    }
    txn.commit();
    return invites;
}

bool Database::revoke_invite(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    // For single-use (max_uses=1): only revoke if unused
    // For multi-use (max_uses!=1): always allow revoke (stops further uses)
    auto r = txn.exec_params(
        "DELETE FROM invite_tokens WHERE id = $1 "
        "AND (max_uses != 1 OR use_count = 0) RETURNING id",
        id);
    txn.commit();
    return !r.empty();
}

// --- Join Requests ---

std::string Database::create_join_request(const std::string& username, const std::string& display_name,
                                           const std::string& public_key,
                                           const std::string& auth_method,
                                           const std::string& credential_data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "INSERT INTO join_requests (username, display_name, public_key, auth_method, credential_data) "
        "VALUES ($1, $2, $3, $4, $5) RETURNING id",
        username, display_name, public_key, auth_method, credential_data);
    txn.commit();
    return r[0][0].as<std::string>();
}

std::vector<Database::JoinRequest> Database::list_pending_requests() {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec(
        "SELECT id, username, display_name, public_key, status, "
        "COALESCE(auth_method, ''), created_at::text "
        "FROM join_requests WHERE status = 'pending' ORDER BY created_at");
    txn.commit();
    std::vector<JoinRequest> requests;
    for (const auto& row : r) {
        requests.push_back({row[0].as<std::string>(), row[1].as<std::string>(),
                            row[2].as<std::string>(), row[3].as<std::string>(),
                            row[4].as<std::string>(), row[5].as<std::string>(),
                            "", "", row[6].as<std::string>()});
    }
    return requests;
}

std::optional<Database::JoinRequest> Database::get_join_request(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT id, username, display_name, public_key, status, "
        "COALESCE(auth_method, ''), COALESCE(credential_data, ''), "
        "COALESCE(session_token, ''), created_at::text "
        "FROM join_requests WHERE id = $1", id);
    txn.commit();
    if (r.empty()) return std::nullopt;
    return JoinRequest{r[0][0].as<std::string>(), r[0][1].as<std::string>(),
                       r[0][2].as<std::string>(), r[0][3].as<std::string>(),
                       r[0][4].as<std::string>(), r[0][5].as<std::string>(),
                       r[0][6].as<std::string>(), r[0][7].as<std::string>(),
                       r[0][8].as<std::string>()};
}

void Database::set_join_request_session(const std::string& id, const std::string& session_token) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "UPDATE join_requests SET session_token = $2 WHERE id = $1",
        id, session_token);
    txn.commit();
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

// --- Space Invites ---

std::string Database::create_space_invite(const std::string& space_id, const std::string& invited_user_id,
                                           const std::string& invited_by, const std::string& role) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "INSERT INTO space_invites (space_id, invited_user_id, invited_by, role) "
        "VALUES ($1, $2, $3, $4) RETURNING id::text",
        space_id, invited_user_id, invited_by, role);
    txn.commit();
    return r[0][0].as<std::string>();
}

std::vector<Database::SpaceInvite> Database::list_pending_space_invites(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT si.id::text, si.space_id::text, s.name, "
        "si.invited_user_id::text, si.invited_by::text, u.username, "
        "si.role, si.status, si.created_at::text "
        "FROM space_invites si "
        "JOIN spaces s ON s.id = si.space_id "
        "JOIN users u ON u.id = si.invited_by "
        "WHERE si.invited_user_id = $1 AND si.status = 'pending' "
        "ORDER BY si.created_at DESC",
        user_id);
    txn.commit();
    std::vector<SpaceInvite> invites;
    for (const auto& row : r) {
        invites.push_back({
            row[0].as<std::string>(), row[1].as<std::string>(),
            row[2].as<std::string>(),
            row[3].as<std::string>(), row[4].as<std::string>(),
            row[5].as<std::string>(), row[6].as<std::string>(),
            row[7].as<std::string>(), row[8].as<std::string>()
        });
    }
    return invites;
}

std::optional<Database::SpaceInvite> Database::get_space_invite(const std::string& invite_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT si.id::text, si.space_id::text, s.name, "
        "si.invited_user_id::text, si.invited_by::text, u.username, "
        "si.role, si.status, si.created_at::text "
        "FROM space_invites si "
        "JOIN spaces s ON s.id = si.space_id "
        "JOIN users u ON u.id = si.invited_by "
        "WHERE si.id = $1",
        invite_id);
    txn.commit();
    if (r.empty()) return std::nullopt;
    const auto& row = r[0];
    return SpaceInvite{
        row[0].as<std::string>(), row[1].as<std::string>(),
        row[2].as<std::string>(),
        row[3].as<std::string>(), row[4].as<std::string>(),
        row[5].as<std::string>(), row[6].as<std::string>(),
        row[7].as<std::string>(), row[8].as<std::string>()
    };
}

void Database::update_space_invite_status(const std::string& invite_id, const std::string& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params("UPDATE space_invites SET status = $2 WHERE id = $1", invite_id, status);
    txn.commit();
}

bool Database::has_pending_space_invite(const std::string& space_id, const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT 1 FROM space_invites WHERE space_id = $1 AND invited_user_id = $2 AND status = 'pending' LIMIT 1",
        space_id, user_id);
    txn.commit();
    return !r.empty();
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

// --- WebAuthn Credentials ---

void Database::store_webauthn_credential(const std::string& user_id, const std::string& credential_id,
                                          const std::vector<unsigned char>& public_key, int sign_count,
                                          const std::string& device_name, const std::string& transports) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "INSERT INTO webauthn_credentials (user_id, credential_id, public_key, sign_count, device_name, transports) "
        "VALUES ($1, $2, $3, $4, $5, $6)",
        user_id, credential_id,
        pqxx::binarystring(public_key.data(), public_key.size()),
        sign_count, device_name, transports);
    txn.commit();
}

std::optional<Database::WebAuthnCredential> Database::find_webauthn_credential(const std::string& credential_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT id, user_id, credential_id, public_key, sign_count, device_name, transports, created_at::text "
        "FROM webauthn_credentials WHERE credential_id = $1",
        credential_id);
    txn.commit();
    if (r.empty()) return std::nullopt;
    auto pk_view = r[0][3].as<pqxx::binarystring>();
    WebAuthnCredential cred;
    cred.id = r[0][0].as<std::string>();
    cred.user_id = r[0][1].as<std::string>();
    cred.credential_id = r[0][2].as<std::string>();
    cred.public_key.assign(pk_view.begin(), pk_view.end());
    cred.sign_count = r[0][4].as<int>();
    cred.device_name = r[0][5].is_null() ? "Passkey" : r[0][5].as<std::string>();
    cred.transports = r[0][6].is_null() ? "" : r[0][6].as<std::string>();
    cred.created_at = r[0][7].as<std::string>();
    return cred;
}

std::vector<Database::WebAuthnCredential> Database::list_webauthn_credentials(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT id, user_id, credential_id, public_key, sign_count, device_name, transports, created_at::text "
        "FROM webauthn_credentials WHERE user_id = $1 ORDER BY created_at",
        user_id);
    txn.commit();
    std::vector<WebAuthnCredential> creds;
    for (const auto& row : r) {
        auto pk_view = row[3].as<pqxx::binarystring>();
        WebAuthnCredential cred;
        cred.id = row[0].as<std::string>();
        cred.user_id = row[1].as<std::string>();
        cred.credential_id = row[2].as<std::string>();
        cred.public_key.assign(pk_view.begin(), pk_view.end());
        cred.sign_count = row[4].as<int>();
        cred.device_name = row[5].is_null() ? "Passkey" : row[5].as<std::string>();
        cred.transports = row[6].is_null() ? "" : row[6].as<std::string>();
        cred.created_at = row[7].as<std::string>();
        creds.push_back(cred);
    }
    return creds;
}

void Database::update_webauthn_sign_count(const std::string& credential_id, int new_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "UPDATE webauthn_credentials SET sign_count = $2 WHERE credential_id = $1",
        credential_id, new_count);
    txn.commit();
}

void Database::remove_webauthn_credential(const std::string& credential_id, const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto count_r = txn.exec_params(
        "SELECT COUNT(*) FROM webauthn_credentials WHERE user_id = $1", user_id);
    if (count_r[0][0].as<int>() <= 1) {
        txn.abort();
        throw std::runtime_error("Cannot remove the last passkey");
    }
    txn.exec_params(
        "DELETE FROM webauthn_credentials WHERE credential_id = $1 AND user_id = $2",
        credential_id, user_id);
    txn.commit();
}

std::optional<User> Database::find_user_by_credential_id(const std::string& credential_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT u.id, u.username, u.display_name, u.public_key, u.role, u.is_online, "
        "u.last_seen::text, u.created_at::text, u.bio, u.status, u.avatar_file_id, u.profile_color, u.is_banned "
        "FROM users u JOIN webauthn_credentials wc ON u.id = wc.user_id "
        "WHERE wc.credential_id = $1",
        credential_id);
    txn.commit();
    if (r.empty()) return std::nullopt;
    return User{r[0][0].as<std::string>(), r[0][1].as<std::string>(),
                r[0][2].as<std::string>(),
                r[0][3].is_null() ? "" : r[0][3].as<std::string>(),
                r[0][4].as<std::string>(), r[0][5].as<bool>(),
                r[0][6].is_null() ? "" : r[0][6].as<std::string>(),
                r[0][7].as<std::string>(),
                r[0][8].is_null() ? "" : r[0][8].as<std::string>(),
                r[0][9].is_null() ? "" : r[0][9].as<std::string>(),
                r[0][10].is_null() ? "" : r[0][10].as<std::string>(),
                r[0][11].is_null() ? "" : r[0][11].as<std::string>(),
                r[0][12].as<bool>()};
}

// --- WebAuthn Challenges ---

void Database::store_webauthn_challenge(const std::string& challenge, const std::string& extra_data_json) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "INSERT INTO webauthn_challenges (challenge, extra_data) VALUES ($1, $2) "
        "ON CONFLICT (challenge) DO UPDATE SET extra_data = $2, created_at = NOW()",
        challenge, extra_data_json);
    txn.commit();
}

std::optional<Database::WebAuthnChallenge> Database::get_webauthn_challenge(const std::string& challenge) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT challenge, extra_data FROM webauthn_challenges "
        "WHERE challenge = $1 AND created_at > NOW() - INTERVAL '5 minutes'",
        challenge);
    txn.commit();
    if (r.empty()) return std::nullopt;
    return WebAuthnChallenge{r[0][0].as<std::string>(),
                             r[0][1].is_null() ? "" : r[0][1].as<std::string>()};
}

void Database::delete_webauthn_challenge(const std::string& challenge) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params("DELETE FROM webauthn_challenges WHERE challenge = $1", challenge);
    txn.commit();
}

bool Database::has_approved_join_request(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT 1 FROM join_requests WHERE username = $1 AND status = 'approved' LIMIT 1",
        username);
    txn.commit();
    return !r.empty();
}

// --- PKI Credentials ---

void Database::store_pki_credential(const std::string& user_id, const std::string& public_key_spki,
                                      const std::string& device_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "INSERT INTO pki_credentials (user_id, public_key, device_name) VALUES ($1, $2, $3)",
        user_id, public_key_spki, device_name);
    txn.commit();
}

std::vector<Database::PkiCredential> Database::list_pki_credentials(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT id, user_id, public_key, device_name, created_at::text "
        "FROM pki_credentials WHERE user_id = $1 ORDER BY created_at",
        user_id);
    txn.commit();
    std::vector<PkiCredential> creds;
    for (const auto& row : r) {
        creds.push_back({row[0].as<std::string>(), row[1].as<std::string>(),
                         row[2].as<std::string>(), row[3].as<std::string>(),
                         row[4].as<std::string>()});
    }
    return creds;
}

std::optional<Database::PkiCredential> Database::find_pki_credential_by_key(const std::string& public_key_spki) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT id, user_id, public_key, device_name, created_at::text "
        "FROM pki_credentials WHERE public_key = $1",
        public_key_spki);
    txn.commit();
    if (r.empty()) return std::nullopt;
    return PkiCredential{r[0][0].as<std::string>(), r[0][1].as<std::string>(),
                         r[0][2].as<std::string>(), r[0][3].as<std::string>(),
                         r[0][4].as<std::string>()};
}

void Database::remove_pki_credential(const std::string& id, const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params("DELETE FROM pki_credentials WHERE id = $1 AND user_id = $2", id, user_id);
    txn.commit();
}

std::optional<User> Database::find_user_by_pki_key(const std::string& public_key_spki) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT u.id, u.username, u.display_name, u.public_key, u.role, u.is_online, "
        "u.last_seen::text, u.created_at::text, u.bio, u.status, u.avatar_file_id, u.profile_color, u.is_banned "
        "FROM users u JOIN pki_credentials pc ON u.id = pc.user_id "
        "WHERE pc.public_key = $1",
        public_key_spki);
    txn.commit();
    if (r.empty()) return std::nullopt;
    return User{r[0][0].as<std::string>(), r[0][1].as<std::string>(),
                r[0][2].as<std::string>(),
                r[0][3].is_null() ? "" : r[0][3].as<std::string>(),
                r[0][4].as<std::string>(), r[0][5].as<bool>(),
                r[0][6].is_null() ? "" : r[0][6].as<std::string>(),
                r[0][7].as<std::string>(),
                r[0][8].is_null() ? "" : r[0][8].as<std::string>(),
                r[0][9].is_null() ? "" : r[0][9].as<std::string>(),
                r[0][10].is_null() ? "" : r[0][10].as<std::string>(),
                r[0][11].is_null() ? "" : r[0][11].as<std::string>(),
                r[0][12].as<bool>()};
}

// --- Recovery Keys ---

void Database::store_recovery_keys(const std::string& user_id, const std::vector<std::string>& key_hashes) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    for (const auto& hash : key_hashes) {
        txn.exec_params(
            "INSERT INTO recovery_keys (user_id, key_hash) VALUES ($1, $2)",
            user_id, hash);
    }
    txn.commit();
}

std::optional<std::string> Database::verify_and_consume_recovery_key(const std::string& key_hash) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT id, user_id FROM recovery_keys WHERE key_hash = $1 AND used = false LIMIT 1",
        key_hash);
    if (r.empty()) {
        txn.commit();
        return std::nullopt;
    }
    std::string id = r[0][0].as<std::string>();
    std::string user_id = r[0][1].as<std::string>();
    txn.exec_params("UPDATE recovery_keys SET used = true WHERE id = $1", id);
    txn.commit();
    return user_id;
}

int Database::count_remaining_recovery_keys(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT COUNT(*) FROM recovery_keys WHERE user_id = $1 AND used = false",
        user_id);
    txn.commit();
    return r[0][0].as<int>();
}

void Database::delete_recovery_keys(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params("DELETE FROM recovery_keys WHERE user_id = $1", user_id);
    txn.commit();
}

int Database::count_user_credentials(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT (SELECT COUNT(*) FROM webauthn_credentials WHERE user_id = $1) + "
        "(SELECT COUNT(*) FROM pki_credentials WHERE user_id = $1)",
        user_id);
    txn.commit();
    return r[0][0].as<int>();
}

// --- Recovery Tokens ---

std::string Database::create_recovery_token(const std::string& created_by,
                                             const std::string& for_user_id,
                                             int expiry_hours) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string token = random_hex(32);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "INSERT INTO recovery_tokens (token, created_by, for_user_id, expires_at) "
        "VALUES ($1, $2, $3, NOW() + ($4 || ' hours')::interval)",
        token, created_by, for_user_id, std::to_string(expiry_hours));
    txn.commit();
    return token;
}

std::optional<std::string> Database::get_recovery_token_user_id(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT for_user_id FROM recovery_tokens "
        "WHERE token = $1 AND used = FALSE AND expires_at > NOW()",
        token);
    txn.commit();
    if (r.empty()) return std::nullopt;
    return r[0][0].as<std::string>();
}

void Database::use_recovery_token(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params("UPDATE recovery_tokens SET used = TRUE, used_at = NOW() WHERE token = $1", token);
    txn.commit();
}

bool Database::delete_recovery_token(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "DELETE FROM recovery_tokens WHERE id = $1 AND used = FALSE RETURNING id", id);
    txn.commit();
    return !r.empty();
}

std::vector<Database::RecoveryTokenInfo> Database::list_recovery_tokens() {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec(
        "SELECT rt.id, rt.token, COALESCE(c.username, 'system'), "
        "COALESCE(u.username, 'unknown'), rt.for_user_id, rt.used, "
        "rt.expires_at::text, rt.created_at::text, "
        "COALESCE(rt.used_at::text, '') "
        "FROM recovery_tokens rt "
        "LEFT JOIN users c ON rt.created_by = c.id "
        "LEFT JOIN users u ON rt.for_user_id = u.id "
        "ORDER BY rt.created_at DESC");
    txn.commit();
    std::vector<RecoveryTokenInfo> tokens;
    for (const auto& row : r) {
        tokens.push_back({row[0].as<std::string>(), row[1].as<std::string>(),
                          row[2].as<std::string>(), row[3].as<std::string>(),
                          row[4].as<std::string>(), row[5].as<bool>(),
                          row[6].as<std::string>(), row[7].as<std::string>(),
                          row[8].as<std::string>()});
    }
    return tokens;
}

// --- Read State / Unread Counts ---

void Database::update_read_state(const std::string& channel_id, const std::string& user_id,
                                  const std::string& message_id, const std::string& timestamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "INSERT INTO channel_read_state (channel_id, user_id, last_read_at, last_read_message_id) "
        "VALUES ($1, $2, $3::timestamptz, $4::uuid) "
        "ON CONFLICT (channel_id, user_id) DO UPDATE "
        "SET last_read_at = GREATEST(channel_read_state.last_read_at, EXCLUDED.last_read_at), "
        "    last_read_message_id = CASE WHEN EXCLUDED.last_read_at > channel_read_state.last_read_at "
        "                           THEN EXCLUDED.last_read_message_id ELSE channel_read_state.last_read_message_id END",
        channel_id, user_id, timestamp, message_id);
    // Clear mention records for messages up to the read point
    txn.exec_params(
        "DELETE FROM mentions WHERE channel_id = $1 AND mentioned_user_id = $2 "
        "AND message_id IN (SELECT id FROM messages WHERE channel_id = $1 AND created_at <= $3::timestamptz)",
        channel_id, user_id, timestamp);
    txn.commit();
}

std::vector<Database::UnreadCount> Database::get_unread_counts(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT cm.channel_id, COUNT(m.id)::int AS unread "
        "FROM channel_members cm "
        "LEFT JOIN channel_read_state crs "
        "  ON crs.channel_id = cm.channel_id AND crs.user_id = cm.user_id "
        "JOIN messages m ON m.channel_id = cm.channel_id "
        "  AND m.is_deleted = false "
        "  AND (crs.last_read_at IS NULL OR m.created_at > crs.last_read_at) "
        "  AND m.user_id != cm.user_id "
        "WHERE cm.user_id = $1 "
        "GROUP BY cm.channel_id "
        "HAVING COUNT(m.id) > 0",
        user_id);
    txn.commit();
    std::vector<UnreadCount> counts;
    for (const auto& row : r) {
        counts.push_back({row[0].as<std::string>(), row[1].as<int>()});
    }
    return counts;
}

std::vector<Database::UnreadCount> Database::get_mention_unread_counts(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT mn.channel_id, COUNT(DISTINCT mn.message_id)::int AS unread "
        "FROM mentions mn "
        "JOIN messages m ON m.id = mn.message_id AND m.is_deleted = false "
        "JOIN channels c ON c.id = mn.channel_id AND c.space_id IS NOT NULL "
        "LEFT JOIN channel_read_state crs "
        "  ON crs.channel_id = mn.channel_id AND crs.user_id = $1 "
        "WHERE mn.mentioned_user_id = $1 "
        "  AND (crs.last_read_at IS NULL OR m.created_at > crs.last_read_at) "
        "GROUP BY mn.channel_id "
        "HAVING COUNT(DISTINCT mn.message_id) > 0",
        user_id);
    txn.commit();
    std::vector<UnreadCount> counts;
    for (const auto& row : r) {
        counts.push_back({row[0].as<std::string>(), row[1].as<int>()});
    }
    return counts;
}

std::vector<Database::ReadReceipt> Database::get_channel_read_receipts(const std::string& channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT crs.user_id, u.username, "
        "COALESCE(crs.last_read_message_id::text, ''), crs.last_read_at::text "
        "FROM channel_read_state crs "
        "JOIN users u ON u.id = crs.user_id "
        "WHERE crs.channel_id = $1",
        channel_id);
    txn.commit();
    std::vector<ReadReceipt> receipts;
    for (const auto& row : r) {
        receipts.push_back({row[0].as<std::string>(), row[1].as<std::string>(),
                            row[2].as<std::string>(), row[3].as<std::string>()});
    }
    return receipts;
}

std::vector<Database::ChannelMemberUsername> Database::get_channel_member_usernames(const std::string& channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT cm.user_id, u.username FROM channel_members cm "
        "JOIN users u ON u.id = cm.user_id WHERE cm.channel_id = $1",
        channel_id);
    txn.commit();
    std::vector<ChannelMemberUsername> members;
    for (const auto& row : r) {
        members.push_back({row[0].as<std::string>(), row[1].as<std::string>()});
    }
    return members;
}

void Database::store_mentions(const std::string& message_id, const std::string& channel_id,
                               const std::string& content,
                               const std::vector<ChannelMemberUsername>& members,
                               const std::string& sender_user_id) {
    // Parse @username and @channel from content
    std::vector<std::string> mentioned_tokens;
    size_t pos = 0;
    while (pos < content.size()) {
        auto at = content.find('@', pos);
        if (at == std::string::npos) break;
        size_t start = at + 1;
        size_t end = start;
        while (end < content.size() && (std::isalnum(content[end]) || content[end] == '_' || content[end] == '-')) {
            ++end;
        }
        if (end > start) {
            mentioned_tokens.push_back(content.substr(start, end - start));
        }
        pos = end;
    }

    if (mentioned_tokens.empty()) return;

    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());

    bool is_channel_mention = false;
    for (const auto& token : mentioned_tokens) {
        if (token == "channel") {
            is_channel_mention = true;
            continue;
        }
        // Find matching member
        for (const auto& m : members) {
            if (m.username == token && m.user_id != sender_user_id) {
                txn.exec_params(
                    "INSERT INTO mentions (message_id, channel_id, mentioned_user_id, is_channel_mention) "
                    "VALUES ($1::uuid, $2::uuid, $3::uuid, false) ON CONFLICT DO NOTHING",
                    message_id, channel_id, m.user_id);
                break;
            }
        }
    }

    // @channel → insert for all members except the sender
    if (is_channel_mention) {
        for (const auto& m : members) {
            if (m.user_id == sender_user_id) continue;
            txn.exec_params(
                "INSERT INTO mentions (message_id, channel_id, mentioned_user_id, is_channel_mention) "
                "VALUES ($1::uuid, $2::uuid, $3::uuid, true) ON CONFLICT DO NOTHING",
                message_id, channel_id, m.user_id);
        }
    }

    txn.commit();
}

// --- Search ---

std::vector<User> Database::search_users(const std::string& query, int limit, int offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT id, username, display_name, public_key, role, is_online, "
        "last_seen::text, created_at::text, bio, status, avatar_file_id, profile_color "
        "FROM users "
        "WHERE username ILIKE '%' || $1 || '%' OR display_name ILIKE '%' || $1 || '%' "
        "ORDER BY CASE WHEN username ILIKE $1 || '%' THEN 0 "
        "              WHEN display_name ILIKE $1 || '%' THEN 1 ELSE 2 END, username "
        "LIMIT $2 OFFSET $3",
        query, limit, offset);
    txn.commit();
    std::vector<User> users;
    for (const auto& row : r) {
        users.push_back(User{
            row[0].as<std::string>(), row[1].as<std::string>(),
            row[2].as<std::string>(), row[3].is_null() ? "" : row[3].as<std::string>(),
            row[4].as<std::string>(), row[5].as<bool>(),
            row[6].is_null() ? "" : row[6].as<std::string>(),
            row[7].as<std::string>(),
            row[8].is_null() ? "" : row[8].as<std::string>(),
            row[9].is_null() ? "" : row[9].as<std::string>(),
            row[10].is_null() ? "" : row[10].as<std::string>(),
            row[11].is_null() ? "" : row[11].as<std::string>()});
    }
    return users;
}

std::vector<Database::MessageSearchResult> Database::search_messages(
    const std::string& tsquery_expr, const std::string& user_id,
    bool is_admin, int limit, int offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());

    std::string sql =
        "SELECT m.id, m.channel_id, c.name, COALESCE(s.name,''), "
        "m.user_id, u.username, m.content, m.created_at::text, c.is_direct "
        "FROM messages m "
        "JOIN users u ON m.user_id = u.id "
        "JOIN channels c ON m.channel_id = c.id "
        "LEFT JOIN spaces s ON c.space_id = s.id "
        "WHERE m.content_tsv @@ (" + tsquery_expr + ") "
        "AND m.is_deleted = false "
        "AND (EXISTS (SELECT 1 FROM channel_members cm WHERE cm.channel_id = m.channel_id AND cm.user_id = $1)";
    if (is_admin) {
        sql += " OR c.is_direct = false";
    }
    sql += ") ORDER BY m.created_at DESC LIMIT $2 OFFSET $3";

    auto r = txn.exec_params(sql, user_id, limit, offset);
    txn.commit();

    std::vector<MessageSearchResult> results;
    for (const auto& row : r) {
        results.push_back(MessageSearchResult{
            row[0].as<std::string>(), row[1].as<std::string>(),
            row[2].is_null() ? "" : row[2].as<std::string>(),
            row[3].as<std::string>(),
            row[4].as<std::string>(), row[5].as<std::string>(),
            row[6].as<std::string>(), row[7].as<std::string>(),
            row[8].as<bool>()});
    }
    return results;
}

std::vector<Database::MessageSearchResult> Database::browse_messages(
    const std::string& user_id, bool is_admin, int limit, int offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());

    std::string sql =
        "SELECT m.id, m.channel_id, c.name, COALESCE(s.name,''), "
        "m.user_id, u.username, m.content, m.created_at::text, c.is_direct "
        "FROM messages m "
        "JOIN users u ON m.user_id = u.id "
        "JOIN channels c ON m.channel_id = c.id "
        "LEFT JOIN spaces s ON c.space_id = s.id "
        "WHERE m.is_deleted = false "
        "AND (EXISTS (SELECT 1 FROM channel_members cm WHERE cm.channel_id = m.channel_id AND cm.user_id = $1)";
    if (is_admin) {
        sql += " OR c.is_direct = false";
    }
    sql += ") ORDER BY m.created_at DESC LIMIT $2 OFFSET $3";

    auto r = txn.exec_params(sql, user_id, limit, offset);
    txn.commit();

    std::vector<MessageSearchResult> results;
    for (const auto& row : r) {
        results.push_back(MessageSearchResult{
            row[0].as<std::string>(), row[1].as<std::string>(),
            row[2].is_null() ? "" : row[2].as<std::string>(),
            row[3].as<std::string>(),
            row[4].as<std::string>(), row[5].as<std::string>(),
            row[6].as<std::string>(), row[7].as<std::string>(),
            row[8].as<bool>()});
    }
    return results;
}

std::vector<Database::FileSearchResult> Database::search_files(
    const std::string& query, const std::string& user_id,
    bool is_admin, int limit, int offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());

    std::string sql =
        "SELECT m.id, m.channel_id, c.name, "
        "m.user_id, u.username, m.file_id, m.file_name, m.file_type, "
        "m.created_at::text, m.file_size "
        "FROM messages m "
        "JOIN users u ON m.user_id = u.id "
        "JOIN channels c ON m.channel_id = c.id "
        "WHERE m.file_id IS NOT NULL AND m.file_id != '' "
        "AND m.file_name ILIKE '%' || $1 || '%' "
        "AND m.is_deleted = false "
        "AND (EXISTS (SELECT 1 FROM channel_members cm WHERE cm.channel_id = m.channel_id AND cm.user_id = $2)";
    if (is_admin) {
        sql += " OR c.is_direct = false";
    }
    sql += ") ORDER BY m.created_at DESC LIMIT $3 OFFSET $4";

    auto r = txn.exec_params(sql, query, user_id, limit, offset);
    txn.commit();

    std::vector<FileSearchResult> results;
    for (const auto& row : r) {
        results.push_back(FileSearchResult{
            row[0].as<std::string>(), row[1].as<std::string>(),
            row[2].is_null() ? "" : row[2].as<std::string>(),
            row[3].as<std::string>(), row[4].as<std::string>(),
            row[5].as<std::string>(), row[6].as<std::string>(),
            row[7].is_null() ? "" : row[7].as<std::string>(),
            row[8].as<std::string>(),
            row[9].is_null() ? 0 : row[9].as<int64_t>()});
    }
    return results;
}

std::vector<Database::SpaceSearchResult> Database::search_spaces(
    const std::string& query, const std::string& user_id,
    bool is_admin, int limit, int offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());

    std::string sql =
        "SELECT s.id, s.name, s.description, s.is_public, "
        "COALESCE(s.avatar_file_id, ''), COALESCE(s.profile_color, '') "
        "FROM spaces s "
        "WHERE (s.name ILIKE '%' || $1 || '%' OR s.description ILIKE '%' || $1 || '%') "
        "AND (EXISTS (SELECT 1 FROM space_members sm WHERE sm.space_id = s.id AND sm.user_id = $2)";
    if (is_admin) {
        sql += " OR true";
    }
    sql += ") ORDER BY s.name LIMIT $3 OFFSET $4";

    auto r = txn.exec_params(sql, query, user_id, limit, offset);
    txn.commit();

    std::vector<SpaceSearchResult> results;
    for (const auto& row : r) {
        results.push_back(SpaceSearchResult{
            row[0].as<std::string>(), row[1].as<std::string>(),
            row[2].is_null() ? "" : row[2].as<std::string>(),
            row[3].as<bool>(),
            row[4].as<std::string>(), row[5].as<std::string>()});
    }
    return results;
}

std::vector<Database::ChannelSearchResult> Database::search_channels(
    const std::string& query, const std::string& user_id,
    bool is_admin, int limit, int offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());

    std::string sql =
        "SELECT c.id, c.name, c.description, COALESCE(s.name,''), "
        "COALESCE(c.space_id::text,''), c.is_public "
        "FROM channels c "
        "LEFT JOIN spaces s ON c.space_id = s.id "
        "WHERE c.is_direct = false "
        "AND (c.name ILIKE '%' || $1 || '%' OR c.description ILIKE '%' || $1 || '%') "
        "AND (EXISTS (SELECT 1 FROM channel_members cm WHERE cm.channel_id = c.id AND cm.user_id = $2)";
    if (is_admin) {
        sql += " OR true";
    }
    sql += ") ORDER BY c.name LIMIT $3 OFFSET $4";

    auto r = txn.exec_params(sql, query, user_id, limit, offset);
    txn.commit();

    std::vector<ChannelSearchResult> results;
    for (const auto& row : r) {
        results.push_back(ChannelSearchResult{
            row[0].as<std::string>(), row[1].as<std::string>(),
            row[2].is_null() ? "" : row[2].as<std::string>(),
            row[3].as<std::string>(),
            row[4].as<std::string>(),
            row[5].as<bool>()});
    }
    return results;
}

// Helper: build message-table filter clauses from composite filters
static void build_message_clauses(pqxx::work& txn,
    const std::vector<Database::CompositeFilter>& filters,
    std::vector<std::string>& clauses) {
    for (const auto& f : filters) {
        if (f.type == "messages") {
            clauses.push_back(
                "m.content_tsv @@ websearch_to_tsquery('english', " +
                txn.quote(f.value) + ")");
        } else if (f.type == "users") {
            clauses.push_back(
                "u.username ILIKE '%' || " + txn.quote(f.value) + " || '%'");
        } else if (f.type == "files") {
            clauses.push_back(
                "(m.file_id IS NOT NULL AND m.file_id != '' AND m.file_name ILIKE '%' || " +
                txn.quote(f.value) + " || '%')");
        } else if (f.type == "channels") {
            clauses.push_back(
                "c.name ILIKE '%' || " + txn.quote(f.value) + " || '%'");
        } else if (f.type == "spaces") {
            clauses.push_back(
                "s.name ILIKE '%' || " + txn.quote(f.value) + " || '%'");
        }
    }
}

static std::string join_clauses(const std::vector<std::string>& clauses,
                                  const std::string& mode) {
    if (clauses.empty()) return "";
    std::string connector = (mode == "or") ? " OR " : " AND ";
    std::string result = " AND (";
    for (size_t i = 0; i < clauses.size(); i++) {
        if (i > 0) result += connector;
        result += clauses[i];
    }
    result += ")";
    return result;
}

std::vector<Database::MessageSearchResult> Database::search_composite_messages(
    const std::vector<CompositeFilter>& filters, const std::string& mode,
    const std::string& user_id, bool is_admin, int limit, int offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());

    std::string sql =
        "SELECT m.id, m.channel_id, c.name, COALESCE(s.name,''), "
        "m.user_id, u.username, m.content, m.created_at::text, c.is_direct "
        "FROM messages m "
        "JOIN users u ON m.user_id = u.id "
        "JOIN channels c ON m.channel_id = c.id "
        "LEFT JOIN spaces s ON c.space_id = s.id "
        "WHERE m.is_deleted = false "
        "AND (EXISTS (SELECT 1 FROM channel_members cm WHERE cm.channel_id = m.channel_id AND cm.user_id = " +
        txn.quote(user_id) + ")";
    if (is_admin) sql += " OR c.is_direct = false";
    sql += ")";

    std::vector<std::string> clauses;
    build_message_clauses(txn, filters, clauses);
    sql += join_clauses(clauses, mode);
    sql += " ORDER BY m.created_at DESC LIMIT " + std::to_string(limit) +
           " OFFSET " + std::to_string(offset);

    auto r = txn.exec(sql);
    txn.commit();

    std::vector<MessageSearchResult> results;
    for (const auto& row : r) {
        results.push_back(MessageSearchResult{
            row[0].as<std::string>(), row[1].as<std::string>(),
            row[2].is_null() ? "" : row[2].as<std::string>(),
            row[3].as<std::string>(),
            row[4].as<std::string>(), row[5].as<std::string>(),
            row[6].as<std::string>(), row[7].as<std::string>(),
            row[8].as<bool>()});
    }
    return results;
}

std::vector<Database::FileSearchResult> Database::search_composite_files(
    const std::vector<CompositeFilter>& filters, const std::string& mode,
    const std::string& user_id, bool is_admin, int limit, int offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());

    std::string sql =
        "SELECT m.id, m.channel_id, c.name, "
        "m.user_id, u.username, m.file_id, m.file_name, m.file_type, "
        "m.created_at::text, m.file_size "
        "FROM messages m "
        "JOIN users u ON m.user_id = u.id "
        "JOIN channels c ON m.channel_id = c.id "
        "LEFT JOIN spaces s ON c.space_id = s.id "
        "WHERE m.file_id IS NOT NULL AND m.file_id != '' "
        "AND m.is_deleted = false "
        "AND (EXISTS (SELECT 1 FROM channel_members cm WHERE cm.channel_id = m.channel_id AND cm.user_id = " +
        txn.quote(user_id) + ")";
    if (is_admin) sql += " OR c.is_direct = false";
    sql += ")";

    std::vector<std::string> clauses;
    build_message_clauses(txn, filters, clauses);
    sql += join_clauses(clauses, mode);
    sql += " ORDER BY m.created_at DESC LIMIT " + std::to_string(limit) +
           " OFFSET " + std::to_string(offset);

    auto r = txn.exec(sql);
    txn.commit();

    std::vector<FileSearchResult> results;
    for (const auto& row : r) {
        results.push_back(FileSearchResult{
            row[0].as<std::string>(), row[1].as<std::string>(),
            row[2].is_null() ? "" : row[2].as<std::string>(),
            row[3].as<std::string>(), row[4].as<std::string>(),
            row[5].as<std::string>(), row[6].as<std::string>(),
            row[7].is_null() ? "" : row[7].as<std::string>(),
            row[8].as<std::string>(),
            row[9].is_null() ? 0 : row[9].as<int64_t>()});
    }
    return results;
}

std::vector<User> Database::search_composite_users(
    const std::vector<CompositeFilter>& filters, const std::string& mode,
    const std::string& user_id, bool is_admin, int limit, int offset) {
    (void)is_admin;
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());

    std::string sql =
        "SELECT DISTINCT u.id, u.username, u.display_name, u.public_key, u.role, "
        "u.is_online, u.last_seen::text, u.created_at::text, u.bio, u.status, u.avatar_file_id, u.profile_color "
        "FROM users u ";

    std::vector<std::string> clauses;
    bool needs_channels = false, needs_spaces = false;

    for (const auto& f : filters) {
        if (f.type == "users") {
            clauses.push_back(
                "(u.username ILIKE '%' || " + txn.quote(f.value) +
                " || '%' OR u.display_name ILIKE '%' || " + txn.quote(f.value) + " || '%')");
        } else if (f.type == "channels") {
            needs_channels = true;
            clauses.push_back(
                "ch.name ILIKE '%' || " + txn.quote(f.value) + " || '%'");
        } else if (f.type == "spaces") {
            needs_spaces = true;
            clauses.push_back(
                "sp.name ILIKE '%' || " + txn.quote(f.value) + " || '%'");
        } else if (f.type == "messages") {
            clauses.push_back(
                "EXISTS (SELECT 1 FROM messages m_t WHERE m_t.user_id = u.id "
                "AND m_t.is_deleted = false "
                "AND m_t.content_tsv @@ websearch_to_tsquery('english', " + txn.quote(f.value) + "))");
        } else if (f.type == "files") {
            clauses.push_back(
                "EXISTS (SELECT 1 FROM messages m_f WHERE m_f.user_id = u.id "
                "AND m_f.is_deleted = false AND m_f.file_id IS NOT NULL AND m_f.file_id != '' "
                "AND m_f.file_name ILIKE '%' || " + txn.quote(f.value) + " || '%')");
        }
    }

    if (needs_channels) {
        sql += "JOIN channel_members cm_u ON cm_u.user_id = u.id "
               "JOIN channels ch ON ch.id = cm_u.channel_id AND ch.is_direct = false ";
    }
    if (needs_spaces) {
        sql += "JOIN space_members sm_u ON sm_u.user_id = u.id "
               "JOIN spaces sp ON sp.id = sm_u.space_id ";
    }

    sql += "WHERE true";
    (void)user_id;
    sql += join_clauses(clauses, mode);
    sql += " ORDER BY u.username LIMIT " + std::to_string(limit) +
           " OFFSET " + std::to_string(offset);

    auto r = txn.exec(sql);
    txn.commit();

    std::vector<User> users;
    for (const auto& row : r) {
        users.push_back(User{
            row[0].as<std::string>(), row[1].as<std::string>(),
            row[2].as<std::string>(), row[3].is_null() ? "" : row[3].as<std::string>(),
            row[4].as<std::string>(), row[5].as<bool>(),
            row[6].is_null() ? "" : row[6].as<std::string>(),
            row[7].as<std::string>(),
            row[8].is_null() ? "" : row[8].as<std::string>(),
            row[9].is_null() ? "" : row[9].as<std::string>(),
            row[10].is_null() ? "" : row[10].as<std::string>(),
            row[11].is_null() ? "" : row[11].as<std::string>()});
    }
    return users;
}

std::vector<Database::ChannelSearchResult> Database::search_composite_channels(
    const std::vector<CompositeFilter>& filters, const std::string& mode,
    const std::string& user_id, bool is_admin, int limit, int offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());

    std::string sql =
        "SELECT DISTINCT c.id, c.name, c.description, COALESCE(s.name,''), "
        "COALESCE(c.space_id::text,''), c.is_public "
        "FROM channels c "
        "LEFT JOIN spaces s ON c.space_id = s.id ";

    bool needs_user_join = false;
    for (const auto& f : filters) {
        if (f.type == "users") { needs_user_join = true; break; }
    }
    if (needs_user_join) {
        sql += "JOIN channel_members cm_f ON cm_f.channel_id = c.id "
               "JOIN users uf ON uf.id = cm_f.user_id ";
    }

    sql += "WHERE c.is_direct = false "
           "AND (EXISTS (SELECT 1 FROM channel_members cm WHERE cm.channel_id = c.id AND cm.user_id = " +
           txn.quote(user_id) + ")";
    if (is_admin) sql += " OR true";
    sql += ")";

    std::vector<std::string> clauses;
    for (const auto& f : filters) {
        if (f.type == "channels") {
            clauses.push_back(
                "(c.name ILIKE '%' || " + txn.quote(f.value) +
                " || '%' OR c.description ILIKE '%' || " + txn.quote(f.value) + " || '%')");
        } else if (f.type == "spaces") {
            clauses.push_back(
                "s.name ILIKE '%' || " + txn.quote(f.value) + " || '%'");
        } else if (f.type == "users") {
            clauses.push_back(
                "uf.username ILIKE '%' || " + txn.quote(f.value) + " || '%'");
        } else if (f.type == "messages") {
            clauses.push_back(
                "EXISTS (SELECT 1 FROM messages m_t WHERE m_t.channel_id = c.id "
                "AND m_t.is_deleted = false "
                "AND m_t.content_tsv @@ websearch_to_tsquery('english', " + txn.quote(f.value) + "))");
        } else if (f.type == "files") {
            clauses.push_back(
                "EXISTS (SELECT 1 FROM messages m_f WHERE m_f.channel_id = c.id "
                "AND m_f.is_deleted = false AND m_f.file_id IS NOT NULL AND m_f.file_id != '' "
                "AND m_f.file_name ILIKE '%' || " + txn.quote(f.value) + " || '%')");
        }
    }

    sql += join_clauses(clauses, mode);
    sql += " ORDER BY c.name LIMIT " + std::to_string(limit) +
           " OFFSET " + std::to_string(offset);

    auto r = txn.exec(sql);
    txn.commit();

    std::vector<ChannelSearchResult> results;
    for (const auto& row : r) {
        results.push_back(ChannelSearchResult{
            row[0].as<std::string>(), row[1].as<std::string>(),
            row[2].is_null() ? "" : row[2].as<std::string>(),
            row[3].as<std::string>(),
            row[4].as<std::string>(),
            row[5].as<bool>()});
    }
    return results;
}

std::vector<Database::SpaceSearchResult> Database::search_composite_spaces(
    const std::vector<CompositeFilter>& filters, const std::string& mode,
    const std::string& user_id, bool is_admin, int limit, int offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());

    std::string sql =
        "SELECT DISTINCT s.id, s.name, s.description, s.is_public, "
        "COALESCE(s.avatar_file_id, ''), COALESCE(s.profile_color, '') "
        "FROM spaces s ";

    bool needs_user_join = false;
    for (const auto& f : filters) {
        if (f.type == "users") { needs_user_join = true; break; }
    }
    if (needs_user_join) {
        sql += "JOIN space_members sm_f ON sm_f.space_id = s.id "
               "JOIN users uf ON uf.id = sm_f.user_id ";
    }

    sql += "WHERE (EXISTS (SELECT 1 FROM space_members sm WHERE sm.space_id = s.id AND sm.user_id = " +
           txn.quote(user_id) + ")";
    if (is_admin) sql += " OR true";
    sql += ")";

    std::vector<std::string> clauses;
    for (const auto& f : filters) {
        if (f.type == "spaces") {
            clauses.push_back(
                "(s.name ILIKE '%' || " + txn.quote(f.value) +
                " || '%' OR s.description ILIKE '%' || " + txn.quote(f.value) + " || '%')");
        } else if (f.type == "users") {
            clauses.push_back(
                "uf.username ILIKE '%' || " + txn.quote(f.value) + " || '%'");
        } else if (f.type == "messages") {
            clauses.push_back(
                "EXISTS (SELECT 1 FROM channels ch_t "
                "JOIN messages m_t ON m_t.channel_id = ch_t.id "
                "WHERE ch_t.space_id = s.id AND m_t.is_deleted = false "
                "AND m_t.content_tsv @@ websearch_to_tsquery('english', " + txn.quote(f.value) + "))");
        } else if (f.type == "files") {
            clauses.push_back(
                "EXISTS (SELECT 1 FROM channels ch_f "
                "JOIN messages m_f ON m_f.channel_id = ch_f.id "
                "WHERE ch_f.space_id = s.id AND m_f.is_deleted = false "
                "AND m_f.file_id IS NOT NULL AND m_f.file_id != '' "
                "AND m_f.file_name ILIKE '%' || " + txn.quote(f.value) + " || '%')");
        } else if (f.type == "channels") {
            clauses.push_back(
                "EXISTS (SELECT 1 FROM channels ch_c WHERE ch_c.space_id = s.id "
                "AND ch_c.name ILIKE '%' || " + txn.quote(f.value) + " || '%')");
        }
    }

    sql += join_clauses(clauses, mode);
    sql += " ORDER BY s.name LIMIT " + std::to_string(limit) +
           " OFFSET " + std::to_string(offset);

    auto r = txn.exec(sql);
    txn.commit();

    std::vector<SpaceSearchResult> results;
    for (const auto& row : r) {
        results.push_back(SpaceSearchResult{
            row[0].as<std::string>(), row[1].as<std::string>(),
            row[2].is_null() ? "" : row[2].as<std::string>(),
            row[3].as<bool>(),
            row[4].as<std::string>(), row[5].as<std::string>()});
    }
    return results;
}

std::vector<Message> Database::get_messages_around(const std::string& channel_id,
                                                     const std::string& message_id, int limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    int half = limit / 2 + 1;
    std::string sql =
        std::string("SELECT * FROM (") +
        "(SELECT " + MSG_COLS_JOINED + " FROM messages m JOIN users u ON m.user_id = u.id "
        "WHERE m.channel_id = $1 AND m.created_at <= (SELECT created_at FROM messages WHERE id = $2) "
        "ORDER BY m.created_at DESC LIMIT $3) "
        "UNION ALL "
        "(SELECT " + MSG_COLS_JOINED + " FROM messages m JOIN users u ON m.user_id = u.id "
        "WHERE m.channel_id = $1 AND m.created_at > (SELECT created_at FROM messages WHERE id = $2) "
        "ORDER BY m.created_at ASC LIMIT $3)"
        ") sub ORDER BY created_at ASC";
    auto r = txn.exec_params(sql, channel_id, message_id, half);
    txn.commit();

    std::vector<Message> msgs;
    for (const auto& row : r) {
        msgs.push_back(row_to_message(row));
    }
    return msgs;
}

// --- Notifications ---

std::string Database::create_notification(const std::string& user_id, const std::string& type,
    const std::string& source_user_id, const std::string& channel_id,
    const std::string& message_id, const std::string& content) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "INSERT INTO notifications (user_id, type, source_user_id, channel_id, message_id, content) "
        "VALUES ($1, $2, NULLIF($3,'')::uuid, NULLIF($4,'')::uuid, NULLIF($5,'')::uuid, $6) "
        "RETURNING id::text, created_at::text",
        user_id, type, source_user_id, channel_id, message_id, content);
    txn.commit();
    return r[0][0].as<std::string>();
}

std::vector<Database::Notification> Database::get_notifications(const std::string& user_id, int limit, int offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT n.id, n.user_id, n.type, COALESCE(n.source_user_id::text,''), "
        "COALESCE(u.username,''), COALESCE(n.channel_id::text,''), "
        "COALESCE(c.name,''), COALESCE(n.message_id::text,''), "
        "COALESCE(c.space_id::text,''), COALESCE(n.content,''), "
        "n.created_at::text, n.is_read "
        "FROM notifications n "
        "LEFT JOIN users u ON n.source_user_id = u.id "
        "LEFT JOIN channels c ON n.channel_id = c.id "
        "WHERE n.user_id = $1 "
        "ORDER BY n.created_at DESC "
        "LIMIT $2 OFFSET $3",
        user_id, limit, offset);
    txn.commit();
    std::vector<Notification> results;
    for (const auto& row : r) {
        results.push_back(Notification{
            row[0].as<std::string>(), row[1].as<std::string>(),
            row[2].as<std::string>(), row[3].as<std::string>(),
            row[4].as<std::string>(), row[5].as<std::string>(),
            row[6].as<std::string>(), row[7].as<std::string>(),
            row[8].as<std::string>(), row[9].as<std::string>(),
            row[10].as<std::string>(), row[11].as<bool>()});
    }
    return results;
}

int Database::get_unread_notification_count(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT COUNT(*) FROM notifications WHERE user_id = $1 AND is_read = false",
        user_id);
    txn.commit();
    return r[0][0].as<int>();
}

void Database::mark_notification_read(const std::string& notification_id, const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "UPDATE notifications SET is_read = true WHERE id = $1 AND user_id = $2",
        notification_id, user_id);
    txn.commit();
}

void Database::mark_all_notifications_read(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "UPDATE notifications SET is_read = true WHERE user_id = $1 AND is_read = false",
        user_id);
    txn.commit();
}

int Database::mark_channel_notifications_read(const std::string& channel_id, const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "UPDATE notifications SET is_read = true WHERE user_id = $1 AND channel_id = $2 AND is_read = false RETURNING id",
        user_id, channel_id);
    txn.commit();
    return static_cast<int>(r.size());
}

// --- Archive management ---

void Database::archive_channel(const std::string& channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params("UPDATE channels SET is_archived = TRUE WHERE id = $1", channel_id);
    txn.commit();
}

void Database::unarchive_channel(const std::string& channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params("UPDATE channels SET is_archived = FALSE WHERE id = $1", channel_id);
    txn.commit();
}

void Database::archive_space(const std::string& space_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params("UPDATE spaces SET is_archived = TRUE WHERE id = $1", space_id);
    txn.exec_params("UPDATE channels SET is_archived = TRUE WHERE space_id = $1", space_id);
    txn.commit();
}

void Database::unarchive_space(const std::string& space_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params("UPDATE spaces SET is_archived = FALSE WHERE id = $1", space_id);
    txn.exec_params("UPDATE channels SET is_archived = FALSE WHERE space_id = $1", space_id);
    txn.commit();
}

bool Database::is_server_archived() {
    auto val = get_setting("server_archived");
    return val && *val == "true";
}

void Database::set_server_archived(bool archived) {
    set_setting("server_archived", archived ? "true" : "false");
}

int Database::count_channel_members_with_role(const std::string& channel_id, const std::string& role) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT COUNT(*) FROM channel_members WHERE channel_id = $1 AND role = $2",
        channel_id, role);
    txn.commit();
    return r[0][0].as<int>();
}

int Database::count_space_members_with_role(const std::string& space_id, const std::string& role) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT COUNT(*) FROM space_members WHERE space_id = $1 AND role = $2",
        space_id, role);
    txn.commit();
    return r[0][0].as<int>();
}

int Database::count_users_with_role(const std::string& role) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params("SELECT COUNT(*) FROM users WHERE role = $1", role);
    txn.commit();
    return r[0][0].as<int>();
}

int Database::count_channel_members(const std::string& channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT COUNT(*) FROM channel_members WHERE channel_id = $1", channel_id);
    txn.commit();
    return r[0][0].as<int>();
}

// --- Password credentials ---

std::optional<User> Database::find_user_by_username(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT u.id, u.username, u.display_name, u.public_key, u.role, u.is_online, "
        "u.last_seen::text, u.created_at::text, u.bio, u.status, u.avatar_file_id, u.profile_color, u.is_banned "
        "FROM users u WHERE u.username = $1", username);
    txn.commit();
    if (r.empty()) return std::nullopt;
    return User{
        r[0][0].as<std::string>(), r[0][1].as<std::string>(),
        r[0][2].as<std::string>(), r[0][3].as<std::string>(),
        r[0][4].as<std::string>(), r[0][5].as<bool>(),
        r[0][6].is_null() ? "" : r[0][6].as<std::string>(),
        r[0][7].as<std::string>(),
        r[0][8].is_null() ? "" : r[0][8].as<std::string>(),
        r[0][9].is_null() ? "" : r[0][9].as<std::string>(),
        r[0][10].is_null() ? "" : r[0][10].as<std::string>(),
        r[0][11].is_null() ? "" : r[0][11].as<std::string>(),
        r[0][12].as<bool>()
    };
}

void Database::store_password(const std::string& user_id, const std::string& password_hash) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    // Remove any existing password for this user (one password per user)
    txn.exec_params("DELETE FROM password_credentials WHERE user_id = $1", user_id);
    txn.exec_params(
        "INSERT INTO password_credentials (user_id, password_hash) VALUES ($1, $2)",
        user_id, password_hash);
    txn.commit();
}

std::optional<std::string> Database::get_password_hash(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT password_hash FROM password_credentials WHERE user_id = $1", user_id);
    txn.commit();
    if (r.empty()) return std::nullopt;
    return r[0][0].as<std::string>();
}

std::string Database::get_password_created_at(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT created_at::text FROM password_credentials WHERE user_id = $1", user_id);
    txn.commit();
    if (r.empty()) return "";
    return r[0][0].as<std::string>();
}

void Database::add_password_history(const std::string& user_id, const std::string& password_hash) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "INSERT INTO password_history (user_id, password_hash) VALUES ($1, $2)",
        user_id, password_hash);
    txn.commit();
}

std::vector<std::string> Database::get_password_history(const std::string& user_id, int count) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT password_hash FROM password_history WHERE user_id = $1 "
        "ORDER BY created_at DESC LIMIT $2",
        user_id, count);
    txn.commit();
    std::vector<std::string> hashes;
    for (const auto& row : r) {
        hashes.push_back(row[0].as<std::string>());
    }
    return hashes;
}

bool Database::has_password(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT COUNT(*) FROM password_credentials WHERE user_id = $1", user_id);
    txn.commit();
    return r[0][0].as<int>() > 0;
}

void Database::delete_password(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params("DELETE FROM password_credentials WHERE user_id = $1", user_id);
    txn.exec_params("DELETE FROM password_history WHERE user_id = $1", user_id);
    txn.commit();
}

bool Database::is_password_expired(const std::string& user_id, int max_age_days) {
    if (max_age_days <= 0) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT COUNT(*) FROM password_credentials "
        "WHERE user_id = $1 AND created_at < NOW() - INTERVAL '1 day' * $2",
        user_id, max_age_days);
    txn.commit();
    return r[0][0].as<int>() > 0;
}

// --- TOTP credentials ---

void Database::store_totp_secret(const std::string& user_id, const std::string& secret) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params("DELETE FROM totp_credentials WHERE user_id = $1", user_id);
    txn.exec_params(
        "INSERT INTO totp_credentials (user_id, secret, verified) VALUES ($1, $2, FALSE)",
        user_id, secret);
    txn.commit();
}

std::optional<std::string> Database::get_totp_secret(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT secret FROM totp_credentials WHERE user_id = $1 AND verified = TRUE", user_id);
    txn.commit();
    if (r.empty()) return std::nullopt;
    return r[0][0].as<std::string>();
}

std::optional<std::string> Database::get_unverified_totp_secret(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT secret FROM totp_credentials WHERE user_id = $1", user_id);
    txn.commit();
    if (r.empty()) return std::nullopt;
    return r[0][0].as<std::string>();
}

void Database::verify_totp(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params(
        "UPDATE totp_credentials SET verified = TRUE WHERE user_id = $1", user_id);
    txn.commit();
}

void Database::delete_totp(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params("DELETE FROM totp_credentials WHERE user_id = $1", user_id);
    txn.commit();
}

bool Database::has_totp(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT COUNT(*) FROM totp_credentials WHERE user_id = $1 AND verified = TRUE", user_id);
    txn.commit();
    return r[0][0].as<int>() > 0;
}

// --- MFA pending tokens ---

std::string Database::create_mfa_pending_token(const std::string& user_id,
                                                const std::string& auth_method, int expiry_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    // Clean up any existing pending tokens for this user
    txn.exec_params("DELETE FROM mfa_pending_tokens WHERE user_id = $1", user_id);
    auto r = txn.exec_params(
        "INSERT INTO mfa_pending_tokens (user_id, auth_method, expires_at) "
        "VALUES ($1, $2, NOW() + INTERVAL '1 second' * $3) RETURNING id::text",
        user_id, auth_method, expiry_seconds);
    txn.commit();
    return r[0][0].as<std::string>();
}

std::optional<std::pair<std::string, std::string>> Database::validate_mfa_pending_token(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    auto r = txn.exec_params(
        "SELECT user_id::text, auth_method FROM mfa_pending_tokens "
        "WHERE id = $1 AND expires_at > NOW()", token);
    txn.commit();
    if (r.empty()) return std::nullopt;
    return std::make_pair(r[0][0].as<std::string>(), r[0][1].as<std::string>());
}

void Database::delete_mfa_pending_token(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec_params("DELETE FROM mfa_pending_tokens WHERE id = $1", token);
    txn.commit();
}

void Database::cleanup_expired_mfa_tokens() {
    std::lock_guard<std::mutex> lock(mutex_);
    pqxx::work txn(get_conn());
    txn.exec("DELETE FROM mfa_pending_tokens WHERE expires_at < NOW()");
    txn.commit();
}
