#include "handlers/search_handler.h"
#include <algorithm>
#include <sstream>
#include "handlers/request_scope.h"

using json = nlohmann::json;

template <bool SSL>
void SearchHandler<SSL>::register_routes(uWS::TemplatedApp<SSL>& app) {
  // Global search endpoint
  app.get("/api/search", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("GET", "/api/search");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string query(req->getQuery("q"));
    std::string type(req->getQuery("type"));
    std::string mode(req->getQuery("mode"));
    std::string limit_str(req->getQuery("limit"));
    std::string offset_str(req->getQuery("offset"));

    if (type.empty()) {
      res->writeStatus("400")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Missing type parameter"})");
      return;
    }

    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    pool_.submit([this,
                  res,
                  aborted,
                  scope,
                  token = std::move(token),
                  query = std::move(query),
                  type = std::move(type),
                  mode = std::move(mode),
                  limit_str = std::move(limit_str),
                  offset_str = std::move(offset_str)]() {
      auto user_id = db.validate_session(token);
      if (!user_id) {
        loop_->defer([res, aborted, scope]() {
          if (*aborted) return;
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Unauthorized"})");
          scope->observe(401);
        });
        return;
      }

      int limit =
        std::clamp(handler_utils::safe_parse_int(limit_str, 20), 1, defaults::SEARCH_MAX_RESULTS);
      int offset = std::max(0, handler_utils::safe_parse_int(offset_str, 0));
      std::string effective_mode = mode.empty() ? "and" : mode;

      // Split query by | delimiter for multi-term
      auto terms = query.empty() ? std::vector<std::string>{""} : split_terms(query);

      try {
        if (type == "users") {
          auto results = db.search_users(terms[0], limit, offset);
          json arr = json::array();
          for (const auto& u : results) {
            arr.push_back(
              {{"id", u.id},
               {"username", u.username},
               {"display_name", u.display_name},
               {"role", u.role},
               {"is_online", u.is_online},
               {"last_seen", u.last_seen},
               {"bio", u.bio},
               {"status", u.status},
               {"avatar_file_id", u.avatar_file_id},
               {"profile_color", u.profile_color}});
          }
          auto body = json({{"type", "users"}, {"results", arr}}).dump();
          loop_->defer([res, aborted, scope, body = std::move(body)]() {
            if (*aborted) return;
            res->writeHeader("Content-Type", "application/json")->end(body);
            scope->observe(200);
          });
        } else if (type == "messages") {
          auto user = db.find_user_by_id(*user_id);
          bool is_admin = user && (user->role == "admin" || user->role == "owner");
          std::vector<Database::MessageSearchResult> results;
          if (query.empty()) {
            results = db.browse_messages(*user_id, is_admin, limit, offset);
          } else {
            std::string tsquery = build_tsquery(terms, effective_mode);
            results = db.search_messages(tsquery, *user_id, is_admin, limit, offset);
          }
          json arr = json::array();
          for (const auto& m : results) {
            arr.push_back(
              {{"id", m.id},
               {"channel_id", m.channel_id},
               {"channel_name", m.channel_name},
               {"space_name", m.space_name},
               {"user_id", m.user_id},
               {"username", m.username},
               {"content", m.content},
               {"created_at", m.created_at},
               {"is_direct", m.is_direct}});
          }
          auto body = json({{"type", "messages"}, {"results", arr}}).dump();
          loop_->defer([res, aborted, scope, body = std::move(body)]() {
            if (*aborted) return;
            res->writeHeader("Content-Type", "application/json")->end(body);
            scope->observe(200);
          });
        } else if (type == "files") {
          auto user = db.find_user_by_id(*user_id);
          bool is_admin = user && (user->role == "admin" || user->role == "owner");
          auto results = db.search_files(terms[0], *user_id, is_admin, limit, offset);
          json arr = json::array();
          for (const auto& f : results) {
            arr.push_back(
              {{"message_id", f.message_id},
               {"channel_id", f.channel_id},
               {"channel_name", f.channel_name},
               {"user_id", f.user_id},
               {"username", f.username},
               {"file_id", f.file_id},
               {"file_name", f.file_name},
               {"file_type", f.file_type},
               {"file_size", f.file_size},
               {"created_at", f.created_at},
               {"source", f.source},
               {"space_id", f.space_id},
               {"space_name", f.space_name},
               {"is_folder", f.is_folder}});
          }
          auto body = json({{"type", "files"}, {"results", arr}}).dump();
          loop_->defer([res, aborted, scope, body = std::move(body)]() {
            if (*aborted) return;
            res->writeHeader("Content-Type", "application/json")->end(body);
            scope->observe(200);
          });
        } else if (type == "channels") {
          auto user = db.find_user_by_id(*user_id);
          bool is_admin = user && (user->role == "admin" || user->role == "owner");
          auto results = db.search_channels(terms[0], *user_id, is_admin, limit, offset);
          json arr = json::array();
          for (const auto& c : results) {
            arr.push_back(
              {{"id", c.id},
               {"name", c.name},
               {"description", c.description},
               {"space_name", c.space_name},
               {"space_id", c.space_id},
               {"is_public", c.is_public}});
          }
          auto body = json({{"type", "channels"}, {"results", arr}}).dump();
          loop_->defer([res, aborted, scope, body = std::move(body)]() {
            if (*aborted) return;
            res->writeHeader("Content-Type", "application/json")->end(body);
            scope->observe(200);
          });
        } else if (type == "spaces") {
          auto user = db.find_user_by_id(*user_id);
          bool is_admin = user && (user->role == "admin" || user->role == "owner");
          auto results = db.search_spaces(terms[0], *user_id, is_admin, limit, offset);
          json arr = json::array();
          for (const auto& s : results) {
            arr.push_back(
              {{"id", s.id},
               {"name", s.name},
               {"description", s.description},
               {"is_public", s.is_public},
               {"avatar_file_id", s.avatar_file_id},
               {"profile_color", s.profile_color}});
          }
          auto body = json({{"type", "spaces"}, {"results", arr}}).dump();
          loop_->defer([res, aborted, scope, body = std::move(body)]() {
            if (*aborted) return;
            res->writeHeader("Content-Type", "application/json")->end(body);
            scope->observe(200);
          });
        } else if (type == "wiki") {
          auto user = db.find_user_by_id(*user_id);
          bool is_admin = user && (user->role == "admin" || user->role == "owner");
          std::vector<Database::WikiSearchResult> results;
          if (query.empty()) {
            results = db.browse_wiki_pages(*user_id, is_admin, limit, offset);
          } else {
            std::string tsquery = build_tsquery(terms, effective_mode, "simple");
            results = db.search_wiki_pages(tsquery, query, *user_id, is_admin, limit, offset);
          }
          json arr = json::array();
          for (const auto& w : results) {
            arr.push_back(
              {{"id", w.id},
               {"space_id", w.space_id},
               {"space_name", w.space_name},
               {"title", w.title},
               {"snippet", w.snippet},
               {"created_at", w.created_at},
               {"created_by_username", w.created_by_username}});
          }
          auto body = json({{"type", "wiki"}, {"results", arr}}).dump();
          loop_->defer([res, aborted, scope, body = std::move(body)]() {
            if (*aborted) return;
            res->writeHeader("Content-Type", "application/json")->end(body);
            scope->observe(200);
          });
        } else {
          loop_->defer([res, aborted, scope]() {
            if (*aborted) return;
            res->writeStatus("400")
              ->writeHeader("Content-Type", "application/json")
              ->end(
                R"({"error":"Invalid type. Use users, messages, files, channels, spaces, or wiki"})");
            scope->observe(400);
          });
        }
      } catch (const std::exception& e) {
        auto err = json({{"error", e.what()}}).dump();
        loop_->defer([res, aborted, scope, err = std::move(err)]() {
          if (*aborted) return;
          res->writeStatus("500")->writeHeader("Content-Type", "application/json")->end(err);
          scope->observe(500);
        });
      }
    });
  });

  // Composite search endpoint
  app.get("/api/search/composite", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("GET", "/api/search/composite");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string filters_str(req->getQuery("filters"));
    std::string result_type(req->getQuery("result_type"));
    std::string mode(req->getQuery("mode"));
    std::string limit_str(req->getQuery("limit"));
    std::string offset_str(req->getQuery("offset"));

    if (filters_str.empty()) {
      res->writeStatus("400")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Missing filters parameter"})");
      return;
    }

    auto filters = parse_filters(filters_str);
    if (filters.empty()) {
      res->writeStatus("400")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Invalid filters format. Use type:value pairs separated by commas"})");
      return;
    }

    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    pool_.submit([this,
                  res,
                  aborted,
                  scope,
                  token = std::move(token),
                  result_type = std::move(result_type),
                  mode = std::move(mode),
                  limit_str = std::move(limit_str),
                  offset_str = std::move(offset_str),
                  filters = std::move(filters)]() {
      auto user_id = db.validate_session(token);
      if (!user_id) {
        loop_->defer([res, aborted, scope]() {
          if (*aborted) return;
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Unauthorized"})");
          scope->observe(401);
        });
        return;
      }

      std::string effective_result_type = result_type.empty() ? "messages" : result_type;
      int limit =
        std::clamp(handler_utils::safe_parse_int(limit_str, 20), 1, defaults::SEARCH_MAX_RESULTS);
      int offset = std::max(0, handler_utils::safe_parse_int(offset_str, 0));
      std::string effective_mode = mode.empty() ? "and" : mode;

      try {
        auto user = db.find_user_by_id(*user_id);
        bool is_admin = user && (user->role == "admin" || user->role == "owner");

        if (effective_result_type == "messages") {
          auto results = db.search_composite_messages(
            filters, effective_mode, *user_id, is_admin, limit, offset);
          json arr = json::array();
          for (const auto& m : results) {
            arr.push_back(
              {{"id", m.id},
               {"channel_id", m.channel_id},
               {"channel_name", m.channel_name},
               {"space_name", m.space_name},
               {"user_id", m.user_id},
               {"username", m.username},
               {"content", m.content},
               {"created_at", m.created_at},
               {"is_direct", m.is_direct}});
          }
          auto body = json({{"type", "messages"}, {"results", arr}}).dump();
          loop_->defer([res, aborted, scope, body = std::move(body)]() {
            if (*aborted) return;
            res->writeHeader("Content-Type", "application/json")->end(body);
            scope->observe(200);
          });
        } else if (effective_result_type == "files") {
          auto results =
            db.search_composite_files(filters, effective_mode, *user_id, is_admin, limit, offset);
          json arr = json::array();
          for (const auto& f : results) {
            arr.push_back(
              {{"message_id", f.message_id},
               {"channel_id", f.channel_id},
               {"channel_name", f.channel_name},
               {"user_id", f.user_id},
               {"username", f.username},
               {"file_id", f.file_id},
               {"file_name", f.file_name},
               {"file_type", f.file_type},
               {"file_size", f.file_size},
               {"created_at", f.created_at},
               {"source", f.source},
               {"space_id", f.space_id},
               {"space_name", f.space_name},
               {"is_folder", f.is_folder}});
          }
          auto body = json({{"type", "files"}, {"results", arr}}).dump();
          loop_->defer([res, aborted, scope, body = std::move(body)]() {
            if (*aborted) return;
            res->writeHeader("Content-Type", "application/json")->end(body);
            scope->observe(200);
          });
        } else if (effective_result_type == "users") {
          auto results =
            db.search_composite_users(filters, effective_mode, *user_id, is_admin, limit, offset);
          json arr = json::array();
          for (const auto& u : results) {
            arr.push_back(
              {{"id", u.id},
               {"username", u.username},
               {"display_name", u.display_name},
               {"role", u.role},
               {"is_online", u.is_online},
               {"last_seen", u.last_seen},
               {"bio", u.bio},
               {"status", u.status},
               {"avatar_file_id", u.avatar_file_id},
               {"profile_color", u.profile_color}});
          }
          auto body = json({{"type", "users"}, {"results", arr}}).dump();
          loop_->defer([res, aborted, scope, body = std::move(body)]() {
            if (*aborted) return;
            res->writeHeader("Content-Type", "application/json")->end(body);
            scope->observe(200);
          });
        } else if (effective_result_type == "channels") {
          auto results = db.search_composite_channels(
            filters, effective_mode, *user_id, is_admin, limit, offset);
          json arr = json::array();
          for (const auto& c : results) {
            arr.push_back(
              {{"id", c.id},
               {"name", c.name},
               {"description", c.description},
               {"space_name", c.space_name},
               {"space_id", c.space_id},
               {"is_public", c.is_public}});
          }
          auto body = json({{"type", "channels"}, {"results", arr}}).dump();
          loop_->defer([res, aborted, scope, body = std::move(body)]() {
            if (*aborted) return;
            res->writeHeader("Content-Type", "application/json")->end(body);
            scope->observe(200);
          });
        } else if (effective_result_type == "spaces") {
          auto results =
            db.search_composite_spaces(filters, effective_mode, *user_id, is_admin, limit, offset);
          json arr = json::array();
          for (const auto& s : results) {
            arr.push_back(
              {{"id", s.id},
               {"name", s.name},
               {"description", s.description},
               {"is_public", s.is_public},
               {"avatar_file_id", s.avatar_file_id},
               {"profile_color", s.profile_color}});
          }
          auto body = json({{"type", "spaces"}, {"results", arr}}).dump();
          loop_->defer([res, aborted, scope, body = std::move(body)]() {
            if (*aborted) return;
            res->writeHeader("Content-Type", "application/json")->end(body);
            scope->observe(200);
          });
        } else {
          loop_->defer([res, aborted, scope]() {
            if (*aborted) return;
            res->writeStatus("400")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Invalid result_type"})");
            scope->observe(400);
          });
        }
      } catch (const std::exception& e) {
        auto err = json({{"error", e.what()}}).dump();
        loop_->defer([res, aborted, scope, err = std::move(err)]() {
          if (*aborted) return;
          res->writeStatus("500")->writeHeader("Content-Type", "application/json")->end(err);
          scope->observe(500);
        });
      }
    });
  });

  // Messages around a target (for jump-to-message)
  app.get("/api/channels/:id/messages/around", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("GET", "/api/channels/:id/messages/around");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string channel_id(req->getParameter("id"));
    std::string message_id(req->getQuery("message_id"));
    std::string limit_str(req->getQuery("limit"));

    if (message_id.empty()) {
      res->writeStatus("400")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Missing message_id parameter"})");
      return;
    }

    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    pool_.submit([this,
                  res,
                  aborted,
                  scope,
                  token = std::move(token),
                  channel_id = std::move(channel_id),
                  message_id = std::move(message_id),
                  limit_str = std::move(limit_str)]() {
      auto user_id = db.validate_session(token);
      if (!user_id) {
        loop_->defer([res, aborted, scope]() {
          if (*aborted) return;
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Unauthorized"})");
          scope->observe(401);
        });
        return;
      }

      std::string role = db.get_effective_role(channel_id, *user_id);
      if (role.empty()) {
        loop_->defer([res, aborted, scope]() {
          if (*aborted) return;
          res->writeStatus("403")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Not a member of this channel"})");
          scope->observe(403);
        });
        return;
      }

      int limit = std::clamp(handler_utils::safe_parse_int(limit_str, 50), 1, 100);

      try {
        auto msgs = db.get_messages_around(channel_id, message_id, limit);

        std::vector<std::string> msg_ids;
        for (const auto& msg : msgs) msg_ids.push_back(msg.id);
        auto reactions_map = db.get_reactions_for_messages(msg_ids);

        json arr = json::array();
        for (const auto& msg : msgs) {
          json j = {
            {"id", msg.id},
            {"channel_id", msg.channel_id},
            {"user_id", msg.user_id},
            {"username", msg.username},
            {"content", msg.content},
            {"created_at", msg.created_at},
            {"is_deleted", msg.is_deleted}};
          if (!msg.edited_at.empty()) j["edited_at"] = msg.edited_at;
          if (!msg.file_id.empty()) {
            j["file_id"] = msg.file_id;
            j["file_name"] = msg.file_name;
            j["file_size"] = msg.file_size;
            j["file_type"] = msg.file_type;
          }
          if (!msg.reply_to_message_id.empty()) {
            j["reply_to_message_id"] = msg.reply_to_message_id;
            j["reply_to_username"] = msg.reply_to_username;
            j["reply_to_content"] = msg.reply_to_content;
            j["reply_to_is_deleted"] = msg.reply_to_is_deleted;
          }
          auto it = reactions_map.find(msg.id);
          if (it != reactions_map.end() && !it->second.empty()) {
            json rarr = json::array();
            for (const auto& r : it->second) {
              rarr.push_back(
                {{"emoji", r.emoji}, {"user_id", r.user_id}, {"username", r.username}});
            }
            j["reactions"] = rarr;
          }
          arr.push_back(j);
        }
        auto body = arr.dump();
        loop_->defer([res, aborted, scope, body = std::move(body)]() {
          if (*aborted) return;
          res->writeHeader("Content-Type", "application/json")->end(body);
          scope->observe(200);
        });
      } catch (const std::exception& e) {
        auto err = json({{"error", e.what()}}).dump();
        loop_->defer([res, aborted, scope, err = std::move(err)]() {
          if (*aborted) return;
          res->writeStatus("500")->writeHeader("Content-Type", "application/json")->end(err);
          scope->observe(500);
        });
      }
    });
  });
}

