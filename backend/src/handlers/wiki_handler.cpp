#include "handlers/wiki_handler.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <pqxx/pqxx>
#include "handlers/cors_utils.h"
#include "handlers/file_access_utils.h"
#include "handlers/format_utils.h"
#include "handlers/request_scope.h"
#include "logging/logger.h"

using json = nlohmann::json;

static json page_to_json(const WikiPage& p) {
  return {
    {"id", p.id},
    {"space_id", p.space_id},
    {"parent_id", p.parent_id.empty() ? json(nullptr) : json(p.parent_id)},
    {"title", p.title},
    {"slug", p.slug},
    {"is_folder", p.is_folder},
    {"content", p.content},
    {"content_text", p.content_text},
    {"icon", p.icon},
    {"cover_image_file_id", p.cover_image_file_id},
    {"position", p.position},
    {"is_deleted", p.is_deleted},
    {"created_by", p.created_by},
    {"created_by_username", p.created_by_username},
    {"created_at", p.created_at},
    {"updated_at", p.updated_at},
    {"last_edited_by", p.last_edited_by},
    {"last_edited_by_username", p.last_edited_by_username}};
}

static json version_to_json(const WikiPageVersion& v) {
  return {
    {"id", v.id},
    {"page_id", v.page_id},
    {"version_number", v.version_number},
    {"title", v.title},
    {"content", v.content},
    {"content_text", v.content_text},
    {"is_major", v.is_major},
    {"edited_by", v.edited_by},
    {"edited_by_username", v.edited_by_username},
    {"created_at", v.created_at}};
}

static json page_permission_to_json(const WikiPagePermission& p) {
  return {
    {"id", p.id},
    {"page_id", p.page_id},
    {"user_id", p.user_id},
    {"username", p.username},
    {"display_name", p.display_name},
    {"permission", p.permission},
    {"granted_by", p.granted_by},
    {"granted_by_username", p.granted_by_username},
    {"created_at", p.created_at}};
}

static json wiki_permission_to_json(const WikiPermission& p) {
  return {
    {"id", p.id},
    {"space_id", p.space_id},
    {"user_id", p.user_id},
    {"username", p.username},
    {"display_name", p.display_name},
    {"permission", p.permission},
    {"granted_by", p.granted_by},
    {"granted_by_username", p.granted_by_username},
    {"created_at", p.created_at}};
}

static std::string generate_slug(const std::string& title) {
  std::string slug;
  slug.reserve(title.size());
  for (char c : title) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      slug += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else {
      slug += '-';
    }
  }
  // Collapse multiple hyphens
  std::string result;
  result.reserve(slug.size());
  bool prev_hyphen = false;
  for (char c : slug) {
    if (c == '-') {
      if (!prev_hyphen) result += c;
      prev_hyphen = true;
    } else {
      result += c;
      prev_hyphen = false;
    }
  }
  // Trim leading/trailing hyphens
  size_t start = result.find_first_not_of('-');
  if (start == std::string::npos) return "page";
  size_t end = result.find_last_not_of('-');
  return result.substr(start, end - start + 1);
}

