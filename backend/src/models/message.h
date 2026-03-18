#pragma once
#include <cstdint>
#include <string>

struct Message {
  std::string id;
  std::string channel_id;
  std::string user_id;
  std::string username;
  std::string content;
  std::string created_at;
  std::string edited_at;
  bool is_deleted = false;
  std::string file_id;
  std::string file_name;
  int64_t file_size = 0;
  std::string file_type;
  std::string reply_to_message_id;
  std::string reply_to_username;
  std::string reply_to_content;
  bool reply_to_is_deleted = false;
  bool is_ai_assisted = false;
};
