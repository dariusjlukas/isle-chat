#include "handlers/ai_handler.h"

#include <Loop.h>

#include <thread>

#include "ai/llm_client.h"
#include "handlers/handler_utils.h"

template <bool SSL>
void AiHandler<SSL>::register_routes(uWS::TemplatedApp<SSL>& app) {
  // List conversations
  app.get("/api/ai/conversations", [this](auto* res, auto* req) {
    auto user_id = get_user_id(res, req);
    if (user_id.empty()) return;
    if (!check_llm_enabled(res)) return;
    if (!check_agent_enabled(res, user_id)) return;

    int limit = 50, offset = 0;
    auto q_limit = req->getQuery("limit");
    auto q_offset = req->getQuery("offset");
    if (q_limit.length() > 0) limit = std::stoi(std::string(q_limit));
    if (q_offset.length() > 0) offset = std::stoi(std::string(q_offset));

    auto conversations = db.list_ai_conversations(user_id, limit, offset);
    json arr = json::array();
    for (const auto& c : conversations) {
      arr.push_back(
        {{"id", c.id},
         {"title", c.title},
         {"created_at", c.created_at},
         {"updated_at", c.updated_at}});
    }
    res->writeHeader("Content-Type", "application/json")->end(arr.dump());
  });

  // Create conversation
  app.post("/api/ai/conversations", [this](auto* res, auto* req) {
    auto user_id_copy = get_user_id(res, req);
    std::string body;
    res->onData([this, res, user_id = std::move(user_id_copy), body = std::move(body)](
                  std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      if (user_id.empty()) return;
      if (!check_llm_enabled(res)) return;
      if (!check_agent_enabled(res, user_id)) return;

      std::string title = "New conversation";
      if (!body.empty()) {
        try {
          auto j = json::parse(body);
          if (j.contains("title")) title = j["title"].get<std::string>();
        } catch (...) {}
      }

      auto conv = db.create_ai_conversation(user_id, title);
      json resp = {
        {"id", conv.id},
        {"title", conv.title},
        {"created_at", conv.created_at},
        {"updated_at", conv.updated_at}};
      res->writeHeader("Content-Type", "application/json")->end(resp.dump());
    });
    res->onAborted([]() {});
  });

  // Get conversation with messages
  app.get("/api/ai/conversations/:id", [this](auto* res, auto* req) {
    auto user_id = get_user_id(res, req);
    if (user_id.empty()) return;
    if (!check_llm_enabled(res)) return;

    std::string conv_id(req->getParameter("id"));
    auto conv = db.find_ai_conversation(conv_id);
    if (!conv || conv->user_id != user_id) {
      res->writeStatus("404")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Conversation not found"})");
      return;
    }

    auto messages = db.get_ai_messages(conv_id);
    json msg_arr = json::array();
    for (const auto& m : messages) {
      json mj = {
        {"id", m.id}, {"role", m.role}, {"content", m.content}, {"created_at", m.created_at}};
      if (!m.tool_calls.empty()) mj["tool_calls"] = json::parse(m.tool_calls);
      if (!m.tool_call_id.empty()) mj["tool_call_id"] = m.tool_call_id;
      if (!m.tool_name.empty()) mj["tool_name"] = m.tool_name;
      msg_arr.push_back(mj);
    }

    json resp = {
      {"id", conv->id},
      {"title", conv->title},
      {"created_at", conv->created_at},
      {"updated_at", conv->updated_at},
      {"messages", msg_arr}};
    res->writeHeader("Content-Type", "application/json")->end(resp.dump());
  });

  // Delete conversation
  app.del("/api/ai/conversations/:id", [this](auto* res, auto* req) {
    auto user_id = get_user_id(res, req);
    if (user_id.empty()) return;

    std::string conv_id(req->getParameter("id"));
    auto conv = db.find_ai_conversation(conv_id);
    if (!conv || conv->user_id != user_id) {
      res->writeStatus("404")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Conversation not found"})");
      return;
    }

    db.delete_ai_conversation(conv_id);
    res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
  });

  // Update conversation title
  app.put("/api/ai/conversations/:id", [this](auto* res, auto* req) {
    auto user_id_copy = get_user_id(res, req);
    std::string conv_id(req->getParameter("id"));
    std::string body;
    res->onData([this,
                 res,
                 user_id = std::move(user_id_copy),
                 conv_id = std::move(conv_id),
                 body = std::move(body)](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      if (user_id.empty()) return;

      auto conv = db.find_ai_conversation(conv_id);
      if (!conv || conv->user_id != user_id) {
        res->writeStatus("404")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Conversation not found"})");
        return;
      }

      try {
        auto j = json::parse(body);
        std::string title = j.at("title").get<std::string>();
        db.update_ai_conversation_title(conv_id, title);
        res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
      } catch (const std::exception& e) {
        res->writeStatus("400")
          ->writeHeader("Content-Type", "application/json")
          ->end(json({{"error", e.what()}}).dump());
      }
    });
    res->onAborted([]() {});
  });

  // Send message and trigger LLM response
  app.post("/api/ai/conversations/:id/messages", [this](auto* res, auto* req) {
    auto user_id_copy = get_user_id(res, req);
    std::string conv_id(req->getParameter("id"));
    std::string body;
    res->onData([this,
                 res,
                 user_id = std::move(user_id_copy),
                 conv_id = std::move(conv_id),
                 body = std::move(body)](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      if (user_id.empty()) return;
      if (!check_llm_enabled(res)) return;
      if (!check_agent_enabled(res, user_id)) return;

      auto conv = db.find_ai_conversation(conv_id);
      if (!conv || conv->user_id != user_id) {
        res->writeStatus("404")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Conversation not found"})");
        return;
      }

      try {
        auto j = json::parse(body);
        std::string content = j.at("content").get<std::string>();
        std::string current_space_id =
          j.contains("current_space_id") ? j["current_space_id"].get<std::string>() : "";
        std::string current_channel_id =
          j.contains("current_channel_id") ? j["current_channel_id"].get<std::string>() : "";

        // Save user message
        db.create_ai_message(conv_id, "user", content);
        db.touch_ai_conversation(conv_id);

        // Build LLM config from server settings
        LlmConfig llm_config;
        llm_config.api_url = db.get_setting("llm_api_url").value_or("");
        llm_config.model = db.get_setting("llm_model").value_or("gpt-oss:120b");
        llm_config.api_key = db.get_setting("llm_api_key").value_or("");
        llm_config.max_tokens = parse_int_setting_or(db.get_setting("llm_max_tokens"), 4096);
        llm_config.system_prompt =
          build_system_prompt(user_id, current_space_id, current_channel_id);

        if (llm_config.api_url.empty()) {
          res->writeStatus("503")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"LLM API URL not configured"})");
          return;
        }

        // Get enabled tool categories for this user
        auto enabled_categories = get_enabled_tool_categories(user_id);
        auto tools_schema = tools.get_tools_schema(enabled_categories);

        // Create cancellation token
        auto cancelled = std::make_shared<std::atomic<bool>>(false);
        {
          std::lock_guard<std::mutex> lock(active_mutex_);
          active_generations_[conv_id] = cancelled;
        }

        // Respond immediately
        res->writeStatus("202")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"status":"streaming"})");

        // Load conversation history for LLM
        auto history = db.get_ai_messages(conv_id);
        std::vector<LlmMessage> messages;
        for (const auto& m : history) {
          LlmMessage lm;
          lm.role = m.role;
          lm.content = m.content;
          if (!m.tool_calls.empty()) {
            lm.tool_calls = json::parse(m.tool_calls);
          }
          lm.tool_call_id = m.tool_call_id;
          lm.name = m.tool_name;
          messages.push_back(std::move(lm));
        }

        // Capture what we need for the background thread
        auto loop = uWS::Loop::get();
        auto db_ptr = &db;
        auto ws_ptr = &ws;
        auto tools_ptr = &tools;
        auto active_mutex_ptr = &active_mutex_;
        auto active_gens_ptr = &active_generations_;

        // Launch LLM call on background thread
        std::thread([loop,
                     db_ptr,
                     ws_ptr,
                     tools_ptr,
                     active_mutex_ptr,
                     active_gens_ptr,
                     conv_id,
                     user_id,
                     llm_config,
                     messages = std::move(messages),
                     tools_schema,
                     enabled_categories,
                     cancelled]() mutable {
          std::cout << "[AI] Starting LLM request for conversation " << conv_id
                    << " (url=" << llm_config.api_url << ", model=" << llm_config.model << ")"
                    << std::endl;

          LlmClient client(llm_config);
          int tool_rounds = 0;
          const int max_tool_rounds = 10;

          auto run_completion = [&](auto& self) -> void {
            if (cancelled->load()) return;

            std::string accumulated_content;
            std::vector<json> accumulated_tool_calls;
            bool has_tool_calls = false;

            LlmStreamCallback cb;
            cb.on_content = [&](const std::string& delta) {
              if (cancelled->load()) return;
              accumulated_content += delta;
              json msg = {
                {"type", "ai_stream_delta"}, {"conversation_id", conv_id}, {"content", delta}};
              auto msg_str = msg.dump();
              loop->defer([ws_ptr, user_id, msg_str]() { ws_ptr->send_to_user(user_id, msg_str); });
            };

            cb.on_tool_call = [&](const json& tool_call) {
              has_tool_calls = true;
              accumulated_tool_calls.push_back(tool_call);
            };

            cb.on_error = [&](const std::string& error) {
              std::cerr << "[AI] LLM error for conversation " << conv_id << ": " << error
                        << std::endl;
              json msg = {
                {"type", "ai_stream_error"}, {"conversation_id", conv_id}, {"error", error}};
              auto msg_str = msg.dump();
              loop->defer([ws_ptr, user_id, msg_str]() { ws_ptr->send_to_user(user_id, msg_str); });
            };

            cb.on_done = [&]() {
              // Will be handled after chat_completion returns
            };

            client.chat_completion(messages, tools_schema, cb);

            if (cancelled->load()) return;

            if (has_tool_calls && tool_rounds < max_tool_rounds) {
              tool_rounds++;

              // Save assistant message with tool calls
              json tc_json = json::array();
              for (const auto& tc : accumulated_tool_calls) {
                tc_json.push_back(tc);
              }
              db_ptr->create_ai_message(conv_id, "assistant", accumulated_content, tc_json.dump());

              // Add assistant message to history
              LlmMessage assistant_msg;
              assistant_msg.role = "assistant";
              assistant_msg.content = accumulated_content;
              assistant_msg.tool_calls = tc_json;
              messages.push_back(std::move(assistant_msg));

              // Execute each tool call
              for (const auto& tc : accumulated_tool_calls) {
                if (cancelled->load()) return;

                std::string tc_id = tc.value("id", "");
                std::string fn_name = tc["function"]["name"].get<std::string>();
                json fn_args;
                try {
                  fn_args = json::parse(tc["function"]["arguments"].get<std::string>());
                } catch (...) {
                  fn_args = json::object();
                }

                // Execute tool
                auto result =
                  tools_ptr->execute_tool(fn_name, *db_ptr, user_id, fn_args, enabled_categories);

                // Send tool use notification to user
                json tool_use_msg = {
                  {"type", "ai_tool_use"},
                  {"conversation_id", conv_id},
                  {"tool_name", fn_name},
                  {"arguments", fn_args},
                  {"result", result.success ? result.data : json({{"error", result.error}})},
                  {"status", result.success ? "success" : "error"}};
                auto tool_use_str = tool_use_msg.dump();
                loop->defer([ws_ptr, user_id, tool_use_str]() {
                  ws_ptr->send_to_user(user_id, tool_use_str);
                });

                // Save tool result message
                std::string tool_content =
                  result.success ? result.data.dump() : ("Error: " + result.error);
                db_ptr->create_ai_message(conv_id, "tool", tool_content, "", tc_id, fn_name);

                // Add tool result to history
                LlmMessage tool_msg;
                tool_msg.role = "tool";
                tool_msg.content = tool_content;
                tool_msg.tool_call_id = tc_id;
                tool_msg.name = fn_name;
                messages.push_back(std::move(tool_msg));
              }

              // Continue the loop — call the LLM again with tool results
              self(self);
            } else {
              // Final response — save and notify
              if (!accumulated_content.empty()) {
                auto saved = db_ptr->create_ai_message(conv_id, "assistant", accumulated_content);

                json end_msg = {
                  {"type", "ai_stream_end"},
                  {"conversation_id", conv_id},
                  {"message_id", saved.id}};
                auto end_str = end_msg.dump();
                loop->defer(
                  [ws_ptr, user_id, end_str]() { ws_ptr->send_to_user(user_id, end_str); });

                // Auto-title: if this is the first exchange, generate a title
                auto conv_check = db_ptr->find_ai_conversation(conv_id);
                std::cout << "[AI] Checking auto-title for " << conv_id
                          << " (current title: \"" << (conv_check ? conv_check->title : "N/A")
                          << "\")" << std::endl;
                if (conv_check && conv_check->title == "New conversation") {
                  // Find the first user message
                  std::string first_user_msg;
                  for (const auto& m : messages) {
                    if (m.role == "user") {
                      first_user_msg = m.content;
                      break;
                    }
                  }
                  if (!first_user_msg.empty()) {
                    std::vector<LlmMessage> title_msgs = {
                      {"system",
                       "Generate a very short title (max 6 words) for a conversation that starts "
                       "with the following message. Reply with ONLY the title, no quotes or "
                       "punctuation.",
                       {}, "", ""},
                      {"user", first_user_msg, {}, "", ""},
                    };
                    auto title = client.simple_completion(title_msgs, 20);
                    std::cout << "[AI] Auto-title result for " << conv_id << ": \""
                              << title << "\"" << std::endl;
                    // Clean up the title
                    if (!title.empty()) {
                      // Remove surrounding quotes if present
                      if (title.front() == '"' && title.back() == '"' && title.size() > 2) {
                        title = title.substr(1, title.size() - 2);
                      }
                      // Truncate to 200 chars
                      if (title.size() > 200) title = title.substr(0, 200);
                      if (!title.empty()) {
                        db_ptr->update_ai_conversation_title(conv_id, title);
                        json title_msg = {
                          {"type", "ai_conversation_titled"},
                          {"conversation_id", conv_id},
                          {"title", title}};
                        auto title_str = title_msg.dump();
                        loop->defer([ws_ptr, user_id, title_str]() {
                          ws_ptr->send_to_user(user_id, title_str);
                        });
                      }
                    }
                  }
                }
              }
            }
          };

          run_completion(run_completion);

          std::cout << "[AI] LLM request completed for conversation " << conv_id << std::endl;

          // Clean up active generation tracker
          {
            std::lock_guard<std::mutex> lock(*active_mutex_ptr);
            active_gens_ptr->erase(conv_id);
          }
        }).detach();

      } catch (const std::exception& e) {
        res->writeStatus("400")
          ->writeHeader("Content-Type", "application/json")
          ->end(json({{"error", e.what()}}).dump());
      }
    });
    res->onAborted([]() {});
  });

  // Stop generation
  app.post("/api/ai/conversations/:id/stop", [this](auto* res, auto* req) {
    auto user_id = get_user_id(res, req);
    if (user_id.empty()) return;

    std::string conv_id(req->getParameter("id"));

    std::lock_guard<std::mutex> lock(active_mutex_);
    auto it = active_generations_.find(conv_id);
    if (it != active_generations_.end()) {
      it->second->store(true);
    }

    res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
  });
}