template <bool SSL>
void WikiHandler<SSL>::register_routes(uWS::TemplatedApp<SSL>& app) {
  // --- Pages ---

  // List pages in folder
  app.get("/api/spaces/:id/wiki/pages", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("GET", "/api/spaces/:id/wiki/pages");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    auto token = extract_session_token(req);
    std::string space_id(req->getParameter("id"));
    std::string parent_id(req->getQuery("parent_id"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit([this,
                  res,
                  aborted,
                  scope,
                  token = std::move(token),
                  space_id = std::move(space_id),
                  parent_id = std::move(parent_id),
                  origin]() {
      auto user_id_opt = db.validate_session(token);
      if (!user_id_opt) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Unauthorized"})");
          scope->observe(401);
        });
        return;
      }
      auto user_id = *user_id_opt;
      if (!check_space_access(res, aborted, space_id, user_id, origin)) return;
      if (!require_permission(res, aborted, space_id, user_id, "view", origin)) return;

      auto pages = db.list_wiki_pages(space_id, parent_id);

      json arr = json::array();
      for (const auto& p : pages) arr.push_back(page_to_json(p));

      json resp = {{"pages", arr}, {"my_permission", get_access_level(space_id, user_id)}};
      auto body = resp.dump();
      loop_->defer([res, aborted, scope, body = std::move(body), origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(body);
        scope->observe(200);
      });
    });
  });

  // Full page tree for sidebar
  app.get("/api/spaces/:id/wiki/tree", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("GET", "/api/spaces/:id/wiki/tree");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    auto token = extract_session_token(req);
    std::string space_id(req->getParameter("id"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit([this,
                  res,
                  aborted,
                  scope,
                  token = std::move(token),
                  space_id = std::move(space_id),
                  origin]() {
      auto user_id_opt = db.validate_session(token);
      if (!user_id_opt) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Unauthorized"})");
          scope->observe(401);
        });
        return;
      }
      auto user_id = *user_id_opt;
      if (!check_space_access(res, aborted, space_id, user_id, origin)) return;
      if (!require_permission(res, aborted, space_id, user_id, "view", origin)) return;

      auto pages = db.get_wiki_tree(space_id);

      json arr = json::array();
      for (const auto& p : pages) arr.push_back(page_to_json(p));

      json resp = {{"pages", arr}, {"my_permission", get_access_level(space_id, user_id)}};
      auto body = resp.dump();
      loop_->defer([res, aborted, scope, body = std::move(body), origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(body);
        scope->observe(200);
      });
    });
  });

  // Create page or folder
  app.post("/api/spaces/:id/wiki/pages", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("POST", "/api/spaces/:id/wiki/pages");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    auto token = extract_session_token(req);
    std::string space_id(req->getParameter("id"));
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 scope,
                 token = std::move(token),
                 space_id = std::move(space_id),
                 body = std::move(body),
                 origin](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    scope,
                    body = std::move(body),
                    token = std::move(token),
                    space_id = std::move(space_id),
                    origin]() {
        auto user_id_opt = db.validate_session(token);
        if (!user_id_opt) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            res->writeStatus("401")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Unauthorized"})");
            scope->observe(401);
          });
          return;
        }
        auto user_id = *user_id_opt;
        if (!check_space_access(res, aborted, space_id, user_id, origin)) return;
        if (!require_permission(res, aborted, space_id, user_id, "edit", origin)) return;

        try {
          auto j = json::parse(body);
          std::string title = j.at("title");
          std::string parent_id = j.value("parent_id", "");
          bool is_folder = j.value("is_folder", false);
          std::string content = j.value("content", "");
          std::string content_text = content;
          std::string icon = j.value("icon", "");
          int position = j.value("position", 0);

          if (title.empty() || title.length() > 255) {
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"json({"error":"Title is required (max 255 characters)"})json");
              scope->observe(400);
            });
            return;
          }

          // Generate slug from title
          std::string slug = generate_slug(title);
          if (slug.empty()) slug = "page";

          // Ensure unique slug within parent
          if (db.wiki_page_slug_exists(space_id, parent_id, slug)) {
            int suffix = 2;
            while (
              db.wiki_page_slug_exists(space_id, parent_id, slug + "-" + std::to_string(suffix))) {
              suffix++;
            }
            slug = slug + "-" + std::to_string(suffix);
          }

          auto page = db.create_wiki_page(
            space_id,
            parent_id,
            title,
            slug,
            is_folder,
            content,
            content_text,
            icon,
            position,
            user_id);
          // Create initial version for history (major)
          db.create_wiki_page_version(page.id, title, content, content_text, user_id, true);

          auto creator = db.find_user_by_id(user_id);
          page.created_by_username = creator ? creator->username : "";

          auto resp_body = page_to_json(page).dump();
          loop_->defer([res, aborted, scope, resp_body = std::move(resp_body), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(resp_body);
            scope->observe(200);
          });
        } catch (const std::exception& e) {
          LOG_ERROR_N("wiki", nullptr, std::string("Create page error: ") + e.what());
          auto err = json({{"error", e.what()}}).dump();
          loop_->defer([res, aborted, scope, err = std::move(err), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
            scope->observe(400);
          });
        }
      });
    });
  });

  // Get page with content + path
  app.get("/api/spaces/:id/wiki/pages/:pageId", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("GET", "/api/spaces/:id/wiki/pages/:pageId");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    auto token = extract_session_token(req);
    std::string space_id(req->getParameter(0));
    std::string page_id(req->getParameter(1));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit([this,
                  res,
                  aborted,
                  scope,
                  token = std::move(token),
                  space_id = std::move(space_id),
                  page_id = std::move(page_id),
                  origin]() {
      auto user_id_opt = db.validate_session(token);
      if (!user_id_opt) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Unauthorized"})");
          scope->observe(401);
        });
        return;
      }
      auto user_id = *user_id_opt;
      if (!check_space_access(res, aborted, space_id, user_id, origin)) return;
      if (!require_page_permission(res, aborted, space_id, page_id, user_id, "view", origin))
        return;

      auto page = db.find_wiki_page(page_id);
      if (!page || page->space_id != space_id || page->is_deleted) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("404")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Page not found"})");
          scope->observe(404);
        });
        return;
      }

      json resp = page_to_json(*page);
      resp["my_permission"] = get_page_access_level(space_id, page_id, user_id);

      // Include breadcrumb path
      auto path = db.get_wiki_page_path(page_id);
      json path_arr = json::array();
      for (const auto& p : path) {
        path_arr.push_back({{"id", p.id}, {"title", p.title}, {"slug", p.slug}});
      }
      resp["path"] = path_arr;

      auto body = resp.dump();
      loop_->defer([res, aborted, scope, body = std::move(body), origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(body);
        scope->observe(200);
      });
    });
  });

  // Update page
  app.put("/api/spaces/:id/wiki/pages/:pageId", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("PUT", "/api/spaces/:id/wiki/pages/:pageId");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    auto token = extract_session_token(req);
    std::string space_id(req->getParameter(0));
    std::string page_id(req->getParameter(1));
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 scope,
                 token = std::move(token),
                 space_id = std::move(space_id),
                 page_id = std::move(page_id),
                 body = std::move(body),
                 origin](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    scope,
                    body = std::move(body),
                    token = std::move(token),
                    space_id = std::move(space_id),
                    page_id = std::move(page_id),
                    origin]() {
        auto user_id_opt = db.validate_session(token);
        if (!user_id_opt) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            res->writeStatus("401")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Unauthorized"})");
            scope->observe(401);
          });
          return;
        }
        auto user_id = *user_id_opt;
        if (!check_space_access(res, aborted, space_id, user_id, origin)) return;
        if (!require_page_permission(res, aborted, space_id, page_id, user_id, "edit", origin))
          return;

        auto existing = db.find_wiki_page(page_id);
        if (!existing || existing->space_id != space_id || existing->is_deleted) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("404")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Page not found"})");
            scope->observe(404);
          });
          return;
        }

        try {
          auto j = json::parse(body);
          std::string title = j.value("title", existing->title);
          std::string content = j.value("content", existing->content);
          std::string content_text = content;
          std::string icon = j.value("icon", existing->icon);
          std::string cover_image_file_id =
            j.value("cover_image_file_id", existing->cover_image_file_id);
          bool create_version = j.value("create_version", false);

          // Generate slug from title if title changed
          std::string slug = existing->slug;
          if (title != existing->title) {
            slug = generate_slug(title);
            if (slug.empty()) slug = "page";
            if (db.wiki_page_slug_exists(space_id, existing->parent_id, slug, page_id)) {
              int suffix = 2;
              while (db.wiki_page_slug_exists(
                space_id, existing->parent_id, slug + "-" + std::to_string(suffix), page_id)) {
                suffix++;
              }
              slug = slug + "-" + std::to_string(suffix);
            }
          }

          // Create a major version snapshot when explicitly requested (e.g. leaving edit mode)
          // but only if content or title actually changed since the last version
          if (create_version) {
            auto prev_versions = db.list_wiki_page_versions(page_id);
            bool changed = prev_versions.empty() || prev_versions[0].title != existing->title ||
                           prev_versions[0].content != existing->content;
            if (changed) {
              db.create_wiki_page_version(
                page_id, existing->title, existing->content, existing->content_text, user_id, true);
            }
          }

          // Update page
          auto page = db.update_wiki_page(
            page_id, title, slug, content, content_text, icon, cover_image_file_id, user_id);

          auto creator = db.find_user_by_id(page.created_by);
          page.created_by_username = creator ? creator->username : "";
          auto editor = db.find_user_by_id(user_id);
          page.last_edited_by_username = editor ? editor->username : "";

          auto resp_body = page_to_json(page).dump();
          loop_->defer([res, aborted, scope, resp_body = std::move(resp_body), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(resp_body);
            scope->observe(200);
          });
        } catch (const std::exception& e) {
          auto err = json({{"error", e.what()}}).dump();
          loop_->defer([res, aborted, scope, err = std::move(err), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
            scope->observe(400);
          });
        }
      });
    });
  });

  // Delete page (soft-delete)
  app.del("/api/spaces/:id/wiki/pages/:pageId", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("DEL", "/api/spaces/:id/wiki/pages/:pageId");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    auto token = extract_session_token(req);
    std::string space_id(req->getParameter(0));
    std::string page_id(req->getParameter(1));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit([this,
                  res,
                  aborted,
                  scope,
                  token = std::move(token),
                  space_id = std::move(space_id),
                  page_id = std::move(page_id),
                  origin]() {
      auto user_id_opt = db.validate_session(token);
      if (!user_id_opt) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Unauthorized"})");
          scope->observe(401);
        });
        return;
      }
      auto user_id = *user_id_opt;
      if (!check_space_access(res, aborted, space_id, user_id, origin)) return;

      auto page = db.find_wiki_page(page_id);
      if (!page || page->space_id != space_id || page->is_deleted) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("404")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Page not found"})");
          scope->observe(404);
        });
        return;
      }

      // Owner of the page can delete with edit permission, otherwise need owner
      if (page->created_by == user_id) {
        if (!require_page_permission(res, aborted, space_id, page_id, user_id, "edit", origin))
          return;
      } else {
        if (!require_page_permission(res, aborted, space_id, page_id, user_id, "owner", origin))
          return;
      }

      db.soft_delete_wiki_page(page_id);
      loop_->defer([res, aborted, scope, origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
        scope->observe(200);
      });
    });
  });

  // Move page to new parent
  app.put("/api/spaces/:id/wiki/pages/:pageId/move", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>(
      "PUT", "/api/spaces/:id/wiki/pages/:pageId/move");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    auto token = extract_session_token(req);
    std::string space_id(req->getParameter(0));
    std::string page_id(req->getParameter(1));
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 scope,
                 token = std::move(token),
                 space_id = std::move(space_id),
                 page_id = std::move(page_id),
                 body = std::move(body),
                 origin](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    scope,
                    body = std::move(body),
                    token = std::move(token),
                    space_id = std::move(space_id),
                    page_id = std::move(page_id),
                    origin]() {
        auto user_id_opt = db.validate_session(token);
        if (!user_id_opt) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            res->writeStatus("401")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Unauthorized"})");
            scope->observe(401);
          });
          return;
        }
        auto user_id = *user_id_opt;
        if (!check_space_access(res, aborted, space_id, user_id, origin)) return;
        if (!require_page_permission(res, aborted, space_id, page_id, user_id, "edit", origin))
          return;

        auto page = db.find_wiki_page(page_id);
        if (!page || page->space_id != space_id || page->is_deleted) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("404")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Page not found"})");
            scope->observe(404);
          });
          return;
        }

        try {
          auto j = json::parse(body);
          std::string new_parent_id = j.value("parent_id", "");

          db.move_wiki_page(page_id, new_parent_id);

          auto updated = db.find_wiki_page(page_id);
          auto resp_body = page_to_json(updated ? *updated : *page).dump();
          loop_->defer([res, aborted, scope, resp_body = std::move(resp_body), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(resp_body);
            scope->observe(200);
          });
        } catch (const std::exception& e) {
          auto err = json({{"error", e.what()}}).dump();
          loop_->defer([res, aborted, scope, err = std::move(err), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
            scope->observe(400);
          });
        }
      });
    });
  });

  // Reorder pages within folder
  app.post("/api/spaces/:id/wiki/pages/reorder", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("POST", "/api/spaces/:id/wiki/pages/reorder");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    auto token = extract_session_token(req);
    std::string space_id(req->getParameter("id"));
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 scope,
                 token = std::move(token),
                 space_id = std::move(space_id),
                 body = std::move(body),
                 origin](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    scope,
                    body = std::move(body),
                    token = std::move(token),
                    space_id = std::move(space_id),
                    origin]() {
        auto user_id_opt = db.validate_session(token);
        if (!user_id_opt) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            res->writeStatus("401")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Unauthorized"})");
            scope->observe(401);
          });
          return;
        }
        auto user_id = *user_id_opt;
        if (!check_space_access(res, aborted, space_id, user_id, origin)) return;
        if (!require_permission(res, aborted, space_id, user_id, "edit", origin)) return;

        try {
          auto j = json::parse(body);
          auto positions = j.at("positions").get<std::vector<json>>();
          std::vector<std::pair<std::string, int>> page_positions;
          for (const auto& pos : positions) {
            page_positions.emplace_back(
              pos.at("id").get<std::string>(), pos.at("position").get<int>());
          }
          db.reorder_wiki_pages(page_positions);
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
            scope->observe(200);
          });
        } catch (const std::exception& e) {
          auto err = json({{"error", e.what()}}).dump();
          loop_->defer([res, aborted, scope, err = std::move(err), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
            scope->observe(400);
          });
        }
      });
    });
  });

  // --- Versions ---

  // List version history
  app.get("/api/spaces/:id/wiki/pages/:pageId/versions", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>(
      "GET", "/api/spaces/:id/wiki/pages/:pageId/versions");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    auto token = extract_session_token(req);
    std::string space_id(req->getParameter(0));
    std::string page_id(req->getParameter(1));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit([this,
                  res,
                  aborted,
                  scope,
                  token = std::move(token),
                  space_id = std::move(space_id),
                  page_id = std::move(page_id),
                  origin]() {
      auto user_id_opt = db.validate_session(token);
      if (!user_id_opt) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Unauthorized"})");
          scope->observe(401);
        });
        return;
      }
      auto user_id = *user_id_opt;
      if (!check_space_access(res, aborted, space_id, user_id, origin)) return;
      if (!require_page_permission(res, aborted, space_id, page_id, user_id, "view", origin))
        return;

      auto page = db.find_wiki_page(page_id);
      if (!page || page->space_id != space_id || page->is_deleted) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("404")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Page not found"})");
          scope->observe(404);
        });
        return;
      }

      auto versions = db.list_wiki_page_versions(page_id);
      json arr = json::array();
      for (const auto& v : versions) arr.push_back(version_to_json(v));

      json resp = {{"versions", arr}};
      auto body = resp.dump();
      loop_->defer([res, aborted, scope, body = std::move(body), origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(body);
        scope->observe(200);
      });
    });
  });

  // Get specific version
  app.get("/api/spaces/:id/wiki/pages/:pageId/versions/:versionId", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>(
      "GET", "/api/spaces/:id/wiki/pages/:pageId/versions/:versionId");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    auto token = extract_session_token(req);
    std::string space_id(req->getParameter(0));
    std::string page_id(req->getParameter(1));
    std::string version_id(req->getParameter(2));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit([this,
                  res,
                  aborted,
                  scope,
                  token = std::move(token),
                  space_id = std::move(space_id),
                  page_id = std::move(page_id),
                  version_id = std::move(version_id),
                  origin]() {
      auto user_id_opt = db.validate_session(token);
      if (!user_id_opt) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Unauthorized"})");
          scope->observe(401);
        });
        return;
      }
      auto user_id = *user_id_opt;
      if (!check_space_access(res, aborted, space_id, user_id, origin)) return;
      if (!require_page_permission(res, aborted, space_id, page_id, user_id, "view", origin))
        return;

      auto page = db.find_wiki_page(page_id);
      if (!page || page->space_id != space_id || page->is_deleted) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("404")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Page not found"})");
          scope->observe(404);
        });
        return;
      }

      auto version = db.get_wiki_page_version(version_id);
      if (!version || version->page_id != page_id) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("404")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Version not found"})");
          scope->observe(404);
        });
        return;
      }

      auto body = version_to_json(*version).dump();
      loop_->defer([res, aborted, scope, body = std::move(body), origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(body);
        scope->observe(200);
      });
    });
  });

  // Revert to version
  app.post(
    "/api/spaces/:id/wiki/pages/:pageId/versions/:versionId/revert", [this](auto* res, auto* req) {
      auto scope = std::make_shared<handler_utils::RequestScope>(
        "POST", "/api/spaces/:id/wiki/pages/:pageId/versions/:versionId/revert");
      handler_utils::set_request_id_header(res, *scope);
      std::string origin(req->getHeader("origin"));
      auto token = extract_session_token(req);
      std::string space_id(req->getParameter(0));
      std::string page_id(req->getParameter(1));
      std::string version_id(req->getParameter(2));
      std::string body;
      auto aborted = std::make_shared<bool>(false);
      res->onAborted([aborted, origin]() { *aborted = true; });
      res->onData([this,
                   res,
                   aborted,
                   scope,
                   token = std::move(token),
                   space_id = std::move(space_id),
                   page_id = std::move(page_id),
                   version_id = std::move(version_id),
                   body = std::move(body),
                   origin](std::string_view data, bool last) mutable {
        body.append(data);
        if (!last) return;
        pool_.submit([this,
                      res,
                      aborted,
                      scope,
                      body = std::move(body),
                      token = std::move(token),
                      space_id = std::move(space_id),
                      page_id = std::move(page_id),
                      version_id = std::move(version_id),
                      origin]() {
          auto user_id_opt = db.validate_session(token);
          if (!user_id_opt) {
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              res->writeStatus("401")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Unauthorized"})");
              scope->observe(401);
            });
            return;
          }
          auto user_id = *user_id_opt;
          if (!check_space_access(res, aborted, space_id, user_id, origin)) return;
          if (!require_page_permission(res, aborted, space_id, page_id, user_id, "edit", origin))
            return;

          auto page = db.find_wiki_page(page_id);
          if (!page || page->space_id != space_id || page->is_deleted) {
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("404")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Page not found"})");
              scope->observe(404);
            });
            return;
          }

          auto version = db.get_wiki_page_version(version_id);
          if (!version || version->page_id != page_id) {
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("404")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Version not found"})");
              scope->observe(404);
            });
            return;
          }

          try {
            // Create a new version with the current content
            db.create_wiki_page_version(
              page_id, page->title, page->content, page->content_text, user_id);

            // Update page with the version's content
            auto updated = db.update_wiki_page(
              page_id,
              version->title,
              page->slug,
              version->content,
              version->content_text,
              page->icon,
              page->cover_image_file_id,
              user_id);

            auto creator = db.find_user_by_id(updated.created_by);
            updated.created_by_username = creator ? creator->username : "";
            auto editor = db.find_user_by_id(user_id);
            updated.last_edited_by_username = editor ? editor->username : "";

            auto resp_body = page_to_json(updated).dump();
            loop_->defer([res, aborted, scope, resp_body = std::move(resp_body), origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeHeader("Content-Type", "application/json")->end(resp_body);
              scope->observe(200);
            });
          } catch (const std::exception& e) {
            auto err = json({{"error", e.what()}}).dump();
            loop_->defer([res, aborted, scope, err = std::move(err), origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
              scope->observe(400);
            });
          }
        });
      });
    });

  // --- Page Permissions ---

  // List page permissions
  app.get("/api/spaces/:id/wiki/pages/:pageId/permissions", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>(
      "GET", "/api/spaces/:id/wiki/pages/:pageId/permissions");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    auto token = extract_session_token(req);
    std::string space_id(req->getParameter(0));
    std::string page_id(req->getParameter(1));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit([this,
                  res,
                  aborted,
                  scope,
                  token = std::move(token),
                  space_id = std::move(space_id),
                  page_id = std::move(page_id),
                  origin]() {
      auto user_id_opt = db.validate_session(token);
      if (!user_id_opt) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Unauthorized"})");
          scope->observe(401);
        });
        return;
      }
      auto user_id = *user_id_opt;
      if (!check_space_access(res, aborted, space_id, user_id, origin)) return;
      if (!require_page_permission(res, aborted, space_id, page_id, user_id, "view", origin))
        return;

      auto perms = db.get_wiki_page_permissions(page_id);
      json arr = json::array();
      for (const auto& p : perms) arr.push_back(page_permission_to_json(p));

      json resp = {
        {"permissions", arr}, {"my_permission", get_page_access_level(space_id, page_id, user_id)}};
      auto body = resp.dump();
      loop_->defer([res, aborted, scope, body = std::move(body), origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(body);
        scope->observe(200);
      });
    });
  });

  // Set page permission
  app.post("/api/spaces/:id/wiki/pages/:pageId/permissions", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>(
      "POST", "/api/spaces/:id/wiki/pages/:pageId/permissions");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    auto token = extract_session_token(req);
    std::string space_id(req->getParameter(0));
    std::string page_id(req->getParameter(1));
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 scope,
                 token = std::move(token),
                 space_id = std::move(space_id),
                 page_id = std::move(page_id),
                 body = std::move(body),
                 origin](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    scope,
                    body = std::move(body),
                    token = std::move(token),
                    space_id = std::move(space_id),
                    page_id = std::move(page_id),
                    origin]() {
        auto user_id_opt = db.validate_session(token);
        if (!user_id_opt) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            res->writeStatus("401")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Unauthorized"})");
            scope->observe(401);
          });
          return;
        }
        auto user_id = *user_id_opt;
        if (!check_space_access(res, aborted, space_id, user_id, origin)) return;
        if (!require_page_permission(res, aborted, space_id, page_id, user_id, "owner", origin))
          return;

        try {
          auto j = json::parse(body);
          std::string target_user_id = j.at("user_id");
          std::string permission = j.at("permission");

          if (permission != "owner" && permission != "edit" && permission != "view") {
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Invalid permission level"})");
              scope->observe(400);
            });
            return;
          }

          // Personal spaces: only view and edit allowed, not owner
          {
            auto space_perm_check = db.find_space_by_id(space_id);
            if (space_perm_check && space_perm_check->is_personal && permission == "owner") {
              loop_->defer([res, aborted, scope, origin]() {
                if (*aborted) return;
                cors::apply(res, origin);
                res->writeStatus("400")
                  ->writeHeader("Content-Type", "application/json")
                  ->end(R"({"error":"Cannot assign owner permission in a personal space"})");
                scope->observe(400);
              });
              return;
            }
          }

          db.set_wiki_page_permission(page_id, target_user_id, permission, user_id);
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
            scope->observe(200);
          });
        } catch (const std::exception& e) {
          auto err = json({{"error", e.what()}}).dump();
          loop_->defer([res, aborted, scope, err = std::move(err), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
            scope->observe(400);
          });
        }
      });
    });
  });

  // Remove page permission
  app.del("/api/spaces/:id/wiki/pages/:pageId/permissions/:userId", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>(
      "DEL", "/api/spaces/:id/wiki/pages/:pageId/permissions/:userId");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    auto token = extract_session_token(req);
    std::string space_id(req->getParameter(0));
    std::string page_id(req->getParameter(1));
    std::string target_user_id(req->getParameter(2));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit([this,
                  res,
                  aborted,
                  scope,
                  token = std::move(token),
                  space_id = std::move(space_id),
                  page_id = std::move(page_id),
                  target_user_id = std::move(target_user_id),
                  origin]() {
      auto user_id_opt = db.validate_session(token);
      if (!user_id_opt) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Unauthorized"})");
          scope->observe(401);
        });
        return;
      }
      auto user_id = *user_id_opt;
      if (!check_space_access(res, aborted, space_id, user_id, origin)) return;
      if (!require_page_permission(res, aborted, space_id, page_id, user_id, "owner", origin))
        return;

      db.remove_wiki_page_permission(page_id, target_user_id);
      loop_->defer([res, aborted, scope, origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
        scope->observe(200);
      });
    });
  });

  // --- Space-level Wiki Permissions ---

  // List space-level wiki permissions
  app.get("/api/spaces/:id/wiki/permissions", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("GET", "/api/spaces/:id/wiki/permissions");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    auto token = extract_session_token(req);
    std::string space_id(req->getParameter("id"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit([this,
                  res,
                  aborted,
                  scope,
                  token = std::move(token),
                  space_id = std::move(space_id),
                  origin]() {
      auto user_id_opt = db.validate_session(token);
      if (!user_id_opt) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Unauthorized"})");
          scope->observe(401);
        });
        return;
      }
      auto user_id = *user_id_opt;
      if (!check_space_access(res, aborted, space_id, user_id, origin)) return;
      if (!require_permission(res, aborted, space_id, user_id, "view", origin)) return;

      auto perms = db.get_wiki_permissions(space_id);
      json arr = json::array();
      for (const auto& p : perms) arr.push_back(wiki_permission_to_json(p));

      json resp = {{"permissions", arr}, {"my_permission", get_access_level(space_id, user_id)}};
      auto body = resp.dump();
      loop_->defer([res, aborted, scope, body = std::move(body), origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(body);
        scope->observe(200);
      });
    });
  });

  // Set space-level wiki permission
  app.post("/api/spaces/:id/wiki/permissions", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("POST", "/api/spaces/:id/wiki/permissions");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    auto token = extract_session_token(req);
    std::string space_id(req->getParameter("id"));
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 scope,
                 token = std::move(token),
                 space_id = std::move(space_id),
                 body = std::move(body),
                 origin](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    scope,
                    body = std::move(body),
                    token = std::move(token),
                    space_id = std::move(space_id),
                    origin]() {
        auto user_id_opt = db.validate_session(token);
        if (!user_id_opt) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            res->writeStatus("401")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Unauthorized"})");
            scope->observe(401);
          });
          return;
        }
        auto user_id = *user_id_opt;
        if (!check_space_access(res, aborted, space_id, user_id, origin)) return;
        if (!require_permission(res, aborted, space_id, user_id, "owner", origin)) return;

        try {
          auto j = json::parse(body);
          std::string target_user_id = j.at("user_id");
          std::string permission = j.at("permission");

          if (permission != "owner" && permission != "edit" && permission != "view") {
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Invalid permission level"})");
              scope->observe(400);
            });
            return;
          }

          // Personal spaces: only view and edit allowed, not owner
          {
            auto space_perm_check = db.find_space_by_id(space_id);
            if (space_perm_check && space_perm_check->is_personal && permission == "owner") {
              loop_->defer([res, aborted, scope, origin]() {
                if (*aborted) return;
                cors::apply(res, origin);
                res->writeStatus("400")
                  ->writeHeader("Content-Type", "application/json")
                  ->end(R"({"error":"Cannot assign owner permission in a personal space"})");
                scope->observe(400);
              });
              return;
            }
          }

          db.set_wiki_permission(space_id, target_user_id, permission, user_id);
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
            scope->observe(200);
          });
        } catch (const std::exception& e) {
          auto err = json({{"error", e.what()}}).dump();
          loop_->defer([res, aborted, scope, err = std::move(err), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
            scope->observe(400);
          });
        }
      });
    });
  });

  // Remove space-level wiki permission
  app.del("/api/spaces/:id/wiki/permissions/:userId", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>(
      "DEL", "/api/spaces/:id/wiki/permissions/:userId");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    auto token = extract_session_token(req);
    std::string space_id(req->getParameter(0));
    std::string target_user_id(req->getParameter(1));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit([this,
                  res,
                  aborted,
                  scope,
                  token = std::move(token),
                  space_id = std::move(space_id),
                  target_user_id = std::move(target_user_id),
                  origin]() {
      auto user_id_opt = db.validate_session(token);
      if (!user_id_opt) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Unauthorized"})");
          scope->observe(401);
        });
        return;
      }
      auto user_id = *user_id_opt;
      if (!check_space_access(res, aborted, space_id, user_id, origin)) return;
      if (!require_permission(res, aborted, space_id, user_id, "owner", origin)) return;

      db.remove_wiki_permission(space_id, target_user_id);
      loop_->defer([res, aborted, scope, origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
        scope->observe(200);
      });
    });
  });

  // --- Chunked upload: init ---
  app.post("/api/spaces/:id/wiki/upload/init", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("POST", "/api/spaces/:id/wiki/upload/init");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    auto token = extract_session_token(req);
    std::string space_id(req->getParameter("id"));
    auto body = std::make_shared<std::string>();
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 scope,
                 body,
                 token = std::move(token),
                 space_id = std::move(space_id),
                 origin](std::string_view data, bool last) mutable {
      body->append(data);
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    scope,
                    body,
                    token = std::move(token),
                    space_id = std::move(space_id),
                    origin]() {
        auto user_id_opt = db.validate_session(token);
        if (!user_id_opt) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            res->writeStatus("401")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Unauthorized"})");
            scope->observe(401);
          });
          return;
        }
        auto user_id = *user_id_opt;

        try {
          auto j = json::parse(*body);
          std::string filename = j.value("filename", "upload");
          std::string content_type = j.value("content_type", "application/octet-stream");
          int64_t total_size = j.value("total_size", int64_t(0));
          int chunk_count = j.value("chunk_count", 0);
          int64_t chunk_size = j.value("chunk_size", int64_t(0));

          if (chunk_count <= 0 || chunk_size <= 0) {
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Invalid chunk count or size"})");
              scope->observe(400);
            });
            return;
          }

          if (!check_space_access(res, aborted, space_id, user_id, origin)) return;
          if (!require_permission(res, aborted, space_id, user_id, "edit", origin)) return;

          int64_t max_size = file_access_utils::parse_max_file_size(
            db.get_setting("max_file_size"), config.max_file_size);
          if (file_access_utils::exceeds_file_size_limit(max_size, total_size)) {
            std::string msg = file_access_utils::file_too_large_message(max_size);
            auto err = json{{"error", msg}}.dump();
            loop_->defer([res, aborted, scope, err = std::move(err), origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("413")->writeHeader("Content-Type", "application/json")->end(err);
              scope->observe(413);
            });
            return;
          }

          int64_t max_storage =
            file_access_utils::parse_max_storage_size(db.get_setting("max_storage_size"));
          if (
            max_storage > 0 && file_access_utils::exceeds_storage_limit(
                                 max_storage, db.get_total_file_size(), total_size)) {
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("413")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Server storage limit reached"})");
              scope->observe(413);
            });
            return;
          }

          int64_t space_limit = file_access_utils::parse_space_storage_limit(
            db.get_setting("space_storage_limit_" + space_id));
          if (
            space_limit > 0 && file_access_utils::exceeds_storage_limit(
                                 space_limit, db.get_space_storage_used(space_id), total_size)) {
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("413")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Space storage limit reached"})");
              scope->observe(413);
            });
            return;
          }

          json metadata = {
            {"filename", filename}, {"content_type", content_type}, {"space_id", space_id}};
          std::string upload_id =
            uploads.create_session(user_id, total_size, chunk_count, chunk_size, metadata);

          auto resp_body = json{{"upload_id", upload_id}}.dump();
          loop_->defer([res, aborted, scope, resp_body = std::move(resp_body), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(resp_body);
            scope->observe(200);
          });
        } catch (...) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Invalid request body"})");
            scope->observe(400);
          });
        }
      });
    });
  });

  // --- Chunked upload: receive chunk ---
  app.post("/api/spaces/:id/wiki/upload/:uploadId/chunk", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>(
      "POST", "/api/spaces/:id/wiki/upload/:uploadId/chunk");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    auto token = extract_session_token(req);
    std::string upload_id(req->getParameter(1));
    std::string index_str(req->getQuery("index"));
    std::string expected_hash(req->getQuery("hash"));

    int index = -1;
    try {
      index = std::stoi(index_str);
    } catch (...) {}

    auto body = std::make_shared<std::string>();
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 scope,
                 body,
                 token = std::move(token),
                 upload_id = std::move(upload_id),
                 index,
                 expected_hash = std::move(expected_hash),
                 origin](std::string_view data, bool last) mutable {
      body->append(data);
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    scope,
                    body,
                    token = std::move(token),
                    upload_id = std::move(upload_id),
                    index,
                    expected_hash = std::move(expected_hash),
                    origin]() {
        auto user_id_opt = db.validate_session(token);
        if (!user_id_opt) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            res->writeStatus("401")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Unauthorized"})");
            scope->observe(401);
          });
          return;
        }
        auto user_id = *user_id_opt;

        auto session = uploads.get_session(upload_id);
        if (!session || session->user_id != user_id) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("404")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Upload session not found"})");
            scope->observe(404);
          });
          return;
        }

        auto err = uploads.store_chunk_err(upload_id, index, *body, expected_hash);
        if (!err.empty()) {
          if (err == "hash_mismatch") {
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("409")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Chunk integrity check failed"})");
              scope->observe(409);
            });
          } else if (err == "invalid_index") {
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Invalid chunk index"})");
              scope->observe(400);
            });
          } else {
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("500")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Failed to store chunk"})");
              scope->observe(500);
            });
          }
          return;
        }

        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
          scope->observe(200);
        });
      });
    });
  });

  // --- Chunked upload: complete ---
  app.post("/api/spaces/:id/wiki/upload/:uploadId/complete", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>(
      "POST", "/api/spaces/:id/wiki/upload/:uploadId/complete");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    auto token = extract_session_token(req);
    std::string space_id(req->getParameter(0));
    std::string upload_id(req->getParameter(1));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 scope,
                 token = std::move(token),
                 space_id = std::move(space_id),
                 upload_id = std::move(upload_id),
                 origin](std::string_view, bool last) mutable {
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    scope,
                    token = std::move(token),
                    space_id = std::move(space_id),
                    upload_id = std::move(upload_id),
                    origin]() {
        auto user_id_opt = db.validate_session(token);
        if (!user_id_opt) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            res->writeStatus("401")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Unauthorized"})");
            scope->observe(401);
          });
          return;
        }
        auto user_id = *user_id_opt;

        auto session = uploads.get_session(upload_id);
        if (!session || session->user_id != user_id) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("404")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Upload session not found"})");
            scope->observe(404);
          });
          return;
        }

        if (!uploads.is_complete(upload_id)) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Not all chunks have been uploaded"})");
            scope->observe(400);
          });
          return;
        }

        if (!check_space_access(res, aborted, space_id, user_id, origin)) {
          uploads.remove_session(upload_id);
          return;
        }

        if (!require_permission(res, aborted, space_id, user_id, "edit", origin)) {
          uploads.remove_session(upload_id);
          return;
        }

        try {
          std::string filename = session->metadata.value("filename", "upload");
          std::string content_type =
            session->metadata.value("content_type", "application/octet-stream");

          std::string disk_file_id = format_utils::random_hex(32);
          std::string dest_path = config.upload_dir + "/" + disk_file_id;

          int64_t assembled_size = uploads.assemble(upload_id, dest_path);
          if (assembled_size < 0) {
            uploads.remove_session(upload_id);
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("500")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Failed to assemble file"})");
              scope->observe(500);
            });
            return;
          }

          if (assembled_size != session->total_size) {
            std::filesystem::remove(dest_path);
            uploads.remove_session(upload_id);
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Assembled file size does not match expected size"})");
              scope->observe(400);
            });
            return;
          }

          // Create space_file with unique name (hidden via tool_source)
          std::string unique_name = disk_file_id + "_" + filename;
          auto file = db.create_space_file(
            space_id, "", unique_name, disk_file_id, assembled_size, content_type, user_id);

          // Mark as wiki file
          db.set_space_file_tool_source(file.id, "wiki");

          uploads.remove_session(upload_id);

          json resp = {{"file_id", file.id}, {"url", "/api/files/" + disk_file_id}};
          auto resp_body = resp.dump();
          loop_->defer([res, aborted, scope, resp_body = std::move(resp_body), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(resp_body);
            scope->observe(200);
          });
        } catch (const pqxx::unique_violation&) {
          uploads.remove_session(upload_id);
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("409")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"A file with this name already exists"})");
            scope->observe(409);
          });
        } catch (const std::exception& e) {
          uploads.remove_session(upload_id);
          auto err = json({{"error", e.what()}}).dump();
          loop_->defer([res, aborted, scope, err = std::move(err), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("500")->writeHeader("Content-Type", "application/json")->end(err);
            scope->observe(500);
          });
        }
      });
    });
  });
}

