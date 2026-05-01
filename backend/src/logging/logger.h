#pragma once

#include <spdlog/spdlog.h>
#include <memory>
#include <string>
#include <string_view>

namespace logging {

// Per-request context. Pass to the log_* helpers (or LOG_* macros) to add
// request_id/user_id/method/route to JSON output.
struct RequestCtx {
  std::string request_id;  // 16-hex-char id
  std::string user_id;     // empty if unauthenticated
  std::string method;      // "GET", "POST", etc. (HTTP only)
  std::string route;       // "/api/foo/:id" or "ws:message:<type>"
};

// Initialize the global JSON logger. Call once at boot from main().
// Reads env LOG_LEVEL (trace, debug, info, warn, err, critical, off; default info).
// Idempotent — safe to call multiple times (e.g. from tests).
void init();

// Returns a pointer to a named child logger ("ws", "pki", "db", etc.). Lazily created.
// All loggers share the same sink/format as the default logger (set up by init()).
std::shared_ptr<spdlog::logger> get(const std::string& name);

// For tests: replace the underlying sinks with the given list, applied to all
// loggers (existing and future). Resets internal logger registry. Pass an empty
// vector to clear all sinks (silences output).
void set_sinks_for_testing(const std::vector<spdlog::sink_ptr>& sinks);

// Generate a 16-hex-char request id using OpenSSL RAND_bytes (fall back to mt19937 only if RAND
// fails).
std::string make_request_id();

// Core logging primitive. Builds a JSON payload {msg, request_id, user_id, method, route}
// (only including non-empty ctx fields) and dispatches at the given level.
// `ctx` may be nullptr for boot-time / no-context logging.
void log_with_ctx(
  spdlog::logger& l, spdlog::level::level_enum lvl, const RequestCtx* ctx, std::string_view msg);

}  // namespace logging

// Convenience macros. The "_N" variants pick a named logger; the unsuffixed
// variants log to the "default" logger.
//
// Usage:
//   LOG_INFO_N("config", nullptr, "Listening on port 9001");
//   LOG_ERROR_N("ws", &ctx, "websocket upgrade failed");
#define LOG_TRACE_N(name, ctx, msg) \
  ::logging::log_with_ctx(*::logging::get(name), spdlog::level::trace, ctx, msg)
#define LOG_DEBUG_N(name, ctx, msg) \
  ::logging::log_with_ctx(*::logging::get(name), spdlog::level::debug, ctx, msg)
#define LOG_INFO_N(name, ctx, msg) \
  ::logging::log_with_ctx(*::logging::get(name), spdlog::level::info, ctx, msg)
#define LOG_WARN_N(name, ctx, msg) \
  ::logging::log_with_ctx(*::logging::get(name), spdlog::level::warn, ctx, msg)
#define LOG_ERROR_N(name, ctx, msg) \
  ::logging::log_with_ctx(*::logging::get(name), spdlog::level::err, ctx, msg)

#define LOG_TRACE(ctx, msg) LOG_TRACE_N("default", ctx, msg)
#define LOG_DEBUG(ctx, msg) LOG_DEBUG_N("default", ctx, msg)
#define LOG_INFO(ctx, msg) LOG_INFO_N("default", ctx, msg)
#define LOG_WARN(ctx, msg) LOG_WARN_N("default", ctx, msg)
#define LOG_ERROR(ctx, msg) LOG_ERROR_N("default", ctx, msg)