template <bool SSL>
std::string AiHandler<SSL>::get_user_id(uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req) {
  return validate_session_or_401(res, req, db);
}

template <bool SSL>
bool AiHandler<SSL>::check_llm_enabled(uWS::HttpResponse<SSL>* res) {
  auto enabled = db.get_setting("llm_enabled");
  if (!enabled || *enabled != "true") {
    res->writeStatus("403")
      ->writeHeader("Content-Type", "application/json")
      ->end(R"({"error":"AI assistant is not enabled on this server"})");
    return false;
  }
  return true;
}

template <bool SSL>
bool AiHandler<SSL>::check_agent_enabled(uWS::HttpResponse<SSL>* res, const std::string& user_id) {
  auto setting = db.get_user_setting(user_id, "agent_enabled");
  if (setting && *setting == "false") {
    res->writeStatus("403")
      ->writeHeader("Content-Type", "application/json")
      ->end(R"({"error":"AI assistant is disabled in your settings"})");
    return false;
  }
  return true;
}

template <bool SSL>
std::set<std::string> AiHandler<SSL>::get_enabled_tool_categories(const std::string& user_id) {
  // All sub-categories enabled by default
  std::set<std::string> categories = {
    "search",         "messaging_read", "messaging_write", "tasks_read",
    "tasks_write",    "calendar_read",  "calendar_write",  "wiki_read",
    "wiki_write",     "files_read",
  };
  auto settings = db.get_all_user_settings(user_id);

  auto check = [&](const std::string& key, const std::vector<std::string>& cats) {
    auto it = settings.find(key);
    if (it != settings.end() && it->second == "false") {
      for (const auto& c : cats) {
        categories.erase(c);
      }
    }
  };

  // Read settings disable the _read (and implicitly _write) sub-categories
  check("agent_tools_messaging_read", {"messaging_read"});
  check("agent_tools_messaging_write", {"messaging_write"});
  check("agent_tools_search", {"search"});
  check("agent_tools_tasks_read", {"tasks_read"});
  check("agent_tools_tasks_write", {"tasks_write"});
  check("agent_tools_calendar_read", {"calendar_read"});
  check("agent_tools_calendar_write", {"calendar_write"});
  check("agent_tools_wiki_read", {"wiki_read"});
  check("agent_tools_wiki_write", {"wiki_write"});
  check("agent_tools_files_read", {"files_read"});

  return categories;
}

