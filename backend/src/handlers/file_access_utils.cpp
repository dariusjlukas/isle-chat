#include "handlers/file_access_utils.h"

#include <algorithm>
#include <cctype>
#include "handlers/format_utils.h"
#include "handlers/handler_utils.h"

namespace file_access_utils {

static std::string sanitize_for_header(const std::string& filename) {
    std::string safe = filename;
    // Remove characters that could break Content-Disposition headers
    std::replace(safe.begin(), safe.end(), '"', '\'');
    safe.erase(std::remove_if(safe.begin(), safe.end(),
        [](char c) { return c == '\r' || c == '\n'; }), safe.end());
    return safe;
}

int64_t parse_max_file_size(const std::optional<std::string>& setting, int64_t fallback) {
    return parse_i64_setting_or(setting, fallback);
}

int64_t parse_max_storage_size(const std::optional<std::string>& setting) {
    return parse_i64_setting_or(setting, 0);
}

int64_t parse_space_storage_limit(const std::optional<std::string>& setting) {
    return parse_i64_setting_or(setting, 0);
}

bool exceeds_file_size_limit(int64_t max_size, int64_t file_size) {
    return max_size > 0 && file_size > max_size;
}

bool exceeds_storage_limit(int64_t limit, int64_t used, int64_t incoming_size) {
    return limit > 0 && used + incoming_size > limit;
}

bool is_valid_hex_id(const std::string& value) {
    if (value.empty()) return false;
    for (char c : value) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

std::string file_too_large_message(int64_t max_size) {
    return "File too large (max " + format_utils::format_size(max_size) + ")";
}

std::string inline_disposition(const std::string& file_name) {
    return "inline; filename=\"" + sanitize_for_header(file_name) + "\"";
}

std::string attachment_disposition(const std::string& file_name) {
    return "attachment; filename=\"" + sanitize_for_header(file_name) + "\"";
}

std::string versioned_attachment_disposition(int version_number, const std::string& file_name) {
    return "attachment; filename=\"v" + std::to_string(version_number) + "_" + sanitize_for_header(file_name) + "\"";
}

}  // namespace file_access_utils