// --- Permission helpers ---

template <bool SSL>
std::string WikiHandler<SSL>::get_user_id(uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req) {
  return validate_session_or_401(res, req, db);
}

template <bool SSL>
bool WikiHandler<SSL>::check_space_access(
  uWS::HttpResponse<SSL>* res,
  std::shared_ptr<bool> aborted,
  const std::string& space_id,
  const std::string& user_id,
  const std::string& origin) {
  if (db.is_space_member(space_id, user_id)) return true;
  auto user = db.find_user_by_id(user_id);
  if (user && (user->role == "admin" || user->role == "owner")) return true;

  // Allow access to personal spaces if user has per-resource permissions
  auto space = db.find_space_by_id(space_id);
  if (space && space->is_personal) {
    if (db.has_resource_permission_in_space(space_id, user_id, "wiki")) return true;
  }

  loop_->defer([res, aborted, origin]() {
    if (*aborted) return;
    cors::apply(res, origin);
    res->writeStatus("403")
      ->writeHeader("Content-Type", "application/json")
      ->end(R"({"error":"Not a member of this space"})");
  });
  return false;
}

template <bool SSL>
std::string WikiHandler<SSL>::get_access_level(
  const std::string& space_id, const std::string& user_id) {
  auto user = db.find_user_by_id(user_id);
  if (user && (user->role == "admin" || user->role == "owner")) return "owner";

  auto space_role = db.get_space_member_role(space_id, user_id);
  if (space_role == "admin" || space_role == "owner") return "owner";

  // "user" role members default to "view"; tool-level permissions can escalate
  auto wiki_perm = db.get_wiki_permission(space_id, user_id);
  if (!wiki_perm.empty()) {
    return wiki_perm;
  }

  return "view";
}

