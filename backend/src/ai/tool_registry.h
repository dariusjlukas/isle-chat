#pragma once
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

using json = nlohmann::json;

class Database;

template <bool SSL>
class WsHandler;

struct ToolParameter {
  std::string name;
  std::string type;  // "string", "integer", "boolean", "number"
  std::string description;
  bool required = true;
  std::vector<std::string> enum_values;  // optional enum constraint
};

struct ToolResult {
  bool success;
  json data;
  std::string error;
};

struct ToolDefinition {
  std::string name;
  std::string category;  // "messaging", "search", "tasks", "calendar", "wiki", "files"
  std::string description;
  std::vector<ToolParameter> parameters;
  std::function<ToolResult(Database& db, const std::string& user_id, const json& args)> execute;
};

class ToolRegistry {
public:
  void register_tool(ToolDefinition tool);

  // Generate OpenAI function-calling tools schema, filtered by enabled categories
  json get_tools_schema(const std::set<std::string>& enabled_categories) const;

  // Execute a tool by name (checks category is enabled)
  ToolResult execute_tool(
    const std::string& tool_name,
    Database& db,
    const std::string& user_id,
    const json& arguments,
    const std::set<std::string>& enabled_categories) const;

  bool has_tool(const std::string& name) const;

private:
  std::map<std::string, ToolDefinition> tools_;
};

// Register all built-in tools
void register_all_tools(ToolRegistry& registry);