template <bool SSL>
std::string SearchHandler<SSL>::get_user_id(uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req) {
  return validate_session_or_401(res, req, db);
}

template <bool SSL>
std::vector<std::string> SearchHandler<SSL>::split_terms(const std::string& input) {
  std::vector<std::string> terms;
  std::istringstream ss(input);
  std::string term;
  while (std::getline(ss, term, '|')) {
    size_t start = term.find_first_not_of(' ');
    size_t end = term.find_last_not_of(' ');
    if (start != std::string::npos) {
      terms.push_back(term.substr(start, end - start + 1));
    }
  }
  if (terms.empty()) terms.push_back(input);
  return terms;
}

template <bool SSL>
std::string SearchHandler<SSL>::build_tsquery(
  const std::vector<std::string>& terms, const std::string& mode, const std::string& config) {
  std::string op = (mode == "or") ? " || " : " && ";
  std::string result;
  for (size_t i = 0; i < terms.size(); i++) {
    if (i > 0) result += op;
    result += "websearch_to_tsquery('" + config + "', " + quote_literal(terms[i]) + ")";
  }
  return result;
}

template <bool SSL>
std::string SearchHandler<SSL>::quote_literal(const std::string& s) {
  std::string result = "'";
  for (char c : s) {
    if (c == '\'')
      result += "''";
    else
      result += c;
  }
  result += "'";
  return result;
}

template <bool SSL>
std::vector<Database::CompositeFilter> SearchHandler<SSL>::parse_filters(const std::string& input) {
  std::vector<Database::CompositeFilter> filters;
  std::istringstream ss(input);
  std::string segment;
  while (std::getline(ss, segment, ',')) {
    auto colon = segment.find(':');
    if (colon == std::string::npos || colon == 0 || colon == segment.size() - 1) {
      continue;
    }
    std::string type = segment.substr(0, colon);
    std::string value = segment.substr(colon + 1);
    // Trim
    auto trim = [](std::string& s) {
      size_t start = s.find_first_not_of(' ');
      size_t end = s.find_last_not_of(' ');
      if (start == std::string::npos) {
        s.clear();
        return;
      }
      s = s.substr(start, end - start + 1);
    };
    trim(type);
    trim(value);
    if (
      type == "messages" || type == "users" || type == "files" || type == "channels" ||
      type == "spaces") {
      filters.push_back({type, value});
    }
  }
  return filters;
}

template struct SearchHandler<false>;
template struct SearchHandler<true>;
