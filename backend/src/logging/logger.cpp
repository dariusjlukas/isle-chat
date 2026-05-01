#include "logging/logger.h"

#include <openssl/rand.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <mutex>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <unordered_map>

namespace logging {

namespace {

// Pattern emits one JSON object per line. The message body (%v) is itself a
// JSON-encoded string built by log_with_ctx, which is double-encoded inside
// the "msg" field. Future work can flatten this once we adopt spdlog's
// structured-fields API.
constexpr const char* kJsonPattern =
  R"({"ts":"%Y-%m-%dT%H:%M:%S.%eZ","lvl":"%l","logger":"%n","msg":%v})";

std::mutex g_mutex;
std::vector<spdlog::sink_ptr> g_sinks;
std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> g_loggers;
spdlog::level::level_enum g_level = spdlog::level::info;
bool g_initialized = false;

spdlog::level::level_enum parse_level(const char* s) {
  if (!s || !*s) return spdlog::level::info;
  std::string v(s);
  for (auto& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (v == "trace") return spdlog::level::trace;
  if (v == "debug") return spdlog::level::debug;
  if (v == "info") return spdlog::level::info;
  if (v == "warn" || v == "warning") return spdlog::level::warn;
  if (v == "err" || v == "error") return spdlog::level::err;
  if (v == "critical") return spdlog::level::critical;
  if (v == "off") return spdlog::level::off;
  return spdlog::level::info;
}

std::shared_ptr<spdlog::logger> create_logger_locked(const std::string& name) {
  auto logger = std::make_shared<spdlog::logger>(name, g_sinks.begin(), g_sinks.end());
  logger->set_level(g_level);
  logger->set_pattern(kJsonPattern);
  logger->flush_on(spdlog::level::warn);
  g_loggers[name] = logger;
  return logger;
}

}  // namespace

void init() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_initialized) return;

  g_level = parse_level(std::getenv("LOG_LEVEL"));

  // Default sink: stderr (so logs survive being piped to a journal).
  // Color is fine — terminals render it; log aggregators typically strip ANSI.
  auto stderr_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
  g_sinks.clear();
  g_sinks.push_back(stderr_sink);

  // Pre-create the "default" logger so LOG_INFO(...) without a name works.
  g_loggers.clear();
  create_logger_locked("default");

  g_initialized = true;
}

std::shared_ptr<spdlog::logger> get(const std::string& name) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_initialized) {
    // Lazy fallback init — useful for tests that call get() before init().
    g_level = parse_level(std::getenv("LOG_LEVEL"));
    if (g_sinks.empty()) {
      g_sinks.push_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());
    }
    g_initialized = true;
  }
  auto it = g_loggers.find(name);
  if (it != g_loggers.end()) return it->second;
  return create_logger_locked(name);
}

void set_sinks_for_testing(const std::vector<spdlog::sink_ptr>& sinks) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_sinks = sinks;
  g_loggers.clear();
  g_initialized = true;
  // Recreate "default" so callers using LOG_INFO without a name still work.
  create_logger_locked("default");
}

std::string make_request_id() {
  unsigned char buf[8];  // 8 bytes -> 16 hex chars
  if (RAND_bytes(buf, sizeof(buf)) != 1) {
    // Fallback: mt19937 seeded from random_device. Not crypto-grade, but
    // good enough for an id when OpenSSL's RNG is somehow unavailable.
    std::random_device rd;
    std::mt19937_64 gen((static_cast<uint64_t>(rd()) << 32) | rd());
    uint64_t v = gen();
    std::memcpy(buf, &v, sizeof(buf));
  }
  std::ostringstream ss;
  for (unsigned char b : buf) {
    ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
  }
  return ss.str();
}

void log_with_ctx(
  spdlog::logger& l, spdlog::level::level_enum lvl, const RequestCtx* ctx, std::string_view msg) {
  if (!l.should_log(lvl)) return;
  nlohmann::json j;
  j["msg"] = std::string(msg);
  if (ctx) {
    if (!ctx->request_id.empty()) j["request_id"] = ctx->request_id;
    if (!ctx->user_id.empty()) j["user_id"] = ctx->user_id;
    if (!ctx->method.empty()) j["method"] = ctx->method;
    if (!ctx->route.empty()) j["route"] = ctx->route;
  }
  // The pattern includes "msg":%v, so we hand the JSON object's serialized
  // form straight into %v. spdlog will not escape it again.
  l.log(lvl, j.dump());
}

}  // namespace logging
