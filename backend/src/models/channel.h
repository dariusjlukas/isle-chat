#pragma once
#include <string>
#include <vector>

struct Channel {
    std::string id;
    std::string name;
    std::string description;
    bool is_direct = false;
    bool is_public = true;
    std::string default_role = "write";
    std::string created_by;
    std::string created_at;
    std::string space_id;
    std::string conversation_name;
    bool is_archived = false;
    bool default_join = false;
    std::vector<std::string> member_ids;
};

struct ChannelMember {
    std::string user_id;
    std::string username;
    std::string display_name;
    std::string role;
    bool is_online = false;
    std::string last_seen;
};
