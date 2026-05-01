#pragma once
#include <App.h>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace cors {

// Initialize the global allowlist from a comma-separated env-var value.
// Called once from main() at startup. Idempotent.
void init_from_env(const char* env_value);

// Returns the currently-configured allowlist (read-only view).
const std::vector<std::string>& allowed_origins();

// Apply Allow-Origin / Vary / Allow-Credentials headers to a response if
// the request's Origin matches the allowlist. Returns true if a header was
// emitted.
//
// The template definition lives in this header so that both SSL=true and
// SSL=false instantiations are visible at every call site (otherwise we'd
// hit linker errors).
template <bool SSL>
inline bool apply(uWS::HttpResponse<SSL>* res, std::string_view origin_header) {
  const auto& origins = allowed_origins();
  if (origin_header.empty() || origins.empty()) return false;
  std::string origin(origin_header);
  for (const auto& o : origins) {
    if (o == origin) {
      res->writeHeader("Access-Control-Allow-Origin", o);
      res->writeHeader("Vary", "Origin");
      res->writeHeader("Access-Control-Allow-Credentials", "true");
      return true;
    }
  }
  return false;
}

// Chainable variant of apply() suitable for use inside ->writeHeader(...)
// chains. Returns res so the caller can continue chaining. If the origin is
// not in the allowlist, this is a no-op (no Access-Control-Allow-Origin is
// emitted, in contrast to the previous behavior which unconditionally emitted
// "*" — which is incompatible with Allow-Credentials and bypasses the
// allowlist).
template <bool SSL>
inline uWS::HttpResponse<SSL>* apply_chain(
  uWS::HttpResponse<SSL>* res, std::string_view origin_header) {
  apply(res, origin_header);
  return res;
}

}  // namespace cors
