#include "ai/tool_registry.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

#include "db/database.h"
#include "handlers/search_handler.h"

// ---------------------------------------------------------------------------
// ToolRegistry implementation
// ---------------------------------------------------------------------------

void ToolRegistry::register_tool(ToolDefinition tool) {
  auto name = tool.name;
  tools_.emplace(std::move(name), std::move(tool));
}

json ToolRegistry::get_tools_schema(const std::set<std::string>& enabled_categories) const {
  json tools_array = json::array();

  for (const auto& [name, tool] : tools_) {
    if (enabled_categories.find(tool.category) == enabled_categories.end()) {
      continue;
    }

    json properties = json::object();
    json required_params = json::array();

    for (const auto& param : tool.parameters) {
      json prop;
      prop["type"] = param.type;
      prop["description"] = param.description;
      if (!param.enum_values.empty()) {
        prop["enum"] = param.enum_values;
      }
      properties[param.name] = prop;

      if (param.required) {
        required_params.push_back(param.name);
      }
    }

    json func;
    func["name"] = tool.name;
    func["description"] = tool.description;
    func["parameters"]["type"] = "object";
    func["parameters"]["properties"] = properties;
    func["parameters"]["required"] = required_params;

    json entry;
    entry["type"] = "function";
    entry["function"] = func;

    tools_array.push_back(entry);
  }

  return tools_array;
}

ToolResult ToolRegistry::execute_tool(
  const std::string& tool_name,
  Database& db,
  const std::string& user_id,
  const json& arguments,
  const std::set<std::string>& enabled_categories) const {
  auto it = tools_.find(tool_name);
  if (it == tools_.end()) {
    return ToolResult{false, {}, "Unknown tool: " + tool_name};
  }

  const auto& tool = it->second;
  if (enabled_categories.find(tool.category) == enabled_categories.end()) {
    return ToolResult{false, {}, "Tool category '" + tool.category + "' is not enabled"};
  }

  try {
    return tool.execute(db, user_id, arguments);
  } catch (const std::exception& e) {
    return ToolResult{false, {}, std::string("Tool execution error: ") + e.what()};
  }
}

bool ToolRegistry::has_tool(const std::string& name) const {
  return tools_.find(name) != tools_.end();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string generate_slug(const std::string& title) {
  std::string slug;
  slug.reserve(title.size());
  for (char c : title) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      slug += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else if (c == ' ' || c == '-') {
      if (!slug.empty() && slug.back() != '-') {
        slug += '-';
      }
    }
  }
  // Trim trailing hyphens
  while (!slug.empty() && slug.back() == '-') {
    slug.pop_back();
  }
  return slug;
}

// ---------------------------------------------------------------------------
// register_all_tools
// ---------------------------------------------------------------------------

