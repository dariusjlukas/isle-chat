-- Chat Application Database Schema
--
-- HISTORICAL ARCHIVE — DO NOT USE.
-- This file is no longer applied by anything. It was a hand-maintained
-- snapshot that drifted out of sync with the embedded run_migrations() DDL
-- in backend/src/db/database.cpp.
--
-- The canonical schema source is now sqitch/deploy/0001-initial-schema.sql.
-- See sqitch/README.md for how to apply migrations.
--
-- Kept here for git-blame archaeology only. Will be removed in a future PR
-- once nobody references it.

CREATE EXTENSION IF NOT EXISTS "pgcrypto";

-- Users
CREATE TABLE users (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    username VARCHAR(50) UNIQUE NOT NULL,
    display_name VARCHAR(100) NOT NULL,
    public_key TEXT NOT NULL,
    role VARCHAR(20) DEFAULT 'user',
    is_online BOOLEAN DEFAULT FALSE,
    last_seen TIMESTAMPTZ,
    created_at TIMESTAMPTZ DEFAULT NOW()
);

-- Channels (also used for DMs with is_direct=true)
CREATE TABLE channels (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name VARCHAR(100),
    description TEXT,
    is_direct BOOLEAN DEFAULT FALSE,
    created_by UUID REFERENCES users(id),
    created_at TIMESTAMPTZ DEFAULT NOW()
);

-- Channel membership
CREATE TABLE channel_members (
    channel_id UUID REFERENCES channels(id) ON DELETE CASCADE,
    user_id UUID REFERENCES users(id) ON DELETE CASCADE,
    joined_at TIMESTAMPTZ DEFAULT NOW(),
    PRIMARY KEY (channel_id, user_id)
);

-- Messages
CREATE TABLE messages (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    channel_id UUID REFERENCES channels(id) ON DELETE CASCADE,
    user_id UUID REFERENCES users(id),
    content TEXT NOT NULL,
    created_at TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_messages_channel_time ON messages(channel_id, created_at);

-- Invite tokens (admin-generated)
CREATE TABLE invite_tokens (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    token VARCHAR(64) UNIQUE NOT NULL,
    created_by UUID REFERENCES users(id),
    used_by UUID REFERENCES users(id),
    expires_at TIMESTAMPTZ NOT NULL,
    created_at TIMESTAMPTZ DEFAULT NOW()
);

-- Join requests (user-initiated)
CREATE TABLE join_requests (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    username VARCHAR(50) NOT NULL,
    display_name VARCHAR(100) NOT NULL,
    public_key TEXT NOT NULL,
    status VARCHAR(20) DEFAULT 'pending',
    reviewed_by UUID REFERENCES users(id),
    created_at TIMESTAMPTZ DEFAULT NOW()
);

-- Sessions
CREATE TABLE sessions (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID REFERENCES users(id) ON DELETE CASCADE,
    token VARCHAR(128) UNIQUE NOT NULL,
    expires_at TIMESTAMPTZ NOT NULL,
    created_at TIMESTAMPTZ DEFAULT NOW()
);

