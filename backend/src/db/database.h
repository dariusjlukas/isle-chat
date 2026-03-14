#pragma once
#include <pqxx/pqxx>
#include <memory>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <vector>
#include "models/user.h"
#include "models/channel.h"
#include "models/message.h"
#include "models/space.h"
#include "models/space_file.h"
#include "models/calendar_event.h"

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
    void set_all_users_offline();
    int count_users();
    User update_user_profile(const std::string& user_id, const std::string& display_name,
                              const std::string& bio, const std::string& status,
                              const std::string& profile_color = "");
    void set_user_avatar(const std::string& user_id, const std::string& avatar_file_id);
    void clear_user_avatar(const std::string& user_id);
    void delete_user(const std::string& user_id);
    void update_user_role(const std::string& user_id, const std::string& role);
    void ban_user(const std::string& user_id, const std::string& banned_by);
    void unban_user(const std::string& user_id);

    // Sessions
    std::string create_session(const std::string& user_id, int expiry_hours);
    std::optional<std::string> validate_session(const std::string& token);
    void delete_session(const std::string& token);

    // Challenges
    void store_challenge(const std::string& public_key, const std::string& challenge);
    std::optional<std::string> get_challenge(const std::string& public_key);
    void delete_challenge(const std::string& public_key);

    // Spaces
    Space create_space(const std::string& name, const std::string& description,
                       bool is_public, const std::string& created_by,
                       const std::string& default_role = "write");
    std::vector<Space> list_user_spaces(const std::string& user_id);
    std::optional<Space> find_space_by_id(const std::string& id);
    Space update_space(const std::string& space_id, const std::string& name,
                       const std::string& description, bool is_public,
                       const std::string& default_role,
                       const std::string& profile_color = "");
    std::vector<Space> list_public_spaces(const std::string& user_id,
                                           const std::string& search = "");
    std::vector<Space> list_all_spaces();
    void set_space_avatar(const std::string& space_id, const std::string& avatar_file_id);
    void clear_space_avatar(const std::string& space_id);

    // Space membership
    bool is_space_member(const std::string& space_id, const std::string& user_id);
    void add_space_member(const std::string& space_id, const std::string& user_id,
                          const std::string& role = "write");
    void remove_space_member(const std::string& space_id, const std::string& user_id);
    void update_space_member_role(const std::string& space_id, const std::string& user_id,
                                   const std::string& role);
    std::string get_space_member_role(const std::string& space_id, const std::string& user_id);
    std::vector<SpaceMember> get_space_members_with_roles(const std::string& space_id);

    // Conversations (group messages)
    Channel create_conversation(const std::string& created_by,
                                 const std::vector<std::string>& member_ids,
                                 const std::string& name = "");
    std::vector<Channel> list_user_conversations(const std::string& user_id);
    void add_conversation_member(const std::string& channel_id, const std::string& user_id);
    void rename_conversation(const std::string& channel_id, const std::string& name);

    // Channels
    Channel create_channel(const std::string& name, const std::string& description,
                           bool is_direct, const std::string& created_by,
                           const std::vector<std::string>& member_ids,
                           bool is_public = true, const std::string& default_role = "write",
                           const std::string& space_id = "", bool default_join = false);
    std::vector<Channel> list_user_channels(const std::string& user_id);
    std::vector<Channel> list_space_channels(const std::string& space_id);
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
                           const std::string& default_role, bool default_join = false);
    std::vector<Channel> get_default_join_channels(const std::string& space_id);
    std::vector<Channel> list_public_channels(const std::string& user_id,
                                               const std::string& search = "");
    std::vector<Channel> list_browsable_space_channels(const std::string& space_id,
                                                        const std::string& user_id,
                                                        const std::string& search = "");
    std::vector<Channel> list_all_channels();
    std::vector<ChannelMember> get_channel_members_with_roles(const std::string& channel_id);
    // Messages
    Message create_message(const std::string& channel_id, const std::string& user_id,
                           const std::string& content,
                           const std::string& reply_to_message_id = "");
    Message create_file_message(const std::string& channel_id, const std::string& user_id,
                                const std::string& content, const std::string& file_id,
                                const std::string& file_name, int64_t file_size,
                                const std::string& file_type);
    std::vector<Message> get_messages(const std::string& channel_id, int limit = 50,
                                       const std::string& before = "");
    Message edit_message(const std::string& message_id, const std::string& user_id,
                          const std::string& new_content);
    Message delete_message(const std::string& message_id, const std::string& user_id);
    Message admin_delete_message(const std::string& message_id);
    struct MessageOwnership { std::string channel_id, user_id; };
    std::optional<MessageOwnership> get_message_ownership(const std::string& message_id);
    struct FileInfo { std::string file_name, file_type; };
    std::optional<FileInfo> get_file_info(const std::string& file_id);

    // Reactions
    struct Reaction { std::string emoji, user_id, username; };
    void add_reaction(const std::string& message_id, const std::string& user_id,
                      const std::string& emoji);
    void remove_reaction(const std::string& message_id, const std::string& user_id,
                         const std::string& emoji);
    std::vector<Reaction> get_reactions(const std::string& message_id);
    std::map<std::string, std::vector<Reaction>> get_reactions_for_messages(
        const std::vector<std::string>& message_ids);
    std::string get_message_channel_id(const std::string& message_id);

    // Read state / unread counts
    void update_read_state(const std::string& channel_id, const std::string& user_id,
                           const std::string& message_id, const std::string& timestamp);
    struct UnreadCount { std::string channel_id; int count; };
    std::vector<UnreadCount> get_unread_counts(const std::string& user_id);
    std::vector<UnreadCount> get_mention_unread_counts(const std::string& user_id);
    struct ReadReceipt {
        std::string user_id, username, last_read_message_id, last_read_at;
    };
    std::vector<ReadReceipt> get_channel_read_receipts(const std::string& channel_id);

    // Mentions
    struct ChannelMemberUsername { std::string user_id, username; };
    std::vector<ChannelMemberUsername> get_channel_member_usernames(const std::string& channel_id);
    void store_mentions(const std::string& message_id, const std::string& channel_id,
                        const std::string& content,
                        const std::vector<ChannelMemberUsername>& members,
                        const std::string& sender_user_id = "");

    // Server settings
    std::optional<std::string> get_setting(const std::string& key);
    void set_setting(const std::string& key, const std::string& value);
    int64_t get_total_file_size();

    // Invites
    std::string create_invite(const std::string& created_by, int expiry_hours = 24, int max_uses = 1);
    bool validate_invite(const std::string& token);
    void use_invite(const std::string& token, const std::string& user_id);
    struct InviteUse {
        std::string username, used_at;
    };
    struct InviteInfo {
        std::string id, token, created_by_username;
        bool used;
        std::string expires_at, created_at;
        int max_uses, use_count;
        std::vector<InviteUse> uses;
    };
    std::vector<InviteInfo> list_invites();
    bool revoke_invite(const std::string& id);

    // Join requests
    std::string create_join_request(const std::string& username, const std::string& display_name,
                                     const std::string& public_key,
                                     const std::string& auth_method = "",
                                     const std::string& credential_data = "");
    struct JoinRequest {
        std::string id, username, display_name, public_key, status,
                    auth_method, credential_data, session_token, created_at;
    };
    std::vector<JoinRequest> list_pending_requests();
    std::optional<JoinRequest> get_join_request(const std::string& id);
    void update_join_request(const std::string& id, const std::string& status,
                              const std::string& reviewed_by);
    void set_join_request_session(const std::string& id, const std::string& session_token);

    // Space invites
    struct SpaceInvite {
        std::string id, space_id, space_name;
        std::string invited_user_id, invited_by, invited_by_username;
        std::string role, status, created_at;
    };
    std::string create_space_invite(const std::string& space_id, const std::string& invited_user_id,
                                     const std::string& invited_by, const std::string& role = "write");
    std::vector<SpaceInvite> list_pending_space_invites(const std::string& user_id);
    std::optional<SpaceInvite> get_space_invite(const std::string& invite_id);
    void update_space_invite_status(const std::string& invite_id, const std::string& status);
    bool has_pending_space_invite(const std::string& space_id, const std::string& user_id);

    // Device / multi-key management (legacy)
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

    // WebAuthn credentials
    struct WebAuthnCredential {
        std::string id, user_id, credential_id;
        std::vector<unsigned char> public_key;
        int sign_count;
        std::string device_name, transports, created_at;
    };
    void store_webauthn_credential(const std::string& user_id, const std::string& credential_id,
                                    const std::vector<unsigned char>& public_key, int sign_count,
                                    const std::string& device_name, const std::string& transports);
    std::optional<WebAuthnCredential> find_webauthn_credential(const std::string& credential_id);
    std::vector<WebAuthnCredential> list_webauthn_credentials(const std::string& user_id);
    void update_webauthn_sign_count(const std::string& credential_id, int new_count);
    void remove_webauthn_credential(const std::string& credential_id, const std::string& user_id);
    std::optional<User> find_user_by_credential_id(const std::string& credential_id);

    // WebAuthn challenges
    void store_webauthn_challenge(const std::string& challenge, const std::string& extra_data_json);
    struct WebAuthnChallenge { std::string challenge, extra_data; };
    std::optional<WebAuthnChallenge> get_webauthn_challenge(const std::string& challenge);
    void delete_webauthn_challenge(const std::string& challenge);

    // Check for approved join request by username
    bool has_approved_join_request(const std::string& username);

    // PKI credentials
    struct PkiCredential {
        std::string id, user_id, public_key, device_name, created_at;
    };
    void store_pki_credential(const std::string& user_id, const std::string& public_key_spki,
                               const std::string& device_name = "Browser Key");
    std::vector<PkiCredential> list_pki_credentials(const std::string& user_id);
    std::optional<PkiCredential> find_pki_credential_by_key(const std::string& public_key_spki);
    void remove_pki_credential(const std::string& id, const std::string& user_id);
    std::optional<User> find_user_by_pki_key(const std::string& public_key_spki);

    // Recovery keys
    void store_recovery_keys(const std::string& user_id, const std::vector<std::string>& key_hashes);
    std::optional<std::string> verify_and_consume_recovery_key(const std::string& key_hash);
    int count_remaining_recovery_keys(const std::string& user_id);
    void delete_recovery_keys(const std::string& user_id);

    // Count total auth credentials for a user (passkeys + PKI keys)
    int count_user_credentials(const std::string& user_id);

    // Password credentials
    std::optional<User> find_user_by_username(const std::string& username);
    void store_password(const std::string& user_id, const std::string& password_hash);
    std::optional<std::string> get_password_hash(const std::string& user_id);
    std::string get_password_created_at(const std::string& user_id);
    bool has_password(const std::string& user_id);
    void delete_password(const std::string& user_id);
    bool is_password_expired(const std::string& user_id, int max_age_days);

    // Password history
    void add_password_history(const std::string& user_id, const std::string& password_hash);
    std::vector<std::string> get_password_history(const std::string& user_id, int count);

    // TOTP credentials
    void store_totp_secret(const std::string& user_id, const std::string& secret);
    std::optional<std::string> get_totp_secret(const std::string& user_id);
    std::optional<std::string> get_unverified_totp_secret(const std::string& user_id);
    void verify_totp(const std::string& user_id);
    void delete_totp(const std::string& user_id);
    bool has_totp(const std::string& user_id);

    // MFA pending tokens
    std::string create_mfa_pending_token(const std::string& user_id,
                                          const std::string& auth_method, int expiry_seconds = 300);
    std::optional<std::pair<std::string, std::string>> validate_mfa_pending_token(const std::string& token);
    void delete_mfa_pending_token(const std::string& token);
    void cleanup_expired_mfa_tokens();

    // Search
    struct MessageSearchResult {
        std::string id, channel_id, channel_name, space_name;
        std::string user_id, username, content, created_at;
        bool is_direct;
    };
    struct FileSearchResult {
        std::string message_id, channel_id, channel_name;
        std::string user_id, username;
        std::string file_id, file_name, file_type, created_at;
        int64_t file_size;
    };
    struct SpaceSearchResult {
        std::string id, name, description;
        bool is_public;
        std::string avatar_file_id, profile_color;
    };
    struct ChannelSearchResult {
        std::string id, name, description, space_name, space_id;
        bool is_public;
    };
    struct CompositeFilter {
        std::string type;  // messages, users, files, channels, spaces
        std::string value;
    };
    std::vector<User> search_users(const std::string& query, int limit, int offset);
    std::vector<MessageSearchResult> search_messages(const std::string& tsquery_expr,
        const std::string& user_id, bool is_admin, int limit, int offset);
    std::vector<MessageSearchResult> browse_messages(const std::string& user_id,
        bool is_admin, int limit, int offset);
    std::vector<FileSearchResult> search_files(const std::string& query,
        const std::string& user_id, bool is_admin, int limit, int offset);
    std::vector<SpaceSearchResult> search_spaces(const std::string& query,
        const std::string& user_id, bool is_admin, int limit, int offset);
    std::vector<ChannelSearchResult> search_channels(const std::string& query,
        const std::string& user_id, bool is_admin, int limit, int offset);
    std::vector<MessageSearchResult> search_composite_messages(
        const std::vector<CompositeFilter>& filters, const std::string& mode,
        const std::string& user_id, bool is_admin, int limit, int offset);
    std::vector<FileSearchResult> search_composite_files(
        const std::vector<CompositeFilter>& filters, const std::string& mode,
        const std::string& user_id, bool is_admin, int limit, int offset);
    std::vector<User> search_composite_users(
        const std::vector<CompositeFilter>& filters, const std::string& mode,
        const std::string& user_id, bool is_admin, int limit, int offset);
    std::vector<ChannelSearchResult> search_composite_channels(
        const std::vector<CompositeFilter>& filters, const std::string& mode,
        const std::string& user_id, bool is_admin, int limit, int offset);
    std::vector<SpaceSearchResult> search_composite_spaces(
        const std::vector<CompositeFilter>& filters, const std::string& mode,
        const std::string& user_id, bool is_admin, int limit, int offset);
    std::vector<Message> get_messages_around(const std::string& channel_id,
        const std::string& message_id, int limit);

    // Notifications
    struct Notification {
        std::string id, user_id, type, source_user_id, source_username;
        std::string channel_id, channel_name, message_id, space_id;
        std::string content, created_at;
        bool is_read;
    };
    std::string create_notification(const std::string& user_id, const std::string& type,
        const std::string& source_user_id, const std::string& channel_id,
        const std::string& message_id, const std::string& content);
    std::vector<Notification> get_notifications(const std::string& user_id, int limit, int offset);
    int get_unread_notification_count(const std::string& user_id);
    void mark_notification_read(const std::string& notification_id, const std::string& user_id);
    void mark_all_notifications_read(const std::string& user_id);
    int mark_channel_notifications_read(const std::string& channel_id, const std::string& user_id);

    // Space tools
    void enable_space_tool(const std::string& space_id, const std::string& tool_name,
                           const std::string& user_id);
    void disable_space_tool(const std::string& space_id, const std::string& tool_name);
    bool is_space_tool_enabled(const std::string& space_id, const std::string& tool_name);
    std::vector<std::string> get_space_tools(const std::string& space_id);

    // Space files
    SpaceFile create_space_folder(const std::string& space_id, const std::string& parent_id,
                                   const std::string& name, const std::string& created_by);
    SpaceFile create_space_file(const std::string& space_id, const std::string& parent_id,
                                 const std::string& name, const std::string& disk_file_id,
                                 int64_t file_size, const std::string& mime_type,
                                 const std::string& created_by);
    std::vector<SpaceFile> list_space_files(const std::string& space_id, const std::string& parent_id);
    std::optional<SpaceFile> find_space_file(const std::string& file_id);
    void rename_space_file(const std::string& file_id, const std::string& new_name);
    void move_space_file(const std::string& file_id, const std::string& new_parent_id);
    void soft_delete_space_file(const std::string& file_id);
    int64_t get_space_storage_used(const std::string& space_id);
    std::vector<SpaceFile> get_space_file_path(const std::string& file_id);
    bool space_file_name_exists(const std::string& space_id, const std::string& parent_id,
                                 const std::string& name, const std::string& exclude_id = "");

    // Space file permissions
    void set_file_permission(const std::string& file_id, const std::string& user_id,
                              const std::string& permission, const std::string& granted_by);
    void remove_file_permission(const std::string& file_id, const std::string& user_id);
    std::vector<SpaceFilePermission> get_file_permissions(const std::string& file_id);
    std::string get_effective_file_permission(const std::string& file_id, const std::string& user_id);

    // Space file versions
    std::vector<SpaceFileVersion> list_file_versions(const std::string& file_id);
    SpaceFileVersion create_file_version(const std::string& file_id, const std::string& disk_file_id,
                                          int64_t file_size, const std::string& mime_type,
                                          const std::string& uploaded_by);
    std::optional<SpaceFileVersion> get_file_version(const std::string& version_id);

    // Storage admin
    struct SpaceStorageInfo {
        std::string space_id, space_name;
        int64_t storage_used, storage_limit;
        int file_count;
    };
    std::vector<SpaceStorageInfo> get_all_space_storage();
    void delete_oldest_file_versions(const std::string& space_id, int64_t bytes_to_free);

    // Archive management
    void archive_channel(const std::string& channel_id);
    void unarchive_channel(const std::string& channel_id);
    void archive_space(const std::string& space_id);
    void unarchive_space(const std::string& space_id);
    bool is_server_archived();
    void set_server_archived(bool archived);
    bool is_server_locked_down();
    void set_server_locked_down(bool locked_down);

    // Role counting
    int count_channel_members_with_role(const std::string& channel_id, const std::string& role);
    int count_space_members_with_role(const std::string& space_id, const std::string& role);
    int count_users_with_role(const std::string& role);
    int count_channel_members(const std::string& channel_id);

    // Recovery tokens (admin-generated account recovery)
    struct RecoveryTokenInfo {
        std::string id, token, created_by_username, for_username, for_user_id;
        bool used;
        std::string expires_at, created_at, used_at;
    };
    std::string create_recovery_token(const std::string& created_by, const std::string& for_user_id,
                                       int expiry_hours = 24);
    std::optional<std::string> get_recovery_token_user_id(const std::string& token);
    void use_recovery_token(const std::string& token);
    bool delete_recovery_token(const std::string& id);
    std::vector<RecoveryTokenInfo> list_recovery_tokens();

    // Calendar events
    CalendarEvent create_calendar_event(const std::string& space_id, const std::string& title,
                                         const std::string& description, const std::string& location,
                                         const std::string& color, const std::string& start_time,
                                         const std::string& end_time, bool all_day,
                                         const std::string& rrule, const std::string& created_by);
    CalendarEvent update_calendar_event(const std::string& event_id, const std::string& title,
                                         const std::string& description, const std::string& location,
                                         const std::string& color, const std::string& start_time,
                                         const std::string& end_time, bool all_day,
                                         const std::string& rrule);
    void delete_calendar_event(const std::string& event_id);
    std::optional<CalendarEvent> find_calendar_event(const std::string& event_id);
    std::vector<CalendarEvent> list_calendar_events(const std::string& space_id,
                                                      const std::string& range_start,
                                                      const std::string& range_end);

    // Calendar event exceptions
    CalendarEventException create_event_exception(const std::string& event_id,
                                                    const std::string& original_date,
                                                    bool is_deleted,
                                                    const std::string& title,
                                                    const std::string& description,
                                                    const std::string& location,
                                                    const std::string& color,
                                                    const std::string& start_time,
                                                    const std::string& end_time,
                                                    bool all_day);
    void delete_event_exception(const std::string& exception_id);
    std::vector<CalendarEventException> get_event_exceptions(const std::string& event_id);

    // Calendar RSVP
    void set_event_rsvp(const std::string& event_id, const std::string& user_id,
                         const std::string& occurrence_date, const std::string& status);
    std::vector<CalendarEventRsvp> get_event_rsvps(const std::string& event_id,
                                                     const std::string& occurrence_date);
    std::optional<std::string> get_user_rsvp(const std::string& event_id,
                                               const std::string& user_id,
                                               const std::string& occurrence_date);

    // Calendar permissions
    void set_calendar_permission(const std::string& space_id, const std::string& user_id,
                                  const std::string& permission, const std::string& granted_by);
    void remove_calendar_permission(const std::string& space_id, const std::string& user_id);
    std::vector<CalendarPermission> get_calendar_permissions(const std::string& space_id);
    std::string get_calendar_permission(const std::string& space_id, const std::string& user_id);

private:
    pqxx::connection& get_conn();
    std::string conn_string_;
    std::unique_ptr<pqxx::connection> conn_;
    std::mutex mutex_;
};
