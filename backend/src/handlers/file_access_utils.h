#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace file_access_utils {

int64_t parse_max_file_size(const std::optional<std::string>& setting, int64_t fallback);
int64_t parse_max_storage_size(const std::optional<std::string>& setting);
int64_t parse_space_storage_limit(const std::optional<std::string>& setting);
bool exceeds_file_size_limit(int64_t max_size, int64_t file_size);
bool exceeds_storage_limit(int64_t limit, int64_t used, int64_t incoming_size);
bool is_valid_hex_id(const std::string& value);
std::string file_too_large_message(int64_t max_size);
std::string inline_disposition(const std::string& file_name);
std::string attachment_disposition(const std::string& file_name);
std::string versioned_attachment_disposition(int version_number, const std::string& file_name);

}  // namespace file_access_utils