template <bool SSL>
std::string WikiHandler<SSL>::get_page_access_level(
  const std::string& space_id, const std::string& page_id, const std::string& user_id) {
  std::string base = get_access_level(space_id, user_id);

  auto page_perm = db.get_effective_wiki_page_permission(page_id, user_id);
  if (!page_perm.empty() && perm_rank(page_perm) > perm_rank(base)) {
    return page_perm;
  }

  return base;
}

template <bool SSL>
bool WikiHandler<SSL>::require_permission(
  uWS::HttpResponse<SSL>* res,
  std::shared_ptr<bool> aborted,
  const std::string& space_id,
  const std::string& user_id,
  const std::string& required_level,
  const std::string& origin) {
  auto level = get_access_level(space_id, user_id);
  if (perm_rank(level) >= perm_rank(required_level)) return true;

  auto err = json({{"error", "Requires " + required_level + " permission"}}).dump();
  loop_->defer([res, aborted, err = std::move(err), origin]() {
    if (*aborted) return;
    cors::apply(res, origin);
    res->writeStatus("403")->writeHeader("Content-Type", "application/json")->end(err);
  });
  return false;
}

template <bool SSL>
bool WikiHandler<SSL>::require_page_permission(
  uWS::HttpResponse<SSL>* res,
  std::shared_ptr<bool> aborted,
  const std::string& space_id,
  const std::string& page_id,
  const std::string& user_id,
  const std::string& required_level,
  const std::string& origin) {
  auto level = get_page_access_level(space_id, page_id, user_id);
  if (perm_rank(level) >= perm_rank(required_level)) return true;

  auto err = json({{"error", "Requires " + required_level + " permission"}}).dump();
  loop_->defer([res, aborted, err = std::move(err), origin]() {
    if (*aborted) return;
    cors::apply(res, origin);
    res->writeStatus("403")->writeHeader("Content-Type", "application/json")->end(err);
  });
  return false;
}

template <bool SSL>
int WikiHandler<SSL>::perm_rank(const std::string& p) {
  if (p == "owner") return 2;
  if (p == "edit") return 1;
  return 0;
}

template struct WikiHandler<false>;
template struct WikiHandler<true>;
