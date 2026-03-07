#pragma once
#include <App.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include "db/database.h"
#include "handlers/handler_utils.h"

using json = nlohmann::json;

template <bool SSL>
struct SearchHandler {
    Database& db;

    void register_routes(uWS::TemplatedApp<SSL>& app) {
        // Global search endpoint
        app.get("/api/search", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            std::string query(req->getQuery("q"));
            std::string type(req->getQuery("type"));
            std::string mode(req->getQuery("mode"));
            std::string limit_str(req->getQuery("limit"));
            std::string offset_str(req->getQuery("offset"));

            if (query.empty() || type.empty()) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Missing q or type parameter"})");
                return;
            }

            int limit = limit_str.empty() ? 20 : std::min(std::stoi(limit_str), defaults::SEARCH_MAX_RESULTS);
            int offset = offset_str.empty() ? 0 : std::stoi(offset_str);
            if (mode.empty()) mode = "and";

            // Split query by | delimiter for multi-term
            auto terms = split_terms(query);

            try {
                if (type == "users") {
                    auto results = db.search_users(terms[0], limit, offset);
                    json arr = json::array();
                    for (const auto& u : results) {
                        arr.push_back({{"id", u.id}, {"username", u.username},
                                       {"display_name", u.display_name}, {"role", u.role},
                                       {"is_online", u.is_online}, {"last_seen", u.last_seen},
                                       {"bio", u.bio}, {"status", u.status}});
                    }
                    json resp = {{"type", "users"}, {"results", arr}};
                    res->writeHeader("Content-Type", "application/json")->end(resp.dump());
                } else if (type == "messages") {
                    auto user = db.find_user_by_id(user_id);
                    bool is_admin = user && (user->role == "admin" || user->role == "owner");
                    std::string tsquery = build_tsquery(terms, mode);
                    auto results = db.search_messages(tsquery, user_id, is_admin, limit, offset);
                    json arr = json::array();
                    for (const auto& m : results) {
                        arr.push_back({{"id", m.id}, {"channel_id", m.channel_id},
                                       {"channel_name", m.channel_name},
                                       {"space_name", m.space_name},
                                       {"user_id", m.user_id}, {"username", m.username},
                                       {"content", m.content}, {"created_at", m.created_at},
                                       {"is_direct", m.is_direct}});
                    }
                    json resp = {{"type", "messages"}, {"results", arr}};
                    res->writeHeader("Content-Type", "application/json")->end(resp.dump());
                } else if (type == "files") {
                    auto user = db.find_user_by_id(user_id);
                    bool is_admin = user && (user->role == "admin" || user->role == "owner");
                    auto results = db.search_files(terms[0], user_id, is_admin, limit, offset);
                    json arr = json::array();
                    for (const auto& f : results) {
                        arr.push_back({{"message_id", f.message_id},
                                       {"channel_id", f.channel_id},
                                       {"channel_name", f.channel_name},
                                       {"user_id", f.user_id}, {"username", f.username},
                                       {"file_id", f.file_id}, {"file_name", f.file_name},
                                       {"file_type", f.file_type}, {"file_size", f.file_size},
                                       {"created_at", f.created_at}});
                    }
                    json resp = {{"type", "files"}, {"results", arr}};
                    res->writeHeader("Content-Type", "application/json")->end(resp.dump());
                } else if (type == "channels") {
                    auto user = db.find_user_by_id(user_id);
                    bool is_admin = user && (user->role == "admin" || user->role == "owner");
                    auto results = db.search_channels(terms[0], user_id, is_admin, limit, offset);
                    json arr = json::array();
                    for (const auto& c : results) {
                        arr.push_back({{"id", c.id}, {"name", c.name},
                                       {"description", c.description},
                                       {"space_name", c.space_name},
                                       {"space_id", c.space_id},
                                       {"is_public", c.is_public}});
                    }
                    json resp = {{"type", "channels"}, {"results", arr}};
                    res->writeHeader("Content-Type", "application/json")->end(resp.dump());
                } else if (type == "spaces") {
                    auto user = db.find_user_by_id(user_id);
                    bool is_admin = user && (user->role == "admin" || user->role == "owner");
                    auto results = db.search_spaces(terms[0], user_id, is_admin, limit, offset);
                    json arr = json::array();
                    for (const auto& s : results) {
                        arr.push_back({{"id", s.id}, {"name", s.name},
                                       {"description", s.description},
                                       {"icon", s.icon},
                                       {"is_public", s.is_public}});
                    }
                    json resp = {{"type", "spaces"}, {"results", arr}};
                    res->writeHeader("Content-Type", "application/json")->end(resp.dump());
                } else {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Invalid type. Use users, messages, files, channels, or spaces"})");
                }
            } catch (const std::exception& e) {
                res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                    ->end(json({{"error", e.what()}}).dump());
            }
        });

        // Composite search endpoint
        app.get("/api/search/composite", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            std::string filters_str(req->getQuery("filters"));
            std::string result_type(req->getQuery("result_type"));
            std::string mode(req->getQuery("mode"));
            std::string limit_str(req->getQuery("limit"));
            std::string offset_str(req->getQuery("offset"));

            if (filters_str.empty()) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Missing filters parameter"})");
                return;
            }
            if (result_type.empty()) result_type = "messages";

            int limit = limit_str.empty() ? 20 : std::min(std::stoi(limit_str), defaults::SEARCH_MAX_RESULTS);
            int offset = offset_str.empty() ? 0 : std::stoi(offset_str);
            if (mode.empty()) mode = "and";

            auto filters = parse_filters(filters_str);
            if (filters.empty()) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Invalid filters format. Use type:value pairs separated by commas"})");
                return;
            }

            try {
                auto user = db.find_user_by_id(user_id);
                bool is_admin = user && (user->role == "admin" || user->role == "owner");

                if (result_type == "messages") {
                    auto results = db.search_composite_messages(filters, mode, user_id, is_admin, limit, offset);
                    json arr = json::array();
                    for (const auto& m : results) {
                        arr.push_back({{"id", m.id}, {"channel_id", m.channel_id},
                                       {"channel_name", m.channel_name},
                                       {"space_name", m.space_name},
                                       {"user_id", m.user_id}, {"username", m.username},
                                       {"content", m.content}, {"created_at", m.created_at},
                                       {"is_direct", m.is_direct}});
                    }
                    json resp = {{"type", "messages"}, {"results", arr}};
                    res->writeHeader("Content-Type", "application/json")->end(resp.dump());
                } else if (result_type == "files") {
                    auto results = db.search_composite_files(filters, mode, user_id, is_admin, limit, offset);
                    json arr = json::array();
                    for (const auto& f : results) {
                        arr.push_back({{"message_id", f.message_id},
                                       {"channel_id", f.channel_id},
                                       {"channel_name", f.channel_name},
                                       {"user_id", f.user_id}, {"username", f.username},
                                       {"file_id", f.file_id}, {"file_name", f.file_name},
                                       {"file_type", f.file_type}, {"file_size", f.file_size},
                                       {"created_at", f.created_at}});
                    }
                    json resp = {{"type", "files"}, {"results", arr}};
                    res->writeHeader("Content-Type", "application/json")->end(resp.dump());
                } else if (result_type == "users") {
                    auto results = db.search_composite_users(filters, mode, user_id, is_admin, limit, offset);
                    json arr = json::array();
                    for (const auto& u : results) {
                        arr.push_back({{"id", u.id}, {"username", u.username},
                                       {"display_name", u.display_name}, {"role", u.role},
                                       {"is_online", u.is_online}, {"last_seen", u.last_seen},
                                       {"bio", u.bio}, {"status", u.status}});
                    }
                    json resp = {{"type", "users"}, {"results", arr}};
                    res->writeHeader("Content-Type", "application/json")->end(resp.dump());
                } else if (result_type == "channels") {
                    auto results = db.search_composite_channels(filters, mode, user_id, is_admin, limit, offset);
                    json arr = json::array();
                    for (const auto& c : results) {
                        arr.push_back({{"id", c.id}, {"name", c.name},
                                       {"description", c.description},
                                       {"space_name", c.space_name},
                                       {"space_id", c.space_id},
                                       {"is_public", c.is_public}});
                    }
                    json resp = {{"type", "channels"}, {"results", arr}};
                    res->writeHeader("Content-Type", "application/json")->end(resp.dump());
                } else if (result_type == "spaces") {
                    auto results = db.search_composite_spaces(filters, mode, user_id, is_admin, limit, offset);
                    json arr = json::array();
                    for (const auto& s : results) {
                        arr.push_back({{"id", s.id}, {"name", s.name},
                                       {"description", s.description},
                                       {"icon", s.icon},
                                       {"is_public", s.is_public}});
                    }
                    json resp = {{"type", "spaces"}, {"results", arr}};
                    res->writeHeader("Content-Type", "application/json")->end(resp.dump());
                } else {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Invalid result_type"})");
                }
            } catch (const std::exception& e) {
                res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                    ->end(json({{"error", e.what()}}).dump());
            }
        });

        // Messages around a target (for jump-to-message)
        app.get("/api/channels/:id/messages/around", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            std::string channel_id(req->getParameter("id"));
            std::string message_id(req->getQuery("message_id"));
            std::string limit_str(req->getQuery("limit"));

            if (message_id.empty()) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Missing message_id parameter"})");
                return;
            }

            std::string role = db.get_effective_role(channel_id, user_id);
            if (role.empty()) {
                res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Not a member of this channel"})");
                return;
            }

            int limit = limit_str.empty() ? 50 : std::min(std::stoi(limit_str), 100);

            try {
                auto msgs = db.get_messages_around(channel_id, message_id, limit);

                std::vector<std::string> msg_ids;
                for (const auto& msg : msgs) msg_ids.push_back(msg.id);
                auto reactions_map = db.get_reactions_for_messages(msg_ids);

                json arr = json::array();
                for (const auto& msg : msgs) {
                    json j = {{"id", msg.id}, {"channel_id", msg.channel_id},
                              {"user_id", msg.user_id}, {"username", msg.username},
                              {"content", msg.content}, {"created_at", msg.created_at},
                              {"is_deleted", msg.is_deleted}};
                    if (!msg.edited_at.empty()) j["edited_at"] = msg.edited_at;
                    if (!msg.file_id.empty()) {
                        j["file_id"] = msg.file_id;
                        j["file_name"] = msg.file_name;
                        j["file_size"] = msg.file_size;
                        j["file_type"] = msg.file_type;
                    }
                    auto it = reactions_map.find(msg.id);
                    if (it != reactions_map.end() && !it->second.empty()) {
                        json rarr = json::array();
                        for (const auto& r : it->second) {
                            rarr.push_back({{"emoji", r.emoji}, {"user_id", r.user_id}, {"username", r.username}});
                        }
                        j["reactions"] = rarr;
                    }
                    arr.push_back(j);
                }
                res->writeHeader("Content-Type", "application/json")->end(arr.dump());
            } catch (const std::exception& e) {
                res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                    ->end(json({{"error", e.what()}}).dump());
            }
        });
    }

private:
    std::string get_user_id(uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req) {
        return validate_session_or_401(res, req, db);
    }

    static std::vector<std::string> split_terms(const std::string& input) {
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

    static std::string build_tsquery(const std::vector<std::string>& terms,
                                      const std::string& mode) {
        std::string op = (mode == "or") ? " || " : " && ";
        std::string result;
        for (size_t i = 0; i < terms.size(); i++) {
            if (i > 0) result += op;
            result += "websearch_to_tsquery('english', " + quote_literal(terms[i]) + ")";
        }
        return result;
    }

    static std::string quote_literal(const std::string& s) {
        std::string result = "'";
        for (char c : s) {
            if (c == '\'') result += "''";
            else result += c;
        }
        result += "'";
        return result;
    }

    static std::vector<Database::CompositeFilter> parse_filters(const std::string& input) {
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
                if (start == std::string::npos) { s.clear(); return; }
                s = s.substr(start, end - start + 1);
            };
            trim(type);
            trim(value);
            if (type == "messages" || type == "users" || type == "files" ||
                type == "channels" || type == "spaces") {
                filters.push_back({type, value});
            }
        }
        return filters;
    }
};
