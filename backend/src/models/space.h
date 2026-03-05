#pragma once
#include <string>
#include <vector>

struct Space {
    std::string id;
    std::string name;
    std::string description;
    std::string icon;
    bool is_public = true;
    std::string default_role = "write";
    std::string created_by;
    std::string created_at;
};

struct SpaceMember {
    std::string user_id;
    std::string username;
    std::string display_name;
    std::string role;
    bool is_online = false;
    std::string last_seen;
};
