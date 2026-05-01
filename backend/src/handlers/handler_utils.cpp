#include "handlers/handler_utils.h"

#include <cctype>
#include <charconv>

namespace handler_utils {

std::optional<int> safe_parse_int(std::string_view s) {
  // Trim leading ASCII whitespace to tolerate values like " 123" in query strings.
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.remove_prefix(1);
  }
  if (s.empty()) return std::nullopt;
  int value = 0;
  const char* begin = s.data();
  const char* end = begin + s.size();
  auto [ptr, ec] = std::from_chars(begin, end, value);
  if (ec != std::errc()) return std::nullopt;
  if (ptr != end) return std::nullopt;  // trailing garbage
  return value;
}

std::optional<int64_t> safe_parse_int64(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.remove_prefix(1);
  }
  if (s.empty()) return std::nullopt;
  int64_t value = 0;
  const char* begin = s.data();
  const char* end = begin + s.size();
  auto [ptr, ec] = std::from_chars(begin, end, value);
  if (ec != std::errc()) return std::nullopt;
  if (ptr != end) return std::nullopt;
  return value;
}

}  // namespace handler_utils

int parse_int_setting_or(const std::optional<std::string>& setting, int fallback) {
  if (setting) {
    try {
      return std::stoi(*setting);
    } catch (...) {}
  }
  return fallback;
}

int64_t parse_i64_setting_or(const std::optional<std::string>& setting, int64_t fallback) {
  if (setting) {
    try {
      return std::stoll(*setting);
    } catch (...) {}
  }
  return fallback;
}

bool parse_bool_setting_or(const std::optional<std::string>& setting, bool fallback) {
  if (!setting) return fallback;
  if (*setting == "true") return true;
  if (*setting == "false") return false;
  return fallback;
}

json parse_auth_methods_setting(const std::optional<std::string>& setting) {
  json auth_methods = json::array({"passkey", "pki"});
  if (setting) {
    try {
      auto parsed = json::parse(*setting);
      if (parsed.is_array()) auth_methods = parsed;
    } catch (...) {}
  }
  return auth_methods;
}

bool auth_methods_include(const json& methods, const std::string& method) {
  for (const auto& item : methods) {
    if (item.is_string() && item.get<std::string>() == method) return true;
  }
  return false;
}

int server_role_rank(const std::string& role) {
  if (role == "owner") return 2;
  if (role == "admin") return 1;
  return 0;
}

int space_role_rank(const std::string& role) {
  if (role == "owner") return 2;
  if (role == "admin") return 1;
  return 0;
}

int channel_role_rank(const std::string& role) {
  if (role == "admin") return 2;
  if (role == "write") return 1;
  return 0;
}