void register_all_tools(ToolRegistry& registry) {
  // ===========================================================================
  // Category: search (discovery tools)
  // ===========================================================================

  registry.register_tool(ToolDefinition{
    "list_spaces",
    "search",
    "List all spaces the user is a member of. Use this to find space IDs by name.",
    {},
    [](Database& db, const std::string& user_id, const json& /*args*/) -> ToolResult {
      try {
        auto spaces = db.list_user_spaces(user_id);

        json result = json::array();
        for (const auto& s : spaces) {
          json obj;
          obj["id"] = s.id;
          obj["name"] = s.name;
          obj["description"] = s.description;
          obj["is_personal"] = s.is_personal;
          result.push_back(obj);
        }
        return ToolResult{true, result, ""};
      } catch (const std::exception& e) {
        return ToolResult{false, {}, std::string("list_spaces failed: ") + e.what()};
      }
    },
  });

  registry.register_tool(ToolDefinition{
    "list_space_channels",
    "search",
    "List all channels in a specific space. Use this to find channel IDs by name within a space.",
    {
      {"space_id", "string", "The ID of the space", true, {}},
    },
    [](Database& db, const std::string& user_id, const json& args) -> ToolResult {
      try {
        auto space_id = args.at("space_id").get<std::string>();

        if (!db.is_space_member(space_id, user_id)) {
          return ToolResult{false, {}, "You are not a member of this space"};
        }

        auto channels = db.list_space_channels(space_id);

        json result = json::array();
        for (const auto& ch : channels) {
          json obj;
          obj["id"] = ch.id;
          obj["name"] = ch.name;
          obj["description"] = ch.description;
          obj["is_public"] = ch.is_public;
          obj["is_direct"] = ch.is_direct;
          result.push_back(obj);
        }
        return ToolResult{true, result, ""};
      } catch (const std::exception& e) {
        return ToolResult{false, {}, std::string("list_space_channels failed: ") + e.what()};
      }
    },
  });

  registry.register_tool(ToolDefinition{
    "list_space_members",
    "search",
    "List all members of a space.",
    {
      {"space_id", "string", "The ID of the space", true, {}},
    },
    [](Database& db, const std::string& user_id, const json& args) -> ToolResult {
      try {
        auto space_id = args.at("space_id").get<std::string>();

        if (!db.is_space_member(space_id, user_id)) {
          return ToolResult{false, {}, "You are not a member of this space"};
        }

        auto members = db.get_space_members_with_roles(space_id);

        json result = json::array();
        for (const auto& m : members) {
          json obj;
          obj["user_id"] = m.user_id;
          obj["username"] = m.username;
          obj["display_name"] = m.display_name;
          obj["role"] = m.role;
          result.push_back(obj);
        }
        return ToolResult{true, result, ""};
      } catch (const std::exception& e) {
        return ToolResult{false, {}, std::string("list_space_members failed: ") + e.what()};
      }
    },
  });

  // ===========================================================================
  // Category: messaging
  // ===========================================================================

  registry.register_tool(ToolDefinition{
    "find_or_create_dm",
    "messaging_write",
    "Find or create a direct message channel with another user. Returns the DM channel ID. "
    "Use this when the user wants to send a direct message to someone.",
    {
      {"username", "string", "The username of the person to DM", false, {}},
      {"user_id", "string", "The user ID of the person to DM (alternative to username)", false, {}},
    },
    [](Database& db, const std::string& user_id, const json& args) -> ToolResult {
      try {
        std::string target_user_id;

        if (args.contains("user_id") && !args["user_id"].is_null() &&
            !args["user_id"].get<std::string>().empty()) {
          target_user_id = args["user_id"].get<std::string>();
        } else if (
          args.contains("username") && !args["username"].is_null() &&
          !args["username"].get<std::string>().empty()) {
          auto target = db.find_user_by_username(args["username"].get<std::string>());
          if (!target) {
            return ToolResult{false, {}, "User not found"};
          }
          target_user_id = target->id;
        } else {
          return ToolResult{false, {}, "Either username or user_id is required"};
        }

        if (target_user_id == user_id) {
          return ToolResult{false, {}, "Cannot create a DM with yourself"};
        }

        // Check if a DM channel already exists
        auto existing = db.find_dm_channel(user_id, target_user_id);
        if (existing) {
          json result;
          result["channel_id"] = existing->id;
          result["name"] = existing->name;
          result["already_existed"] = true;
          return ToolResult{true, result, ""};
        }

        // Create a new DM conversation
        auto channel = db.create_conversation(user_id, {target_user_id});

        json result;
        result["channel_id"] = channel.id;
        result["name"] = channel.name;
        result["already_existed"] = false;
        return ToolResult{true, result, ""};
      } catch (const std::exception& e) {
        return ToolResult{false, {}, std::string("find_or_create_dm failed: ") + e.what()};
      }
    },
  });

  registry.register_tool(
    ToolDefinition{
      "send_message",
      "messaging_write",
      "Send a message to a channel the user is a member of.",
      {
        {"channel_id", "string", "The ID of the channel to send the message to", true, {}},
        {"content", "string", "The message content", true, {}},
      },
      [](Database& db, const std::string& user_id, const json& args) -> ToolResult {
        try {
          auto channel_id = args.at("channel_id").get<std::string>();
          auto content = args.at("content").get<std::string>();

          if (!db.is_channel_member(channel_id, user_id)) {
            return ToolResult{false, {}, "You are not a member of this channel"};
          }

          auto msg = db.create_message(channel_id, user_id, content, "", true);

          json result;
          result["id"] = msg.id;
          result["content"] = msg.content;
          result["channel_id"] = msg.channel_id;
          result["created_at"] = msg.created_at;
          return ToolResult{true, result, ""};
        } catch (const std::exception& e) {
          return ToolResult{false, {}, std::string("send_message failed: ") + e.what()};
        }
      },
    });

  registry.register_tool(
    ToolDefinition{
      "list_channels",
      "messaging_read",
      "List all channels the user is a member of across all spaces.",
      {},
      [](Database& db, const std::string& user_id, const json& /*args*/) -> ToolResult {
        try {
          auto channels = db.list_user_channels(user_id);

          // Build a space name lookup
          auto spaces = db.list_user_spaces(user_id);
          std::map<std::string, std::string> space_names;
          for (const auto& s : spaces) {
            space_names[s.id] = s.name;
          }

          json result = json::array();
          for (const auto& ch : channels) {
            json obj;
            obj["id"] = ch.id;
            obj["name"] = ch.name;
            obj["space_id"] = ch.space_id;
            obj["is_direct"] = ch.is_direct;
            if (!ch.space_id.empty() && space_names.count(ch.space_id)) {
              obj["space_name"] = space_names[ch.space_id];
            }
            result.push_back(obj);
          }
          return ToolResult{true, result, ""};
        } catch (const std::exception& e) {
          return ToolResult{false, {}, std::string("list_channels failed: ") + e.what()};
        }
      },
    });

  registry.register_tool(
    ToolDefinition{
      "list_channel_members",
      "messaging_read",
      "List members of a channel the user belongs to.",
      {
        {"channel_id", "string", "The ID of the channel", true, {}},
      },
      [](Database& db, const std::string& user_id, const json& args) -> ToolResult {
        try {
          auto channel_id = args.at("channel_id").get<std::string>();

          if (!db.is_channel_member(channel_id, user_id)) {
            return ToolResult{false, {}, "You are not a member of this channel"};
          }

          auto members = db.get_channel_members_with_roles(channel_id);

          json result = json::array();
          for (const auto& m : members) {
            json obj;
            obj["user_id"] = m.user_id;
            obj["username"] = m.username;
            obj["display_name"] = m.display_name;
            obj["role"] = m.role;
            result.push_back(obj);
          }
          return ToolResult{true, result, ""};
        } catch (const std::exception& e) {
          return ToolResult{false, {}, std::string("list_channel_members failed: ") + e.what()};
        }
      },
    });

  // ===========================================================================
  // Category: search
  // ===========================================================================

  registry.register_tool(
    ToolDefinition{
      "search_messages",
      "search",
      "Search messages across channels the user has access to.",
      {
        {"query", "string", "The search query", true, {}},
        {"limit", "integer", "Maximum number of results (default 20)", false, {}},
      },
      [](Database& db, const std::string& user_id, const json& args) -> ToolResult {
        try {
          auto query = args.at("query").get<std::string>();
          int limit = 20;
          if (args.contains("limit") && !args["limit"].is_null()) {
            limit = args["limit"].get<int>();
          }

          std::vector<Database::MessageSearchResult> results;
          if (query.empty()) {
            results = db.browse_messages(user_id, false, limit, 0);
          } else {
            auto terms = SearchHandler<false>::split_terms(query);
            auto tsquery = SearchHandler<false>::build_tsquery(terms, "and");
            results = db.search_messages(tsquery, user_id, false, limit, 0);
          }

          json result = json::array();
          for (const auto& r : results) {
            json obj;
            obj["id"] = r.id;
            obj["channel_id"] = r.channel_id;
            obj["channel_name"] = r.channel_name;
            obj["username"] = r.username;
            obj["content"] = r.content;
            obj["created_at"] = r.created_at;
            result.push_back(obj);
          }
          return ToolResult{true, result, ""};
        } catch (const std::exception& e) {
          return ToolResult{false, {}, std::string("search_messages failed: ") + e.what()};
        }
      },
    });

  registry.register_tool(
    ToolDefinition{
      "search_users",
      "search",
      "Search for users by name or username.",
      {
        {"query", "string", "The search query", true, {}},
      },
      [](Database& db, const std::string& /*user_id*/, const json& args) -> ToolResult {
        try {
          auto query = args.at("query").get<std::string>();

          auto users = db.search_users(query, 10, 0);

          json result = json::array();
          for (const auto& u : users) {
            json obj;
            obj["id"] = u.id;
            obj["username"] = u.username;
            obj["display_name"] = u.display_name;
            obj["status"] = u.status;
            obj["is_online"] = u.is_online;
            result.push_back(obj);
          }
          return ToolResult{true, result, ""};
        } catch (const std::exception& e) {
          return ToolResult{false, {}, std::string("search_users failed: ") + e.what()};
        }
      },
    });

  // ===========================================================================
  // Category: tasks
  // ===========================================================================

  registry.register_tool(ToolDefinition{
    "create_task_board",
    "tasks_write",
    "Create a new task board in a space.",
    {
      {"space_id", "string", "The ID of the space", true, {}},
      {"name", "string", "The name of the task board", true, {}},
      {"description", "string", "Description of the task board", false, {}},
    },
    [](Database& db, const std::string& user_id, const json& args) -> ToolResult {
      try {
        auto space_id = args.at("space_id").get<std::string>();
        auto name = args.at("name").get<std::string>();
        std::string description;
        if (args.contains("description") && !args["description"].is_null()) {
          description = args["description"].get<std::string>();
        }

        if (!db.is_space_member(space_id, user_id)) {
          return ToolResult{false, {}, "You are not a member of this space"};
        }

        auto board = db.create_task_board(space_id, name, description, user_id);

        json result;
        result["id"] = board.id;
        result["name"] = board.name;
        result["description"] = board.description;
        result["space_id"] = board.space_id;
        result["created_at"] = board.created_at;
        return ToolResult{true, result, ""};
      } catch (const std::exception& e) {
        return ToolResult{false, {}, std::string("create_task_board failed: ") + e.what()};
      }
    },
  });

  registry.register_tool(ToolDefinition{
    "list_task_columns",
    "tasks_read",
    "List columns on a task board. Use this to find column IDs by name.",
    {
      {"board_id", "string", "The ID of the task board", true, {}},
    },
    [](Database& db, const std::string& /*user_id*/, const json& args) -> ToolResult {
      try {
        auto board_id = args.at("board_id").get<std::string>();

        auto columns = db.list_task_columns(board_id);

        json result = json::array();
        for (const auto& col : columns) {
          json obj;
          obj["id"] = col.id;
          obj["name"] = col.name;
          obj["position"] = col.position;
          obj["wip_limit"] = col.wip_limit;
          result.push_back(obj);
        }
        return ToolResult{true, result, ""};
      } catch (const std::exception& e) {
        return ToolResult{false, {}, std::string("list_task_columns failed: ") + e.what()};
      }
    },
  });

  registry.register_tool(ToolDefinition{
    "create_task_column",
    "tasks_write",
    "Create a new column on a task board.",
    {
      {"board_id", "string", "The ID of the task board", true, {}},
      {"name", "string", "The name of the column", true, {}},
      {"position", "integer", "Position of the column (0-based)", false, {}},
      {"wip_limit", "integer", "Work-in-progress limit (0 = no limit)", false, {}},
    },
    [](Database& db, const std::string& user_id, const json& args) -> ToolResult {
      try {
        auto board_id = args.at("board_id").get<std::string>();
        auto name = args.at("name").get<std::string>();
        int position = 0;
        if (args.contains("position") && !args["position"].is_null()) {
          position = args["position"].get<int>();
        }
        int wip_limit = 0;
        if (args.contains("wip_limit") && !args["wip_limit"].is_null()) {
          wip_limit = args["wip_limit"].get<int>();
        }

        auto board = db.find_task_board(board_id);
        if (!board) {
          return ToolResult{false, {}, "Task board not found"};
        }
        if (!db.is_space_member(board->space_id, user_id)) {
          return ToolResult{false, {}, "You are not a member of this space"};
        }

        auto col = db.create_task_column(board_id, name, position, wip_limit, "");

        json result;
        result["id"] = col.id;
        result["name"] = col.name;
        result["position"] = col.position;
        result["board_id"] = col.board_id;
        return ToolResult{true, result, ""};
      } catch (const std::exception& e) {
        return ToolResult{false, {}, std::string("create_task_column failed: ") + e.what()};
      }
    },
  });

  registry.register_tool(
    ToolDefinition{
      "list_task_boards",
      "tasks_read",
      "List task boards in a space.",
      {
        {"space_id", "string", "The ID of the space", true, {}},
      },
      [](Database& db, const std::string& user_id, const json& args) -> ToolResult {
        try {
          auto space_id = args.at("space_id").get<std::string>();

          if (!db.is_space_member(space_id, user_id)) {
            return ToolResult{false, {}, "You are not a member of this space"};
          }

          auto boards = db.list_task_boards(space_id);

          json result = json::array();
          for (const auto& b : boards) {
            json obj;
            obj["id"] = b.id;
            obj["name"] = b.name;
            obj["description"] = b.description;
            obj["space_id"] = b.space_id;
            obj["created_at"] = b.created_at;
            result.push_back(obj);
          }
          return ToolResult{true, result, ""};
        } catch (const std::exception& e) {
          return ToolResult{false, {}, std::string("list_task_boards failed: ") + e.what()};
        }
      },
    });

  registry.register_tool(
    ToolDefinition{
      "list_tasks",
      "tasks_read",
      "List all tasks on a task board.",
      {
        {"board_id", "string", "The ID of the task board", true, {}},
      },
      [](Database& db, const std::string& /*user_id*/, const json& args) -> ToolResult {
        try {
          auto board_id = args.at("board_id").get<std::string>();

          auto tasks = db.list_tasks(board_id);

          json result = json::array();
          for (const auto& t : tasks) {
            json obj;
            obj["id"] = t.id;
            obj["title"] = t.title;
            obj["description"] = t.description;
            obj["priority"] = t.priority;
            obj["due_date"] = t.due_date;
            obj["column_id"] = t.column_id;
            result.push_back(obj);
          }
          return ToolResult{true, result, ""};
        } catch (const std::exception& e) {
          return ToolResult{false, {}, std::string("list_tasks failed: ") + e.what()};
        }
      },
    });

  registry.register_tool(
    ToolDefinition{
      "create_task",
      "tasks_write",
      "Create a new task on a task board. Provide either column_id or column_name to specify the "
      "column.",
      {
        {"board_id", "string", "The ID of the task board", true, {}},
        {"column_id", "string", "The ID of the column (use this or column_name)", false, {}},
        {"column_name", "string", "The name of the column (use this or column_id)", false, {}},
        {"title", "string", "The task title", true, {}},
        {"description", "string", "The task description", false, {}},
        {"priority", "string", "Task priority level", false, {"low", "medium", "high", "critical"}},
        {"due_date", "string", "Due date in ISO 8601 format", false, {}},
      },
      [](Database& db, const std::string& user_id, const json& args) -> ToolResult {
        try {
          auto board_id = args.at("board_id").get<std::string>();
          std::string column_id;
          if (args.contains("column_id") && !args["column_id"].is_null() &&
              !args["column_id"].get<std::string>().empty()) {
            column_id = args["column_id"].get<std::string>();
          } else if (
            args.contains("column_name") && !args["column_name"].is_null() &&
            !args["column_name"].get<std::string>().empty()) {
            auto col_name = args["column_name"].get<std::string>();
            auto columns = db.list_task_columns(board_id);
            for (const auto& col : columns) {
              // Case-insensitive comparison
              std::string a = col.name, b = col_name;
              std::transform(a.begin(), a.end(), a.begin(), ::tolower);
              std::transform(b.begin(), b.end(), b.begin(), ::tolower);
              if (a == b) {
                column_id = col.id;
                break;
              }
            }
            if (column_id.empty()) {
              return ToolResult{false, {}, "Column '" + col_name + "' not found on this board"};
            }
          } else {
            return ToolResult{false, {}, "Either column_id or column_name is required"};
          }
          auto title = args.at("title").get<std::string>();
          std::string description;
          if (args.contains("description") && !args["description"].is_null()) {
            description = args["description"].get<std::string>();
          }
          std::string priority = "medium";
          if (args.contains("priority") && !args["priority"].is_null()) {
            priority = args["priority"].get<std::string>();
          }
          std::string due_date;
          if (args.contains("due_date") && !args["due_date"].is_null()) {
            due_date = args["due_date"].get<std::string>();
          }

          auto board = db.find_task_board(board_id);
          if (!board) {
            return ToolResult{false, {}, "Task board not found"};
          }

          if (!db.is_space_member(board->space_id, user_id)) {
            return ToolResult{false, {}, "You are not a member of this space"};
          }

          auto task = db.create_task(
            board_id, column_id, title, description, priority, due_date, "", 0, user_id);

          json result;
          result["id"] = task.id;
          result["title"] = task.title;
          result["description"] = task.description;
          result["priority"] = task.priority;
          result["due_date"] = task.due_date;
          result["column_id"] = task.column_id;
          result["board_id"] = task.board_id;
          result["created_at"] = task.created_at;
          return ToolResult{true, result, ""};
        } catch (const std::exception& e) {
          return ToolResult{false, {}, std::string("create_task failed: ") + e.what()};
        }
      },
    });

  registry.register_tool(
    ToolDefinition{
      "update_task",
      "tasks_write",
      "Update an existing task's fields. Use column_id or column_name to move the task.",
      {
        {"task_id", "string", "The ID of the task to update", true, {}},
        {"title", "string", "New task title", false, {}},
        {"description", "string", "New task description", false, {}},
        {"priority", "string", "New priority level", false, {"low", "medium", "high", "critical"}},
        {"due_date", "string", "New due date in ISO 8601 format", false, {}},
        {"column_id", "string", "Move task to this column by ID", false, {}},
        {"column_name", "string", "Move task to this column by name", false, {}},
      },
      [](Database& db, const std::string& user_id, const json& args) -> ToolResult {
        try {
          auto task_id = args.at("task_id").get<std::string>();

          auto existing = db.find_task(task_id);
          if (!existing) {
            return ToolResult{false, {}, "Task not found"};
          }

          auto board = db.find_task_board(existing->board_id);
          if (!board) {
            return ToolResult{false, {}, "Task board not found"};
          }
          if (!db.is_space_member(board->space_id, user_id)) {
            return ToolResult{false, {}, "You are not a member of this space"};
          }

          std::string title = existing->title;
          if (args.contains("title") && !args["title"].is_null()) {
            title = args["title"].get<std::string>();
          }
          std::string description = existing->description;
          if (args.contains("description") && !args["description"].is_null()) {
            description = args["description"].get<std::string>();
          }
          std::string priority = existing->priority;
          if (args.contains("priority") && !args["priority"].is_null()) {
            priority = args["priority"].get<std::string>();
          }
          std::string due_date = existing->due_date;
          if (args.contains("due_date") && !args["due_date"].is_null()) {
            due_date = args["due_date"].get<std::string>();
          }
          std::string column_id = existing->column_id;
          if (args.contains("column_id") && !args["column_id"].is_null() &&
              !args["column_id"].get<std::string>().empty()) {
            column_id = args["column_id"].get<std::string>();
          } else if (
            args.contains("column_name") && !args["column_name"].is_null() &&
            !args["column_name"].get<std::string>().empty()) {
            auto col_name = args["column_name"].get<std::string>();
            auto columns = db.list_task_columns(existing->board_id);
            bool found = false;
            for (const auto& col : columns) {
              std::string a = col.name, b = col_name;
              std::transform(a.begin(), a.end(), a.begin(), ::tolower);
              std::transform(b.begin(), b.end(), b.begin(), ::tolower);
              if (a == b) {
                column_id = col.id;
                found = true;
                break;
              }
            }
            if (!found) {
              return ToolResult{false, {}, "Column '" + col_name + "' not found on this board"};
            }
          }

          auto updated = db.update_task(
            task_id,
            column_id,
            title,
            description,
            priority,
            due_date,
            existing->color,
            existing->position);

          json result;
          result["id"] = updated.id;
          result["title"] = updated.title;
          result["description"] = updated.description;
          result["priority"] = updated.priority;
          result["due_date"] = updated.due_date;
          result["column_id"] = updated.column_id;
          result["board_id"] = updated.board_id;
          result["updated_at"] = updated.updated_at;
          return ToolResult{true, result, ""};
        } catch (const std::exception& e) {
          return ToolResult{false, {}, std::string("update_task failed: ") + e.what()};
        }
      },
    });

  // ===========================================================================
  // Category: calendar
  // ===========================================================================

  registry.register_tool(
    ToolDefinition{
      "list_calendar_events",
      "calendar_read",
      "List calendar events in a space within a date range.",
      {
        {"space_id", "string", "The ID of the space", true, {}},
        {"start_date", "string", "Range start in ISO 8601 format", true, {}},
        {"end_date", "string", "Range end in ISO 8601 format", true, {}},
      },
      [](Database& db, const std::string& user_id, const json& args) -> ToolResult {
        try {
          auto space_id = args.at("space_id").get<std::string>();
          auto start_date = args.at("start_date").get<std::string>();
          auto end_date = args.at("end_date").get<std::string>();

          if (!db.is_space_member(space_id, user_id)) {
            return ToolResult{false, {}, "You are not a member of this space"};
          }

          auto events = db.list_calendar_events(space_id, start_date, end_date);

          json result = json::array();
          for (const auto& ev : events) {
            json obj;
            obj["id"] = ev.id;
            obj["title"] = ev.title;
            obj["description"] = ev.description;
            obj["location"] = ev.location;
            obj["start_time"] = ev.start_time;
            obj["end_time"] = ev.end_time;
            obj["all_day"] = ev.all_day;
            obj["created_at"] = ev.created_at;
            result.push_back(obj);
          }
          return ToolResult{true, result, ""};
        } catch (const std::exception& e) {
          return ToolResult{false, {}, std::string("list_calendar_events failed: ") + e.what()};
        }
      },
    });

  registry.register_tool(
    ToolDefinition{
      "create_calendar_event",
      "calendar_write",
      "Create a new calendar event in a space.",
      {
        {"space_id", "string", "The ID of the space", true, {}},
        {"title", "string", "Event title", true, {}},
        {"start_time", "string", "Start time in ISO 8601 format", true, {}},
        {"end_time", "string", "End time in ISO 8601 format", true, {}},
        {"description", "string", "Event description", false, {}},
        {"location", "string", "Event location", false, {}},
        {"all_day", "boolean", "Whether this is an all-day event", false, {}},
      },
      [](Database& db, const std::string& user_id, const json& args) -> ToolResult {
        try {
          auto space_id = args.at("space_id").get<std::string>();
          auto title = args.at("title").get<std::string>();
          auto start_time = args.at("start_time").get<std::string>();
          auto end_time = args.at("end_time").get<std::string>();
          std::string description;
          if (args.contains("description") && !args["description"].is_null()) {
            description = args["description"].get<std::string>();
          }
          std::string location;
          if (args.contains("location") && !args["location"].is_null()) {
            location = args["location"].get<std::string>();
          }
          bool all_day = false;
          if (args.contains("all_day") && !args["all_day"].is_null()) {
            all_day = args["all_day"].get<bool>();
          }

          if (!db.is_space_member(space_id, user_id)) {
            return ToolResult{false, {}, "You are not a member of this space"};
          }

          auto event = db.create_calendar_event(
            space_id, title, description, location, "", start_time, end_time, all_day, "", user_id);

          json result;
          result["id"] = event.id;
          result["title"] = event.title;
          result["description"] = event.description;
          result["location"] = event.location;
          result["start_time"] = event.start_time;
          result["end_time"] = event.end_time;
          result["all_day"] = event.all_day;
          result["created_at"] = event.created_at;
          return ToolResult{true, result, ""};
        } catch (const std::exception& e) {
          return ToolResult{false, {}, std::string("create_calendar_event failed: ") + e.what()};
        }
      },
    });

  // ===========================================================================
  // Category: wiki
  // ===========================================================================

  registry.register_tool(
    ToolDefinition{
      "list_wiki_pages",
      "wiki_read",
      "List wiki pages in a space, optionally under a parent page.",
      {
        {"space_id", "string", "The ID of the space", true, {}},
        {"parent_id", "string", "Parent page ID (empty for root pages)", false, {}},
      },
      [](Database& db, const std::string& user_id, const json& args) -> ToolResult {
        try {
          auto space_id = args.at("space_id").get<std::string>();
          std::string parent_id;
          if (args.contains("parent_id") && !args["parent_id"].is_null()) {
            parent_id = args["parent_id"].get<std::string>();
          }

          if (!db.is_space_member(space_id, user_id)) {
            return ToolResult{false, {}, "You are not a member of this space"};
          }

          auto pages = db.list_wiki_pages(space_id, parent_id);

          json result = json::array();
          for (const auto& p : pages) {
            json obj;
            obj["id"] = p.id;
            obj["title"] = p.title;
            obj["slug"] = p.slug;
            obj["is_folder"] = p.is_folder;
            result.push_back(obj);
          }
          return ToolResult{true, result, ""};
        } catch (const std::exception& e) {
          return ToolResult{false, {}, std::string("list_wiki_pages failed: ") + e.what()};
        }
      },
    });

  registry.register_tool(
    ToolDefinition{
      "get_wiki_page",
      "wiki_read",
      "Get the full content of a wiki page.",
      {
        {"page_id", "string", "The ID of the wiki page", true, {}},
      },
      [](Database& db, const std::string& user_id, const json& args) -> ToolResult {
        try {
          auto page_id = args.at("page_id").get<std::string>();

          auto page = db.find_wiki_page(page_id);
          if (!page) {
            return ToolResult{false, {}, "Wiki page not found"};
          }

          if (!db.is_space_member(page->space_id, user_id)) {
            return ToolResult{false, {}, "You are not a member of this space"};
          }

          json result;
          result["id"] = page->id;
          result["title"] = page->title;
          result["slug"] = page->slug;
          result["content"] = page->content;
          result["content_text"] = page->content_text;
          result["is_folder"] = page->is_folder;
          result["space_id"] = page->space_id;
          result["parent_id"] = page->parent_id;
          result["created_at"] = page->created_at;
          result["updated_at"] = page->updated_at;
          return ToolResult{true, result, ""};
        } catch (const std::exception& e) {
          return ToolResult{false, {}, std::string("get_wiki_page failed: ") + e.what()};
        }
      },
    });

  registry.register_tool(
    ToolDefinition{
      "create_wiki_page",
      "wiki_write",
      "Create a new wiki page in a space.",
      {
        {"space_id", "string", "The ID of the space", true, {}},
        {"title", "string", "Page title", true, {}},
        {"content", "string", "Page content (plain text or JSON TipTap document)", true, {}},
        {"parent_id", "string", "Parent page ID for nesting", false, {}},
      },
      [](Database& db, const std::string& user_id, const json& args) -> ToolResult {
        try {
          auto space_id = args.at("space_id").get<std::string>();
          auto title = args.at("title").get<std::string>();
          auto content = args.at("content").get<std::string>();
          std::string parent_id;
          if (args.contains("parent_id") && !args["parent_id"].is_null()) {
            parent_id = args["parent_id"].get<std::string>();
          }

          if (!db.is_space_member(space_id, user_id)) {
            return ToolResult{false, {}, "You are not a member of this space"};
          }

          auto slug = generate_slug(title);

          auto page = db.create_wiki_page(
            space_id, parent_id, title, slug, false, content, content, "", 0, user_id);

          json result;
          result["id"] = page.id;
          result["title"] = page.title;
          result["slug"] = page.slug;
          result["space_id"] = page.space_id;
          result["parent_id"] = page.parent_id;
          result["created_at"] = page.created_at;
          return ToolResult{true, result, ""};
        } catch (const std::exception& e) {
          return ToolResult{false, {}, std::string("create_wiki_page failed: ") + e.what()};
        }
      },
    });

  registry.register_tool(
    ToolDefinition{
      "update_wiki_page",
      "wiki_write",
      "Update an existing wiki page's title or content.",
      {
        {"page_id", "string", "The ID of the wiki page to update", true, {}},
        {"title", "string", "New page title", false, {}},
        {"content", "string", "New page content", false, {}},
      },
      [](Database& db, const std::string& user_id, const json& args) -> ToolResult {
        try {
          auto page_id = args.at("page_id").get<std::string>();

          auto existing = db.find_wiki_page(page_id);
          if (!existing) {
            return ToolResult{false, {}, "Wiki page not found"};
          }

          if (!db.is_space_member(existing->space_id, user_id)) {
            return ToolResult{false, {}, "You are not a member of this space"};
          }

          std::string title = existing->title;
          if (args.contains("title") && !args["title"].is_null()) {
            title = args["title"].get<std::string>();
          }
          std::string content = existing->content;
          std::string content_text = existing->content_text;
          if (args.contains("content") && !args["content"].is_null()) {
            content = args["content"].get<std::string>();
            content_text = content;
          }

          auto slug = generate_slug(title);

          auto updated = db.update_wiki_page(
            page_id,
            title,
            slug,
            content,
            content_text,
            existing->icon,
            existing->cover_image_file_id,
            user_id);

          json result;
          result["id"] = updated.id;
          result["title"] = updated.title;
          result["slug"] = updated.slug;
          result["space_id"] = updated.space_id;
          result["updated_at"] = updated.updated_at;
          return ToolResult{true, result, ""};
        } catch (const std::exception& e) {
          return ToolResult{false, {}, std::string("update_wiki_page failed: ") + e.what()};
        }
      },
    });

  // ===========================================================================
  // Category: files
  // ===========================================================================

  registry.register_tool(
    ToolDefinition{
      "list_space_files",
      "files_read",
      "List files and folders in a space directory.",
      {
        {"space_id", "string", "The ID of the space", true, {}},
        {"parent_id", "string", "Parent folder ID (empty for root)", false, {}},
      },
      [](Database& db, const std::string& user_id, const json& args) -> ToolResult {
        try {
          auto space_id = args.at("space_id").get<std::string>();
          std::string parent_id;
          if (args.contains("parent_id") && !args["parent_id"].is_null()) {
            parent_id = args["parent_id"].get<std::string>();
          }

          if (!db.is_space_member(space_id, user_id)) {
            return ToolResult{false, {}, "You are not a member of this space"};
          }

          auto files = db.list_space_files(space_id, parent_id);

          json result = json::array();
          for (const auto& f : files) {
            json obj;
            obj["id"] = f.id;
            obj["name"] = f.name;
            obj["is_folder"] = f.is_folder;
            obj["mime_type"] = f.mime_type;
            obj["file_size"] = f.file_size;
            result.push_back(obj);
          }
          return ToolResult{true, result, ""};
        } catch (const std::exception& e) {
          return ToolResult{false, {}, std::string("list_space_files failed: ") + e.what()};
        }
      },
    });
}
