#pragma once
#include <string>

struct AiConversation {
  std::string id;
  std::string user_id;
  std::string title;
  std::string created_at;
  std::string updated_at;
};

struct AiMessage {
  std::string id;
  std::string conversation_id;
  std::string role;  // "user", "assistant", "system", "tool"
  std::string content;
  std::string tool_calls;    // JSON string of tool calls (for assistant messages)
  std::string tool_call_id;  // for tool result messages
  std::string tool_name;     // for tool result messages
  std::string created_at;
};
