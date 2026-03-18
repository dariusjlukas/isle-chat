#pragma once
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using json = nlohmann::json;

struct LlmConfig {
  std::string api_url;  // e.g. "http://localhost:8080/v1"
  std::string model;    // e.g. "gpt-oss:120b"
  std::string api_key;  // optional
  int max_tokens = 4096;
  std::string system_prompt;
};

struct LlmMessage {
  std::string role;  // user, assistant, system, tool
  std::string content;
  json tool_calls;           // optional, for assistant messages
  std::string tool_call_id;  // optional, for tool results
  std::string name;          // optional, for tool results
};

struct LlmStreamCallback {
  std::function<void(const std::string& delta)> on_content;
  std::function<void(const json& tool_call)> on_tool_call;
  std::function<void()> on_done;
  std::function<void(const std::string& error)> on_error;
};

class LlmClient {
public:
  explicit LlmClient(const LlmConfig& config);

  // Blocking streaming call — run on a background thread
  void chat_completion(
    const std::vector<LlmMessage>& messages, const json& tools, const LlmStreamCallback& cb);

  // Blocking non-streaming call — returns the full response content
  std::string simple_completion(const std::vector<LlmMessage>& messages, int max_tokens = 50);

private:
  LlmConfig config_;
  json build_request_body(const std::vector<LlmMessage>& messages, const json& tools) const;
};
