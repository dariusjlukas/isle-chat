#include "cors_utils.h"
#include <sstream>

namespace cors {

namespace {
// CORS allowlist. Populated from the ALLOWED_ORIGINS env var (comma-separated).
// NOTE: The wildcard "*" is no longer accepted. If this list is empty the server
// will refuse to emit Access-Control-Allow-Origin and CORS requests from browsers
// will fail. See the startup log for a warning when the env var is unset.
std::vector<std::string> g_allowed_origins;

std::vector<std::string> parse_allowed_origins(const char* env_value) {
  std::vector<std::string> out;
  if (!env_value || !*env_value) return out;
  std::stringstream ss(env_value);
  std::string item;
  while (std::getline(ss, item, ',')) {
    // trim whitespace
    auto l = item.find_first_not_of(" \t\r\n");
    auto r = item.find_last_not_of(" \t\r\n");
    if (l == std::string::npos) continue;
    out.push_back(item.substr(l, r - l + 1));
  }
  return out;
}
}  // namespace

void init_from_env(const char* env_value) {
  g_allowed_origins = parse_allowed_origins(env_value);
}

const std::vector<std::string>& allowed_origins() {
  return g_allowed_origins;
}

}  // namespace cors
