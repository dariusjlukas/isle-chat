#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct SpaceFile {
    std::string id;
    std::string space_id;
    std::string parent_id;
    std::string name;
    bool is_folder = false;
    std::string disk_file_id;
    int64_t file_size = 0;
    std::string mime_type;
    std::string created_by;
    std::string created_by_username;
    std::string created_at;
    std::string updated_at;
    bool is_deleted = false;
};

struct SpaceFileVersion {
    std::string id;
    std::string file_id;
    int version_number = 0;
    std::string disk_file_id;
    int64_t file_size = 0;
    std::string mime_type;
    std::string uploaded_by;
    std::string uploaded_by_username;
    std::string created_at;
};

struct SpaceFilePermission {
    std::string id;
    std::string file_id;
    std::string user_id;
    std::string username;
    std::string display_name;
    std::string permission;  // "owner", "edit", "view"
    std::string granted_by;
    std::string granted_by_username;
    std::string created_at;
};
