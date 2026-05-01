#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <unordered_set>

#include <spdlog/sinks/ostream_sink.h>

#include "logging/logger.h"

namespace {

// Drives a string-backed sink so tests can capture log output. Each test
// installs the sink via logging::set_sinks_for_testing(), exercises the
// logger, then parses the captured output as JSON.
class CapturingLogger : public ::testing::Test {
protected:
  void SetUp() override {
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(stream_);
    logging::set_sinks_for_testing({sink});
  }
  void TearDown() override {
    // Reset back to a no-op stderr sink so other tests don't pollute streams.
    logging::set_sinks_for_testing({});
  }

  // Returns the most recent line of captured output (without the trailing newline).
  std::string last_line() {
    auto s = stream_.str();
    if (s.empty()) return s;
    // Trim trailing newline.
    if (s.back() == '\n') s.pop_back();
    auto last_nl = s.find_last_of('\n');
    if (last_nl == std::string::npos) return s;
    return s.substr(last_nl + 1);
  }

  std::ostringstream stream_;
};

}  // namespace

TEST_F(CapturingLogger, EmitsValidJsonWithRequiredFields) {
  LOG_INFO_N("test-module", nullptr, "hello world");

  auto line = last_line();
  ASSERT_FALSE(line.empty()) << "no log output captured";

  // Top-level object should parse as JSON.
  nlohmann::json j = nlohmann::json::parse(line);
  EXPECT_TRUE(j.contains("ts"));
  EXPECT_TRUE(j.contains("lvl"));
  EXPECT_TRUE(j.contains("logger"));
  EXPECT_TRUE(j.contains("msg"));
  EXPECT_EQ(j["lvl"].get<std::string>(), "info");
  EXPECT_EQ(j["logger"].get<std::string>(), "test-module");

  // The "msg" field is itself a JSON object containing the structured payload.
  auto msg = j["msg"];
  ASSERT_TRUE(msg.is_object());
  EXPECT_EQ(msg["msg"].get<std::string>(), "hello world");
  // No request context provided; those fields should be absent.
  EXPECT_FALSE(msg.contains("request_id"));
  EXPECT_FALSE(msg.contains("user_id"));
}

TEST_F(CapturingLogger, IncludesRequestContextFields) {
  logging::RequestCtx ctx;
  ctx.request_id = "abcdef0123456789";
  ctx.user_id = "user-42";
  ctx.method = "POST";
  ctx.route = "/api/foo/:id";

  LOG_ERROR_N("server", &ctx, "boom");

  auto line = last_line();
  ASSERT_FALSE(line.empty());

  auto j = nlohmann::json::parse(line);
  EXPECT_EQ(j["lvl"].get<std::string>(), "error");
  EXPECT_EQ(j["logger"].get<std::string>(), "server");

  auto msg = j["msg"];
  ASSERT_TRUE(msg.is_object());
  EXPECT_EQ(msg["msg"].get<std::string>(), "boom");
  EXPECT_EQ(msg["request_id"].get<std::string>(), "abcdef0123456789");
  EXPECT_EQ(msg["user_id"].get<std::string>(), "user-42");
  EXPECT_EQ(msg["method"].get<std::string>(), "POST");
  EXPECT_EQ(msg["route"].get<std::string>(), "/api/foo/:id");
}

TEST_F(CapturingLogger, OmitsEmptyContextFields) {
  logging::RequestCtx ctx;
  ctx.request_id = "ffffffffffffffff";
  // user_id, method, route deliberately empty

  LOG_INFO_N("auth", &ctx, "login");
  auto j = nlohmann::json::parse(last_line());
  auto msg = j["msg"];
  EXPECT_EQ(msg["request_id"].get<std::string>(), "ffffffffffffffff");
  EXPECT_FALSE(msg.contains("user_id"));
  EXPECT_FALSE(msg.contains("method"));
  EXPECT_FALSE(msg.contains("route"));
}

TEST(MakeRequestId, ReturnsSixteenHexChars) {
  std::regex hex16("^[0-9a-f]{16}$");
  for (int i = 0; i < 32; ++i) {
    auto id = logging::make_request_id();
    EXPECT_EQ(id.size(), 16u) << "id was '" << id << "'";
    EXPECT_TRUE(std::regex_match(id, hex16)) << "id was '" << id << "'";
  }
}

TEST(MakeRequestId, IsNonDeterministic) {
  // 256 ids should be unique with overwhelming probability (16 hex chars = 64 bits).
  std::unordered_set<std::string> seen;
  constexpr int kIterations = 256;
  for (int i = 0; i < kIterations; ++i) {
    seen.insert(logging::make_request_id());
  }
  EXPECT_EQ(seen.size(), static_cast<size_t>(kIterations))
    << "make_request_id() produced collisions across " << kIterations << " calls";
}