template <bool SSL>
std::string AiHandler<SSL>::build_system_prompt(
  const std::string& user_id,
  const std::string& current_space_id,
  const std::string& current_channel_id) {
  // Check for custom system prompt
  auto custom_prompt = db.get_setting("llm_system_prompt");

  std::string prompt;
  if (custom_prompt && !custom_prompt->empty()) {
    prompt = *custom_prompt;
  } else {
    prompt =
      "You are an AI assistant integrated into Enclave Station, a self-hosted team collaboration "
      "platform. You help users by answering questions and taking actions on their behalf using "
      "the available tools. Be helpful, concise, and accurate. When using tools, explain what "
      "you're doing. Only use tools when the user asks you to take an action.\n\n"
      "## Platform Overview\n\n"
      "Enclave Station is organized around **spaces** — shared workspaces that contain channels "
      "and tools. Each space can have:\n"
      "- **Channels** — real-time chat rooms for messaging within the space\n"
      "- **Files** — a shared file storage area with folders\n"
      "- **Calendar** — shared calendar events\n"
      "- **Tasks** — kanban-style task boards with columns, cards, labels, and assignees\n"
      "- **Wiki** — collaborative documentation pages organized in a hierarchy\n\n"
      "**Personal space**: Every user has their own private personal space (marked with "
      "is_personal=true in the API). It works just like a regular space but is private to the "
      "user. It has its own files, calendar, tasks, and wiki. When users say \"my space\" or "
      "\"my personal space\", they mean this.\n\n"
      "**Direct messages**: Users can have direct message (DM) conversations outside of spaces. "
      "To send a DM, use `find_or_create_dm` with the recipient's username to get the channel ID, "
      "then use `send_message` with that channel ID. You can also look up users with `search_users` "
      "if you need to find someone by name.\n\n"
      "## How to Find Things\n\n"
      "Most tools require IDs (space_id, channel_id, etc.). To find IDs:\n"
      "1. Use `list_spaces` to find spaces by name and get their IDs\n"
      "2. Use `list_space_channels` to find channels within a space\n"
      "3. Use `list_channels` to see all channels across all spaces\n"
      "4. Use `list_task_boards` to find task boards in a space\n"
      "5. Use `list_task_columns` to find columns on a task board\n"
      "When the user mentions a space or channel by name, look it up first to get the ID.\n\n"
      "## Search Tips\n\n"
      "The `search_messages` tool uses full-text search. An empty query string acts as a "
      "wildcard and returns recent messages across all accessible channels. This is useful "
      "for browsing recent activity. You can also use this to find recent messages in a "
      "channel when the user asks about recent conversations or activity.";
  }

  // Add user context
  auto user = db.find_user_by_id(user_id);
  if (user) {
    prompt +=
      "\n\nThe user you are chatting with is " + user->display_name + " (@" + user->username + ").";
  }

  // Add current context
  if (!current_channel_id.empty()) {
    auto channel = db.find_channel_by_id(current_channel_id);
    if (channel) {
      if (!current_space_id.empty()) {
        auto space = db.find_space_by_id(current_space_id);
        if (space) {
          prompt += " They are currently viewing the #" + channel->name + " channel in the \"" +
                    space->name + "\" space.";
        }
      } else {
        prompt += " They are currently viewing the #" + channel->name + " channel.";
      }
    }
  } else if (!current_space_id.empty()) {
    auto space = db.find_space_by_id(current_space_id);
    if (space) {
      prompt += " They are currently viewing the \"" + space->name + "\" space.";
    }
  }

  return prompt;
}

template struct AiHandler<false>;
template struct AiHandler<true>;
