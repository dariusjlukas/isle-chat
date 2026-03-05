#pragma once
#include <pqxx/pqxx>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>
#include "models/user.h"
#include "models/channel.h"
#include "models/message.h"

class Database {
public:
    explicit Database(const std::string& connection_string);

    void run_migrations();

    // Users
    std::optional<User> find_user_by_public_key(const std::string& public_key);
    std::optional<User> find_user_by_id(const std::string& id);
    User create_user(const std::string& username, const std::string& display_name,
                     const std::string& public_key, const std::string& role = "user");
    std::vector<User> list_users();
    void set_user_online(const std::string& user_id, bool online);
    int count_users();
    User update_user_profile(const std::string& user_id, const std::string& display_name,
                              const std::string& bio, const std::string& status);
    void delete_user(const std::string& user_id);

    // Sessions
    std::string create_session(const std::string& user_id, int expiry_hours);
    std::optional<std::string> validate_session(const std::string& token);
    void delete_session(const std::string& token);

    // Challenges
    void store_challenge(const std::string& public_key, const std::string& challenge);
    std::optional<std::string> get_challenge(const std::string& public_key);
    void delete_challenge(const std::string& public_key);

    // Channels
    Channel create_channel(const std::string& name, const std::string& description,
                           bool is_direct, const std::string& created_by,
                           const std::vector<std::string>& member_ids,
                           bool is_public = true, const std::string& default_role = "write");
    std::vector<Channel> list_user_channels(const std::string& user_id);
    std::optional<Channel> find_dm_channel(const std::string& user1_id, const std::string& user2_id);
    std::optional<Channel> find_channel_by_id(const std::string& id);
    bool is_channel_member(const std::string& channel_id, const std::string& user_id);
    void add_channel_member(const std::string& channel_id, const std::string& user_id,
                            const std::string& role = "write");
    std::vector<std::string> get_channel_member_ids(const std::string& channel_id);
    std::string get_member_role(const std::string& channel_id, const std::string& user_id);
    std::string get_effective_role(const std::string& channel_id, const std::string& user_id);
    void remove_channel_member(const std::string& channel_id, const std::string& user_id);
    void update_member_role(const std::string& channel_id, const std::string& user_id,
                            const std::string& role);
    Channel update_channel(const std::string& channel_id, const std::string& name,
                           const std::string& description, bool is_public,
                           const std::string& default_role);
    std::vector<Channel> list_public_channels(const std::string& user_id,
                                               const std::string& search = "");
    std::vector<Channel> list_all_channels();
    std::vector<ChannelMember> get_channel_members_with_roles(const std::string& channel_id);
    std::optional<Channel> find_general_channel();

    // Messages
    Message create_message(const std::string& channel_id, const std::string& user_id,
                           const std::string& content);
    Message create_file_message(const std::string& channel_id, const std::string& user_id,
                                const std::string& content, const std::string& file_id,
                                const std::string& file_name, int64_t file_size,
                                const std::string& file_type);
    std::vector<Message> get_messages(const std::string& channel_id, int limit = 50,
                                       const std::string& before = "");
    Message edit_message(const std::string& message_id, const std::string& user_id,
                          const std::string& new_content);
    Message delete_message(const std::string& message_id, const std::string& user_id);
    struct FileInfo { std::string file_name, file_type; };
    std::optional<FileInfo> get_file_info(const std::string& file_id);

    // Server settings
    std::optional<std::string> get_setting(const std::string& key);
    void set_setting(const std::string& key, const std::string& value);
    int64_t get_total_file_size();

    // Invites
    std::string create_invite(const std::string& created_by, int expiry_hours = 24);
    bool validate_invite(const std::string& token);
    void use_invite(const std::string& token, const std::string& user_id);
    struct InviteInfo {
        std::string id, token, created_by_username;
        bool used;
        std::string expires_at, created_at;
    };
    std::vector<InviteInfo> list_invites();

    // Join requests
    std::string create_join_request(const std::string& username, const std::string& display_name,
                                     const std::string& public_key);
    struct JoinRequest {
        std::string id, username, display_name, public_key, status, created_at;
    };
    std::vector<JoinRequest> list_pending_requests();
    std::optional<JoinRequest> get_join_request(const std::string& id);
    void update_join_request(const std::string& id, const std::string& status,
                              const std::string& reviewed_by);

    // Device / multi-key management
    struct UserKey {
        std::string id, user_id, public_key, device_name, created_at;
    };
    std::string create_device_token(const std::string& user_id, int expiry_minutes = 15);
    std::optional<std::string> validate_device_token(const std::string& token);
    void mark_device_token_used(const std::string& token);
    void add_user_key(const std::string& user_id, const std::string& public_key,
                       const std::string& device_name);
    std::vector<UserKey> list_user_keys(const std::string& user_id);
    void remove_user_key(const std::string& key_id, const std::string& user_id);

private:
    pqxx::connection& get_conn();
    std::string conn_string_;
    std::unique_ptr<pqxx::connection> conn_;
    std::mutex mutex_;
};
