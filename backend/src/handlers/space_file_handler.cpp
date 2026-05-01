#include "handlers/space_file_handler.h"
#include <filesystem>
#include <fstream>
#include <pqxx/pqxx>
#include "handlers/cors_utils.h"
#include "handlers/file_access_utils.h"
#include "handlers/format_utils.h"
#include "handlers/request_scope.h"
#include "zip_builder.h"

using json = nlohmann::json;

static json space_file_to_json(const SpaceFile& f, const std::string& username = "") {
  return {
    {"id", f.id},
    {"space_id", f.space_id},
    {"parent_id", f.parent_id.empty() ? json(nullptr) : json(f.parent_id)},
    {"name", f.name},
    {"is_folder", f.is_folder},
    {"file_size", f.file_size},
    {"mime_type", f.mime_type},
    {"created_by", f.created_by},
    {"created_by_username", username.empty() ? f.created_by_username : username},
    {"created_at", f.created_at},
    {"updated_at", f.updated_at}};
}

static json space_file_version_to_json(const SpaceFileVersion& v) {
  return {
    {"id", v.id},
    {"file_id", v.file_id},
    {"version_number", v.version_number},
    {"file_size", v.file_size},
    {"mime_type", v.mime_type},
    {"uploaded_by", v.uploaded_by},
    {"uploaded_by_username", v.uploaded_by_username},
    {"created_at", v.created_at}};
}

template <bool SSL>
void SpaceFileHandler<SSL>::register_routes(uWS::TemplatedApp<SSL>& app) {
  // List files in a folder (root if no parent_id)
  app.get("/api/spaces/:id/files", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("GET", "/api/spaces/:id/files");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    std::string token = extract_session_token(req);
    std::string space_id(req->getParameter("id"));
    std::string parent_id(req->getQuery("parent_id"));

    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });

    pool_.submit([this, res, aborted, scope, token, space_id, parent_id, origin]() {
      if (*aborted) return;

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
      std::string user_id = *user_id_opt;

      // check_space_access inline
      if (!check_space_access_sync(space_id, user_id)) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("403")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Not a member of this space"})");
          scope->observe(403);
        });
        return;
      }

      auto files = db.list_space_files(space_id, parent_id);

      json arr = json::array();
      for (const auto& f : files) {
        arr.push_back(space_file_to_json(f));
      }

      // Include breadcrumb path if inside a subfolder
      json path_arr = json::array();
      if (!parent_id.empty()) {
        auto path = db.get_space_file_path(parent_id);
        for (const auto& p : path) {
          path_arr.push_back({{"id", p.id}, {"name", p.name}});
        }
      }

      // Include caller's effective permission on this folder
      std::string my_perm = get_access_level(space_id, parent_id, user_id);

      json resp = {{"files", arr}, {"path", path_arr}, {"my_permission", my_perm}};
      std::string resp_str = resp.dump();

      loop_->defer([res, aborted, scope, resp_str = std::move(resp_str), origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(resp_str);
        scope->observe(200);
      });
    });
  });

  // Create folder
  app.post("/api/spaces/:id/files/folder", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("POST", "/api/spaces/:id/files/folder");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    std::string token = extract_session_token(req);
    std::string space_id(req->getParameter("id"));

    auto body = std::make_shared<std::string>();
    auto aborted = std::make_shared<bool>(false);

    res->onAborted([aborted, origin]() { *aborted = true; });

    res->onData([this, res, body, aborted, scope, token, space_id, origin](
                  std::string_view data, bool last) {
      body->append(data);
      if (!last) return;

      pool_.submit([this, res, body, aborted, scope, token, space_id, origin]() {
        if (*aborted) return;

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
        std::string user_id = *user_id_opt;

        if (!check_space_access_sync(space_id, user_id)) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("403")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Not a member of this space"})");
            scope->observe(403);
          });
          return;
        }

        try {
          auto j = json::parse(*body);
          std::string name = j.at("name");
          std::string parent_id = j.value("parent_id", "");

          if (name.empty() || name.length() > 255) {
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Invalid folder name"})");
              scope->observe(400);
            });
            return;
          }

          // Permission check: need "edit" on parent
          if (!require_permission_sync(space_id, parent_id, user_id, "edit")) {
            auto err = json({{"error", "Requires edit permission"}}).dump();
            loop_->defer([res, aborted, scope, err = std::move(err), origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("403")->writeHeader("Content-Type", "application/json")->end(err);
              scope->observe(403);
            });
            return;
          }

          if (db.space_file_name_exists(space_id, parent_id, name)) {
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("409")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"A file or folder with this name already exists"})");
              scope->observe(409);
            });
            return;
          }

          if (!parent_id.empty()) {
            auto parent = db.find_space_file(parent_id);
            if (
              !parent || parent->space_id != space_id || !parent->is_folder || parent->is_deleted) {
              loop_->defer([res, aborted, scope, origin]() {
                if (*aborted) return;
                cors::apply(res, origin);
                res->writeStatus("400")
                  ->writeHeader("Content-Type", "application/json")
                  ->end(R"({"error":"Invalid parent folder"})");
                scope->observe(400);
              });
              return;
            }
          }

          auto folder = db.create_space_folder(space_id, parent_id, name, user_id);
          auto creator = db.find_user_by_id(user_id);
          json resp = space_file_to_json(folder, creator ? creator->username : "");
          std::string resp_str = resp.dump();

          loop_->defer([res, aborted, scope, resp_str = std::move(resp_str), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(resp_str);
            scope->observe(200);
          });
        } catch (const pqxx::unique_violation&) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("409")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"A file or folder with this name already exists"})");
            scope->observe(409);
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

  // Upload file
  app.post("/api/spaces/:id/files/upload", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("POST", "/api/spaces/:id/files/upload");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    std::string token = extract_session_token(req);
    std::string space_id(req->getParameter("id"));
    std::string parent_id(req->getQuery("parent_id"));
    std::string filename(req->getQuery("filename"));
    std::string content_type(req->getQuery("content_type"));

    if (filename.empty()) filename = "upload";
    if (content_type.empty()) content_type = "application/octet-stream";

    auto body = std::make_shared<std::string>();
    auto aborted = std::make_shared<bool>(false);

    res->onAborted([aborted, origin]() { *aborted = true; });

    res->onData([this,
                 res,
                 body,
                 aborted,
                 scope,
                 token,
                 space_id,
                 parent_id,
                 filename,
                 content_type,
                 origin](std::string_view data, bool last) mutable {
      body->append(data);
      if (!last) return;

      pool_.submit([this,
                    res,
                    body,
                    aborted,
                    scope,
                    token,
                    space_id,
                    parent_id,
                    filename,
                    content_type,
                    origin]() {
        if (*aborted) return;

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
        std::string user_id = *user_id_opt;

        int64_t max_size = file_access_utils::parse_max_file_size(
          db.get_setting("max_file_size"), config.max_file_size);

        if (file_access_utils::exceeds_file_size_limit(
              max_size, static_cast<int64_t>(body->size()))) {
          std::string msg = file_access_utils::file_too_large_message(max_size);
          auto err_json = json{{"error", msg}}.dump();
          loop_->defer([res, aborted, scope, err_json = std::move(err_json), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("413")->writeHeader("Content-Type", "application/json")->end(err_json);
            scope->observe(413);
          });
          return;
        }

        if (!check_space_access_sync(space_id, user_id)) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("403")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Not a member of this space"})");
            scope->observe(403);
          });
          return;
        }

        // Permission check: need "edit" on parent
        if (!require_permission_sync(space_id, parent_id, user_id, "edit")) {
          auto err = json({{"error", "Requires edit permission"}}).dump();
          loop_->defer([res, aborted, scope, err = std::move(err), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("403")->writeHeader("Content-Type", "application/json")->end(err);
            scope->observe(403);
          });
          return;
        }

        if (!parent_id.empty()) {
          auto parent = db.find_space_file(parent_id);
          if (!parent || parent->space_id != space_id || !parent->is_folder || parent->is_deleted) {
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Invalid parent folder"})");
              scope->observe(400);
            });
            return;
          }
        }

        if (db.space_file_name_exists(space_id, parent_id, filename)) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("409")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"A file or folder with this name already exists"})");
            scope->observe(409);
          });
          return;
        }

        try {
          // Check server storage limit
          int64_t max_storage =
            file_access_utils::parse_max_storage_size(db.get_setting("max_storage_size"));
          if (
            max_storage > 0 &&
            file_access_utils::exceeds_storage_limit(
              max_storage, db.get_total_file_size(), static_cast<int64_t>(body->size()))) {
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

          // Check space storage limit
          int64_t space_limit = file_access_utils::parse_space_storage_limit(
            db.get_setting("space_storage_limit_" + space_id));
          if (
            space_limit > 0 && file_access_utils::exceeds_storage_limit(
                                 space_limit,
                                 db.get_space_storage_used(space_id),
                                 static_cast<int64_t>(body->size()))) {
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

          // Check aggregate personal spaces storage limit
          std::string personal_err =
            check_personal_total_limit_sync(space_id, static_cast<int64_t>(body->size()));
          if (!personal_err.empty()) {
            loop_->defer([res, aborted, scope, personal_err = std::move(personal_err), origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("413")
                ->writeHeader("Content-Type", "application/json")
                ->end(personal_err);
              scope->observe(413);
            });
            return;
          }

          std::string disk_file_id = format_utils::random_hex(32);
          std::string path = config.upload_dir + "/" + disk_file_id;
          std::ofstream out(path, std::ios::binary);
          if (!out) {
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("500")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Failed to save file"})");
              scope->observe(500);
            });
            return;
          }
          out.write(body->data(), body->size());
          out.close();

          int64_t file_size = static_cast<int64_t>(body->size());

          auto file = db.create_space_file(
            space_id, parent_id, filename, disk_file_id, file_size, content_type, user_id);
          auto creator = db.find_user_by_id(user_id);
          json resp = space_file_to_json(file, creator ? creator->username : "");
          std::string resp_str = resp.dump();

          loop_->defer([res, aborted, scope, resp_str = std::move(resp_str), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(resp_str);
            scope->observe(200);
          });
        } catch (const pqxx::unique_violation&) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("409")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"A file or folder with this name already exists"})");
            scope->observe(409);
          });
        } catch (const std::exception& e) {
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

  // --- Chunked upload: init ---
  app.post("/api/spaces/:id/files/upload/init", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("POST", "/api/spaces/:id/files/upload/init");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    std::string token = extract_session_token(req);
    std::string space_id(req->getParameter("id"));

    auto body = std::make_shared<std::string>();
    auto aborted = std::make_shared<bool>(false);

    res->onAborted([aborted, origin]() { *aborted = true; });

    res->onData([this, res, body, aborted, scope, token, space_id, origin](
                  std::string_view data, bool last) {
      body->append(data);
      if (!last) return;

      pool_.submit([this, res, body, aborted, scope, token, space_id, origin]() {
        if (*aborted) return;

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
        std::string user_id = *user_id_opt;

        try {
          auto j = json::parse(*body);
          std::string filename = j.value("filename", "upload");
          std::string content_type = j.value("content_type", "application/octet-stream");
          int64_t total_size = j.value("total_size", int64_t(0));
          int chunk_count = j.value("chunk_count", 0);
          int64_t chunk_size = j.value("chunk_size", int64_t(0));
          std::string parent_id = j.value("parent_id", "");

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

          if (!check_space_access_sync(space_id, user_id)) {
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("403")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Not a member of this space"})");
              scope->observe(403);
            });
            return;
          }

          if (!require_permission_sync(space_id, parent_id, user_id, "edit")) {
            auto err = json({{"error", "Requires edit permission"}}).dump();
            loop_->defer([res, aborted, scope, err = std::move(err), origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("403")->writeHeader("Content-Type", "application/json")->end(err);
              scope->observe(403);
            });
            return;
          }

          if (!parent_id.empty()) {
            auto parent = db.find_space_file(parent_id);
            if (
              !parent || parent->space_id != space_id || !parent->is_folder || parent->is_deleted) {
              loop_->defer([res, aborted, scope, origin]() {
                if (*aborted) return;
                cors::apply(res, origin);
                res->writeStatus("400")
                  ->writeHeader("Content-Type", "application/json")
                  ->end(R"({"error":"Invalid parent folder"})");
                scope->observe(400);
              });
              return;
            }
          }

          if (db.space_file_name_exists(space_id, parent_id, filename)) {
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("409")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"A file or folder with this name already exists"})");
              scope->observe(409);
            });
            return;
          }

          int64_t max_size = file_access_utils::parse_max_file_size(
            db.get_setting("max_file_size"), config.max_file_size);
          if (file_access_utils::exceeds_file_size_limit(max_size, total_size)) {
            std::string msg = file_access_utils::file_too_large_message(max_size);
            auto err_json = json{{"error", msg}}.dump();
            loop_->defer([res, aborted, scope, err_json = std::move(err_json), origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("413")
                ->writeHeader("Content-Type", "application/json")
                ->end(err_json);
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

          // Check aggregate personal spaces storage limit
          std::string personal_err = check_personal_total_limit_sync(space_id, total_size);
          if (!personal_err.empty()) {
            loop_->defer([res, aborted, scope, personal_err = std::move(personal_err), origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("413")
                ->writeHeader("Content-Type", "application/json")
                ->end(personal_err);
              scope->observe(413);
            });
            return;
          }

          json metadata = {
            {"filename", filename},
            {"content_type", content_type},
            {"space_id", space_id},
            {"parent_id", parent_id}};
          std::string upload_id =
            uploads.create_session(user_id, total_size, chunk_count, chunk_size, metadata);

          auto resp_str = json{{"upload_id", upload_id}}.dump();
          loop_->defer([res, aborted, scope, resp_str = std::move(resp_str), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(resp_str);
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
  app.post("/api/spaces/:id/files/upload/:uploadId/chunk", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>(
      "POST", "/api/spaces/:id/files/upload/:uploadId/chunk");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    std::string token = extract_session_token(req);
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

    res->onData([this, res, body, aborted, scope, token, upload_id, index, expected_hash, origin](
                  std::string_view data, bool last) {
      body->append(data);
      if (!last) return;

      pool_.submit(
        [this, res, body, aborted, scope, token, upload_id, index, expected_hash, origin]() {
          if (*aborted) return;

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
          std::string user_id = *user_id_opt;

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
            std::string status;
            std::string msg;
            if (err == "hash_mismatch") {
              status = "409";
              msg = R"({"error":"Chunk integrity check failed"})";
            } else if (err == "invalid_index") {
              status = "400";
              msg = R"({"error":"Invalid chunk index"})";
            } else {
              status = "500";
              msg = R"({"error":"Failed to store chunk"})";
            }
            loop_->defer(
              [res, aborted, scope, status = std::move(status), msg = std::move(msg), origin]() {
                if (*aborted) return;
                cors::apply(res, origin);
                res->writeStatus(status)->writeHeader("Content-Type", "application/json")->end(msg);
                scope->observe(200);
              });
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
  app.post("/api/spaces/:id/files/upload/:uploadId/complete", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>(
      "POST", "/api/spaces/:id/files/upload/:uploadId/complete");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    std::string token = extract_session_token(req);
    std::string space_id(req->getParameter(0));
    std::string upload_id(req->getParameter(1));

    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });

    res->onData(
      [this, res, aborted, scope, space_id, upload_id, token, origin](std::string_view, bool last) {
        if (!last) return;

        pool_.submit([this, res, aborted, scope, space_id, upload_id, token, origin]() {
          if (*aborted) return;

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
          std::string user_id = *user_id_opt;

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

          if (!check_space_access_sync(space_id, user_id)) {
            uploads.remove_session(upload_id);
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("403")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Not a member of this space"})");
              scope->observe(403);
            });
            return;
          }

          std::string parent_id = session->metadata.value("parent_id", "");
          if (!require_permission_sync(space_id, parent_id, user_id, "edit")) {
            uploads.remove_session(upload_id);
            auto err = json({{"error", "Requires edit permission"}}).dump();
            loop_->defer([res, aborted, scope, err = std::move(err), origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("403")->writeHeader("Content-Type", "application/json")->end(err);
              scope->observe(403);
            });
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

            auto file = db.create_space_file(
              space_id, parent_id, filename, disk_file_id, assembled_size, content_type, user_id);
            auto creator = db.find_user_by_id(user_id);
            json resp = space_file_to_json(file, creator ? creator->username : "");
            std::string resp_str = resp.dump();
            uploads.remove_session(upload_id);

            loop_->defer([res, aborted, scope, resp_str = std::move(resp_str), origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeHeader("Content-Type", "application/json")->end(resp_str);
              scope->observe(200);
            });
          } catch (const pqxx::unique_violation&) {
            uploads.remove_session(upload_id);
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("409")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"A file or folder with this name already exists"})");
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

  // --- Chunked version upload: init ---
  app.post("/api/spaces/:spaceId/files/:fileId/versions/init", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>(
      "POST", "/api/spaces/:spaceId/files/:fileId/versions/init");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    std::string token = extract_session_token(req);
    std::string space_id(req->getParameter(0));
    std::string file_id(req->getParameter(1));

    auto body = std::make_shared<std::string>();
    auto aborted = std::make_shared<bool>(false);

    res->onAborted([aborted, origin]() { *aborted = true; });

    res->onData([this, res, body, aborted, scope, token, space_id, file_id, origin](
                  std::string_view data, bool last) {
      body->append(data);
      if (!last) return;

      pool_.submit([this, res, body, aborted, scope, token, space_id, file_id, origin]() {
        if (*aborted) return;

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
        std::string user_id = *user_id_opt;

        try {
          auto j = json::parse(*body);
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

          if (!check_space_access_sync(space_id, user_id)) {
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("403")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Not a member of this space"})");
              scope->observe(403);
            });
            return;
          }

          if (!require_permission_sync(space_id, file_id, user_id, "edit")) {
            auto err = json({{"error", "Requires edit permission"}}).dump();
            loop_->defer([res, aborted, scope, err = std::move(err), origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("403")->writeHeader("Content-Type", "application/json")->end(err);
              scope->observe(403);
            });
            return;
          }

          auto file = db.find_space_file(file_id);
          if (!file || file->space_id != space_id || file->is_deleted || file->is_folder) {
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("404")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"File not found"})");
              scope->observe(404);
            });
            return;
          }

          int64_t max_size = file_access_utils::parse_max_file_size(
            db.get_setting("max_file_size"), config.max_file_size);
          if (file_access_utils::exceeds_file_size_limit(max_size, total_size)) {
            std::string msg = file_access_utils::file_too_large_message(max_size);
            auto err_json = json{{"error", msg}}.dump();
            loop_->defer([res, aborted, scope, err_json = std::move(err_json), origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("413")
                ->writeHeader("Content-Type", "application/json")
                ->end(err_json);
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

          // Check aggregate personal spaces storage limit
          std::string personal_err = check_personal_total_limit_sync(space_id, total_size);
          if (!personal_err.empty()) {
            loop_->defer([res, aborted, scope, personal_err = std::move(personal_err), origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("413")
                ->writeHeader("Content-Type", "application/json")
                ->end(personal_err);
              scope->observe(413);
            });
            return;
          }

          json metadata = {
            {"space_id", space_id}, {"file_id", file_id}, {"mime_type", file->mime_type}};
          std::string upload_id =
            uploads.create_session(user_id, total_size, chunk_count, chunk_size, metadata);

          auto resp_str = json{{"upload_id", upload_id}}.dump();
          loop_->defer([res, aborted, scope, resp_str = std::move(resp_str), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(resp_str);
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

  // --- Chunked version upload: receive chunk ---
  app.post(
    "/api/spaces/:spaceId/files/:fileId/versions/:uploadId/chunk", [this](auto* res, auto* req) {
      auto scope = std::make_shared<handler_utils::RequestScope>(
        "POST", "/api/spaces/:spaceId/files/:fileId/versions/:uploadId/chunk");
      handler_utils::set_request_id_header(res, *scope);
      std::string origin(req->getHeader("origin"));
      std::string token = extract_session_token(req);
      std::string upload_id(req->getParameter(2));
      std::string index_str(req->getQuery("index"));
      std::string expected_hash(req->getQuery("hash"));

      int index = -1;
      try {
        index = std::stoi(index_str);
      } catch (...) {}

      auto body = std::make_shared<std::string>();
      auto aborted = std::make_shared<bool>(false);

      res->onAborted([aborted, origin]() { *aborted = true; });

      res->onData([this, res, body, aborted, scope, token, upload_id, index, expected_hash, origin](
                    std::string_view data, bool last) {
        body->append(data);
        if (!last) return;

        pool_.submit(
          [this, res, body, aborted, scope, token, upload_id, index, expected_hash, origin]() {
            if (*aborted) return;

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
            std::string user_id = *user_id_opt;

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
              std::string status;
              std::string msg;
              if (err == "hash_mismatch") {
                status = "409";
                msg = R"({"error":"Chunk integrity check failed"})";
              } else if (err == "invalid_index") {
                status = "400";
                msg = R"({"error":"Invalid chunk index"})";
              } else {
                status = "500";
                msg = R"({"error":"Failed to store chunk"})";
              }
              loop_->defer(
                [res, aborted, scope, status = std::move(status), msg = std::move(msg), origin]() {
                  if (*aborted) return;
                  cors::apply(res, origin);
                  res->writeStatus(status)
                    ->writeHeader("Content-Type", "application/json")
                    ->end(msg);
                  scope->observe(200);
                });
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

  // --- Chunked version upload: complete ---
  app.post(
    "/api/spaces/:spaceId/files/:fileId/versions/:uploadId/complete", [this](auto* res, auto* req) {
      auto scope = std::make_shared<handler_utils::RequestScope>(
        "POST", "/api/spaces/:spaceId/files/:fileId/versions/:uploadId/complete");
      handler_utils::set_request_id_header(res, *scope);
      std::string origin(req->getHeader("origin"));
      std::string token = extract_session_token(req);
      std::string space_id(req->getParameter(0));
      std::string file_id(req->getParameter(1));
      std::string upload_id(req->getParameter(2));

      auto aborted = std::make_shared<bool>(false);
      res->onAborted([aborted, origin]() { *aborted = true; });

      res->onData([this, res, aborted, scope, space_id, file_id, upload_id, token, origin](
                    std::string_view, bool last) {
        if (!last) return;

        pool_.submit([this, res, aborted, scope, space_id, file_id, upload_id, token, origin]() {
          if (*aborted) return;

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
          std::string user_id = *user_id_opt;

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

          if (!check_space_access_sync(space_id, user_id)) {
            uploads.remove_session(upload_id);
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("403")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Not a member of this space"})");
              scope->observe(403);
            });
            return;
          }

          if (!require_permission_sync(space_id, file_id, user_id, "edit")) {
            uploads.remove_session(upload_id);
            auto err = json({{"error", "Requires edit permission"}}).dump();
            loop_->defer([res, aborted, scope, err = std::move(err), origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("403")->writeHeader("Content-Type", "application/json")->end(err);
              scope->observe(403);
            });
            return;
          }

          auto file = db.find_space_file(file_id);
          if (!file || file->space_id != space_id || file->is_deleted || file->is_folder) {
            uploads.remove_session(upload_id);
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("404")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"File not found"})");
              scope->observe(404);
            });
            return;
          }

          try {
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

            std::string mime_type = session->metadata.value("mime_type", "");
            auto version =
              db.create_file_version(file_id, disk_file_id, assembled_size, mime_type, user_id);
            json resp = space_file_version_to_json(version);
            std::string resp_str = resp.dump();
            uploads.remove_session(upload_id);

            loop_->defer([res, aborted, scope, resp_str = std::move(resp_str), origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeHeader("Content-Type", "application/json")->end(resp_str);
              scope->observe(200);
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

  // Get file/folder details
  app.get("/api/spaces/:spaceId/files/:fileId", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("GET", "/api/spaces/:spaceId/files/:fileId");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    std::string token = extract_session_token(req);
    std::string space_id(req->getParameter(0));
    std::string file_id(req->getParameter(1));

    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });

    pool_.submit([this, res, aborted, scope, token, space_id, file_id, origin]() {
      if (*aborted) return;

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
      std::string user_id = *user_id_opt;

      if (!check_space_access_sync(space_id, user_id)) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("403")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Not a member of this space"})");
          scope->observe(403);
        });
        return;
      }

      auto file = db.find_space_file(file_id);
      if (!file || file->space_id != space_id || file->is_deleted) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("404")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"File not found"})");
          scope->observe(404);
        });
        return;
      }

      std::string my_perm = get_access_level(space_id, file_id, user_id);
      json resp = space_file_to_json(*file);
      resp["my_permission"] = my_perm;

      auto path = db.get_space_file_path(file_id);
      json path_arr = json::array();
      for (const auto& p : path) {
        path_arr.push_back({{"id", p.id}, {"name", p.name}});
      }
      resp["path"] = path_arr;

      std::string resp_str = resp.dump();

      loop_->defer([res, aborted, scope, resp_str = std::move(resp_str), origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(resp_str);
        scope->observe(200);
      });
    });
  });

  // Download file
  // Auth via header or query param (needed for <img>/<iframe> tags)
  app.get("/api/spaces/:spaceId/files/:fileId/download", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>(
      "GET", "/api/spaces/:spaceId/files/:fileId/download");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    std::string token = extract_session_token(req);
    if (token.empty()) token = std::string(req->getQuery("token"));
    std::string space_id(req->getParameter(0));
    std::string file_id(req->getParameter(1));
    std::string inline_query(req->getQuery("inline"));

    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });

    pool_.submit([this, res, aborted, scope, token, space_id, file_id, inline_query, origin]() {
      if (*aborted) return;

      auto user_id_opt = db.validate_session(token);
      if (!user_id_opt) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Unauthorized"})");
          scope->observe(401);
        });
        return;
      }
      std::string user_id = *user_id_opt;

      if (!check_space_access_sync(space_id, user_id)) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("403")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Not a member of this space"})");
          scope->observe(403);
        });
        return;
      }

      auto file = db.find_space_file(file_id);
      if (!file || file->space_id != space_id || file->is_deleted || file->is_folder) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("404")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"File not found"})");
          scope->observe(404);
        });
        return;
      }

      std::string path = config.upload_dir + "/" + file->disk_file_id;
      std::ifstream in(path, std::ios::binary);
      if (!in) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("404")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"File data not found on disk"})");
          scope->observe(404);
        });
        return;
      }

      auto data = std::make_shared<std::string>(
        (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

      std::string content_type =
        file->mime_type.empty() ? "application/octet-stream" : file->mime_type;
      std::string disposition = inline_query.empty()
                                  ? file_access_utils::attachment_disposition(file->name)
                                  : file_access_utils::inline_disposition(file->name);

      loop_->defer([res,
                    aborted,
                    scope,
                    data,
                    content_type = std::move(content_type),
                    disposition = std::move(disposition),
                    origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", content_type)
          ->writeHeader("Content-Disposition", disposition)
          ->end(*data);
        scope->observe(200);
      });
    });
  });

  // Update file/folder (rename, move)
  app.put("/api/spaces/:spaceId/files/:fileId", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("PUT", "/api/spaces/:spaceId/files/:fileId");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    std::string token = extract_session_token(req);
    std::string space_id(req->getParameter(0));
    std::string file_id(req->getParameter(1));

    auto body = std::make_shared<std::string>();
    auto aborted = std::make_shared<bool>(false);

    res->onAborted([aborted, origin]() { *aborted = true; });

    res->onData([this, res, body, aborted, scope, token, space_id, file_id, origin](
                  std::string_view data, bool last) mutable {
      body->append(data);
      if (!last) return;

      pool_.submit([this, res, body, aborted, scope, token, space_id, file_id, origin]() {
        if (*aborted) return;

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
        std::string user_id = *user_id_opt;

        if (!check_space_access_sync(space_id, user_id)) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("403")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Not a member of this space"})");
            scope->observe(403);
          });
          return;
        }

        // Permission check: need "edit" on the file
        if (!require_permission_sync(space_id, file_id, user_id, "edit")) {
          auto err = json({{"error", "Requires edit permission"}}).dump();
          loop_->defer([res, aborted, scope, err = std::move(err), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("403")->writeHeader("Content-Type", "application/json")->end(err);
            scope->observe(403);
          });
          return;
        }

        auto file = db.find_space_file(file_id);
        if (!file || file->space_id != space_id || file->is_deleted) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("404")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"File not found"})");
            scope->observe(404);
          });
          return;
        }

        try {
          auto j = json::parse(*body);

          if (j.contains("name")) {
            std::string new_name = j["name"];
            if (new_name.empty() || new_name.length() > 255) {
              loop_->defer([res, aborted, scope, origin]() {
                if (*aborted) return;
                cors::apply(res, origin);
                res->writeStatus("400")
                  ->writeHeader("Content-Type", "application/json")
                  ->end(R"({"error":"Invalid name"})");
                scope->observe(400);
              });
              return;
            }
            if (db.space_file_name_exists(space_id, file->parent_id, new_name, file_id)) {
              loop_->defer([res, aborted, scope, origin]() {
                if (*aborted) return;
                cors::apply(res, origin);
                res->writeStatus("409")
                  ->writeHeader("Content-Type", "application/json")
                  ->end(R"({"error":"A file or folder with this name already exists"})");
                scope->observe(409);
              });
              return;
            }
            db.rename_space_file(file_id, new_name);
          }

          if (j.contains("parent_id")) {
            std::string new_parent =
              j["parent_id"].is_null() ? "" : j["parent_id"].get<std::string>();
            if (!new_parent.empty()) {
              if (new_parent == file_id) {
                loop_->defer([res, aborted, scope, origin]() {
                  if (*aborted) return;
                  cors::apply(res, origin);
                  res->writeStatus("400")
                    ->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Cannot move a folder into itself"})");
                  scope->observe(400);
                });
                return;
              }
              auto parent = db.find_space_file(new_parent);
              if (
                !parent || parent->space_id != space_id || !parent->is_folder ||
                parent->is_deleted) {
                loop_->defer([res, aborted, scope, origin]() {
                  if (*aborted) return;
                  cors::apply(res, origin);
                  res->writeStatus("400")
                    ->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Invalid parent folder"})");
                  scope->observe(400);
                });
                return;
              }
              if (file->is_folder) {
                auto path = db.get_space_file_path(new_parent);
                for (const auto& p : path) {
                  if (p.id == file_id) {
                    loop_->defer([res, aborted, scope, origin]() {
                      if (*aborted) return;
                      cors::apply(res, origin);
                      res->writeStatus("400")
                        ->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Cannot move a folder into its own descendant"})");
                      scope->observe(400);
                    });
                    return;
                  }
                }
              }
            }
            std::string current_name = j.value("name", file->name);
            if (db.space_file_name_exists(space_id, new_parent, current_name, file_id)) {
              loop_->defer([res, aborted, scope, origin]() {
                if (*aborted) return;
                cors::apply(res, origin);
                res->writeStatus("409")
                  ->writeHeader("Content-Type", "application/json")
                  ->end(
                    R"({"error":"A file or folder with this name already exists in the destination"})");
                scope->observe(409);
              });
              return;
            }
            db.move_space_file(file_id, new_parent);
          }

          auto updated = db.find_space_file(file_id);
          json resp = space_file_to_json(*updated);
          std::string resp_str = resp.dump();

          loop_->defer([res, aborted, scope, resp_str = std::move(resp_str), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(resp_str);
            scope->observe(200);
          });
        } catch (const pqxx::unique_violation&) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("409")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"A file or folder with this name already exists"})");
            scope->observe(409);
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

  // Delete file/folder -- requires "owner" permission
  app.del("/api/spaces/:spaceId/files/:fileId", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("DEL", "/api/spaces/:spaceId/files/:fileId");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    std::string token = extract_session_token(req);
    std::string space_id(req->getParameter(0));
    std::string file_id(req->getParameter(1));

    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });

    pool_.submit([this, res, aborted, scope, token, space_id, file_id, origin]() {
      if (*aborted) return;

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
      std::string user_id = *user_id_opt;

      if (!check_space_access_sync(space_id, user_id)) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("403")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Not a member of this space"})");
          scope->observe(403);
        });
        return;
      }

      if (!require_permission_sync(space_id, file_id, user_id, "owner")) {
        auto err = json({{"error", "Requires owner permission"}}).dump();
        loop_->defer([res, aborted, scope, err = std::move(err), origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("403")->writeHeader("Content-Type", "application/json")->end(err);
          scope->observe(403);
        });
        return;
      }

      auto file = db.find_space_file(file_id);
      if (!file || file->space_id != space_id || file->is_deleted) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("404")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"File not found"})");
          scope->observe(404);
        });
        return;
      }

      // Hard delete: remove DB records and collect disk file IDs
      auto disk_ids = db.hard_delete_space_file(file_id);

      // Remove files from disk
      for (const auto& did : disk_ids) {
        std::string path = config.upload_dir + "/" + did;
        std::filesystem::remove(path);
      }

      loop_->defer([res, aborted, scope, origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
        scope->observe(200);
      });
    });
  });

  // Get space storage usage
  app.get("/api/spaces/:id/storage", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("GET", "/api/spaces/:id/storage");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    std::string token = extract_session_token(req);
    std::string space_id(req->getParameter("id"));

    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });

    pool_.submit([this, res, aborted, scope, token, space_id, origin]() {
      if (*aborted) return;

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
      std::string user_id = *user_id_opt;

      if (!check_space_access_sync(space_id, user_id)) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("403")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Not a member of this space"})");
          scope->observe(403);
        });
        return;
      }

      int64_t used = db.get_space_storage_used(space_id);
      int64_t limit = 0;
      auto limit_str = db.get_setting("space_storage_limit_" + space_id);
      if (limit_str) {
        try {
          limit = std::stoll(*limit_str);
        } catch (...) {}
      }

      auto breakdown = db.get_space_storage_breakdown(space_id);
      json breakdown_arr = json::array();
      for (const auto& entry : breakdown) {
        if (entry.used > 0) {
          breakdown_arr.push_back(
            {{"name", entry.name}, {"type", entry.type}, {"used", entry.used}});
        }
      }

      json resp = {{"used", used}, {"limit", limit}, {"breakdown", breakdown_arr}};
      std::string resp_str = resp.dump();

      loop_->defer([res, aborted, scope, resp_str = std::move(resp_str), origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(resp_str);
        scope->observe(200);
      });
    });
  });

  // --- File Permissions endpoints ---

  // Get permissions for a file/folder
  app.get("/api/spaces/:spaceId/files/:fileId/permissions", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>(
      "GET", "/api/spaces/:spaceId/files/:fileId/permissions");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    std::string token = extract_session_token(req);
    std::string space_id(req->getParameter(0));
    std::string file_id(req->getParameter(1));

    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });

    pool_.submit([this, res, aborted, scope, token, space_id, file_id, origin]() {
      if (*aborted) return;

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
      std::string user_id = *user_id_opt;

      if (!check_space_access_sync(space_id, user_id)) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("403")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Not a member of this space"})");
          scope->observe(403);
        });
        return;
      }

      auto file = db.find_space_file(file_id);
      if (!file || file->space_id != space_id || file->is_deleted) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("404")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"File not found"})");
          scope->observe(404);
        });
        return;
      }

      auto perms = db.get_file_permissions(file_id);
      json arr = json::array();
      for (const auto& p : perms) {
        arr.push_back(
          {{"id", p.id},
           {"file_id", p.file_id},
           {"user_id", p.user_id},
           {"username", p.username},
           {"display_name", p.display_name},
           {"permission", p.permission},
           {"granted_by", p.granted_by},
           {"granted_by_username", p.granted_by_username},
           {"created_at", p.created_at}});
      }

      std::string my_perm = get_access_level(space_id, file_id, user_id);

      json resp = {{"permissions", arr}, {"my_permission", my_perm}};
      std::string resp_str = resp.dump();

      loop_->defer([res, aborted, scope, resp_str = std::move(resp_str), origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(resp_str);
        scope->observe(200);
      });
    });
  });

  // Set permission for a user on a file/folder -- requires "owner" on the file
  app.put("/api/spaces/:spaceId/files/:fileId/permissions", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>(
      "PUT", "/api/spaces/:spaceId/files/:fileId/permissions");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    std::string token = extract_session_token(req);
    std::string space_id(req->getParameter(0));
    std::string file_id(req->getParameter(1));

    auto body = std::make_shared<std::string>();
    auto aborted = std::make_shared<bool>(false);

    res->onAborted([aborted, origin]() { *aborted = true; });

    res->onData([this, res, body, aborted, scope, token, space_id, file_id, origin](
                  std::string_view data, bool last) mutable {
      body->append(data);
      if (!last) return;

      pool_.submit([this, res, body, aborted, scope, token, space_id, file_id, origin]() {
        if (*aborted) return;

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
        std::string user_id = *user_id_opt;

        if (!check_space_access_sync(space_id, user_id)) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("403")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Not a member of this space"})");
            scope->observe(403);
          });
          return;
        }

        if (!require_permission_sync(space_id, file_id, user_id, "owner")) {
          auto err = json({{"error", "Requires owner permission"}}).dump();
          loop_->defer([res, aborted, scope, err = std::move(err), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("403")->writeHeader("Content-Type", "application/json")->end(err);
            scope->observe(403);
          });
          return;
        }

        try {
          auto j = json::parse(*body);
          std::string target_user_id = j.at("user_id");
          std::string permission = j.at("permission");

          if (permission != "owner" && permission != "edit" && permission != "view") {
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(
                  R"json({"error":"Invalid permission level (must be owner, edit, or view)"})json");
              scope->observe(400);
            });
            return;
          }

          // Personal spaces: only view and edit allowed, not owner
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

          // Verify target user exists and is a space member
          auto target = db.find_user_by_id(target_user_id);
          if (!target) {
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"User not found"})");
              scope->observe(400);
            });
            return;
          }

          // Check monotonic escalation: new permission must be >= parent's effective permission
          auto file = db.find_space_file(file_id);
          if (file && !file->parent_id.empty()) {
            std::string parent_perm =
              db.get_effective_file_permission(file->parent_id, target_user_id);
            if (perm_rank(permission) < perm_rank(parent_perm)) {
              auto err =
                json(
                  {{"error", "Cannot set permission below inherited level (" + parent_perm + ")"}})
                  .dump();
              loop_->defer([res, aborted, scope, err = std::move(err), origin]() {
                if (*aborted) return;
                cors::apply(res, origin);
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
                scope->observe(400);
              });
              return;
            }
          }

          db.set_file_permission(file_id, target_user_id, permission, user_id);

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

  // Remove a specific user's permission on a file
  app.del("/api/spaces/:spaceId/files/:fileId/permissions/:userId", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>(
      "DEL", "/api/spaces/:spaceId/files/:fileId/permissions/:userId");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    std::string token = extract_session_token(req);
    std::string space_id(req->getParameter(0));
    std::string file_id(req->getParameter(1));
    std::string target_user_id(req->getParameter(2));

    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });

    pool_.submit([this, res, aborted, scope, token, space_id, file_id, target_user_id, origin]() {
      if (*aborted) return;

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
      std::string user_id = *user_id_opt;

      if (!check_space_access_sync(space_id, user_id)) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("403")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Not a member of this space"})");
          scope->observe(403);
        });
        return;
      }

      if (!require_permission_sync(space_id, file_id, user_id, "owner")) {
        auto err = json({{"error", "Requires owner permission"}}).dump();
        loop_->defer([res, aborted, scope, err = std::move(err), origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("403")->writeHeader("Content-Type", "application/json")->end(err);
          scope->observe(403);
        });
        return;
      }

      db.remove_file_permission(file_id, target_user_id);

      loop_->defer([res, aborted, scope, origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
        scope->observe(200);
      });
    });
  });

  // --- Version endpoints ---

  // List versions for a file
  app.get("/api/spaces/:spaceId/files/:fileId/versions", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>(
      "GET", "/api/spaces/:spaceId/files/:fileId/versions");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    std::string token = extract_session_token(req);
    std::string space_id(req->getParameter(0));
    std::string file_id(req->getParameter(1));

    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });

    pool_.submit([this, res, aborted, scope, token, space_id, file_id, origin]() {
      if (*aborted) return;

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
      std::string user_id = *user_id_opt;

      if (!check_space_access_sync(space_id, user_id)) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("403")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Not a member of this space"})");
          scope->observe(403);
        });
        return;
      }

      auto file = db.find_space_file(file_id);
      if (!file || file->space_id != space_id || file->is_deleted || file->is_folder) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("404")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"File not found"})");
          scope->observe(404);
        });
        return;
      }

      auto versions = db.list_file_versions(file_id);
      json arr = json::array();
      for (const auto& v : versions) {
        arr.push_back(space_file_version_to_json(v));
      }

      json resp = {{"versions", arr}};
      std::string resp_str = resp.dump();

      loop_->defer([res, aborted, scope, resp_str = std::move(resp_str), origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(resp_str);
        scope->observe(200);
      });
    });
  });

  // Upload new version of a file -- requires "edit" permission
  app.post("/api/spaces/:spaceId/files/:fileId/versions", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>(
      "POST", "/api/spaces/:spaceId/files/:fileId/versions");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    std::string token = extract_session_token(req);
    std::string space_id(req->getParameter(0));
    std::string file_id(req->getParameter(1));

    auto body = std::make_shared<std::string>();
    auto aborted = std::make_shared<bool>(false);

    res->onAborted([aborted, origin]() { *aborted = true; });

    res->onData([this, res, body, aborted, scope, token, space_id, file_id, origin](
                  std::string_view data, bool last) mutable {
      body->append(data);
      if (!last) return;

      pool_.submit([this, res, body, aborted, scope, token, space_id, file_id, origin]() {
        if (*aborted) return;

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
        std::string user_id = *user_id_opt;

        int64_t max_size = file_access_utils::parse_max_file_size(
          db.get_setting("max_file_size"), config.max_file_size);

        if (file_access_utils::exceeds_file_size_limit(
              max_size, static_cast<int64_t>(body->size()))) {
          std::string msg = file_access_utils::file_too_large_message(max_size);
          auto err_json = json{{"error", msg}}.dump();
          loop_->defer([res, aborted, scope, err_json = std::move(err_json), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("413")->writeHeader("Content-Type", "application/json")->end(err_json);
            scope->observe(413);
          });
          return;
        }

        if (!check_space_access_sync(space_id, user_id)) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("403")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Not a member of this space"})");
            scope->observe(403);
          });
          return;
        }

        if (!require_permission_sync(space_id, file_id, user_id, "edit")) {
          auto err = json({{"error", "Requires edit permission"}}).dump();
          loop_->defer([res, aborted, scope, err = std::move(err), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("403")->writeHeader("Content-Type", "application/json")->end(err);
            scope->observe(403);
          });
          return;
        }

        auto file = db.find_space_file(file_id);
        if (!file || file->space_id != space_id || file->is_deleted || file->is_folder) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("404")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"File not found"})");
            scope->observe(404);
          });
          return;
        }

        try {
          // Check server storage limit
          int64_t max_storage =
            file_access_utils::parse_max_storage_size(db.get_setting("max_storage_size"));
          if (
            max_storage > 0 &&
            file_access_utils::exceeds_storage_limit(
              max_storage, db.get_total_file_size(), static_cast<int64_t>(body->size()))) {
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

          // Check space storage limit
          int64_t space_limit = file_access_utils::parse_space_storage_limit(
            db.get_setting("space_storage_limit_" + space_id));
          if (
            space_limit > 0 && file_access_utils::exceeds_storage_limit(
                                 space_limit,
                                 db.get_space_storage_used(space_id),
                                 static_cast<int64_t>(body->size()))) {
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

          // Check aggregate personal spaces storage limit
          std::string personal_err =
            check_personal_total_limit_sync(space_id, static_cast<int64_t>(body->size()));
          if (!personal_err.empty()) {
            loop_->defer([res, aborted, scope, personal_err = std::move(personal_err), origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("413")
                ->writeHeader("Content-Type", "application/json")
                ->end(personal_err);
              scope->observe(413);
            });
            return;
          }

          std::string disk_file_id = format_utils::random_hex(32);
          std::string path = config.upload_dir + "/" + disk_file_id;
          std::ofstream out(path, std::ios::binary);
          if (!out) {
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("500")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Failed to save file"})");
              scope->observe(500);
            });
            return;
          }
          out.write(body->data(), body->size());
          out.close();

          int64_t file_size = static_cast<int64_t>(body->size());
          std::string mime_type = file->mime_type;

          auto version =
            db.create_file_version(file_id, disk_file_id, file_size, mime_type, user_id);
          json resp = space_file_version_to_json(version);
          std::string resp_str = resp.dump();

          loop_->defer([res, aborted, scope, resp_str = std::move(resp_str), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(resp_str);
            scope->observe(200);
          });
        } catch (const std::exception& e) {
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

  // Download a specific version
  // Auth via header or query param (needed for <img>/<iframe> tags)
  app.get(
    "/api/spaces/:spaceId/files/:fileId/versions/:versionId/download",
    [this](auto* res, auto* req) {
      auto scope = std::make_shared<handler_utils::RequestScope>(
        "GET", "/api/spaces/:spaceId/files/:fileId/versions/:versionId/download");
      handler_utils::set_request_id_header(res, *scope);
      std::string origin(req->getHeader("origin"));
      std::string token = extract_session_token(req);
      if (token.empty()) token = std::string(req->getQuery("token"));
      std::string space_id(req->getParameter(0));
      std::string file_id(req->getParameter(1));
      std::string version_id(req->getParameter(2));

      auto aborted = std::make_shared<bool>(false);
      res->onAborted([aborted, origin]() { *aborted = true; });

      pool_.submit([this, res, aborted, scope, token, space_id, file_id, version_id, origin]() {
        if (*aborted) return;

        auto user_id_opt = db.validate_session(token);
        if (!user_id_opt) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("401")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Unauthorized"})");
            scope->observe(401);
          });
          return;
        }
        std::string user_id = *user_id_opt;

        if (!check_space_access_sync(space_id, user_id)) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("403")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Not a member of this space"})");
            scope->observe(403);
          });
          return;
        }

        auto file = db.find_space_file(file_id);
        if (!file || file->space_id != space_id || file->is_deleted || file->is_folder) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("404")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"File not found"})");
            scope->observe(404);
          });
          return;
        }

        auto version = db.get_file_version(version_id);
        if (!version || version->file_id != file_id) {
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

        std::string path = config.upload_dir + "/" + version->disk_file_id;
        std::ifstream in(path, std::ios::binary);
        if (!in) {
          loop_->defer([res, aborted, scope, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("404")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Version data not found on disk"})");
            scope->observe(404);
          });
          return;
        }

        auto data = std::make_shared<std::string>(
          (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        std::string content_type =
          version->mime_type.empty() ? "application/octet-stream" : version->mime_type;
        std::string disposition =
          file_access_utils::versioned_attachment_disposition(version->version_number, file->name);

        loop_->defer([res,
                      aborted,
                      scope,
                      data,
                      content_type = std::move(content_type),
                      disposition = std::move(disposition),
                      origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeHeader("Content-Type", content_type)
            ->writeHeader("Content-Disposition", disposition)
            ->end(*data);
          scope->observe(200);
        });
      });
    });

  // Revert to a specific version -- requires "edit" permission
  app.post(
    "/api/spaces/:spaceId/files/:fileId/versions/:versionId/revert", [this](auto* res, auto* req) {
      auto scope = std::make_shared<handler_utils::RequestScope>(
        "POST", "/api/spaces/:spaceId/files/:fileId/versions/:versionId/revert");
      handler_utils::set_request_id_header(res, *scope);
      std::string origin(req->getHeader("origin"));
      std::string token = extract_session_token(req);
      std::string space_id(req->getParameter(0));
      std::string file_id(req->getParameter(1));
      std::string version_id(req->getParameter(2));

      auto aborted = std::make_shared<bool>(false);
      res->onAborted([aborted, origin]() { *aborted = true; });

      res->onData([this, res, aborted, scope, token, space_id, file_id, version_id, origin](
                    std::string_view, bool last) mutable {
        if (!last) return;

        pool_.submit([this, res, aborted, scope, token, space_id, file_id, version_id, origin]() {
          if (*aborted) return;

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
          std::string user_id = *user_id_opt;

          if (!check_space_access_sync(space_id, user_id)) {
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("403")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Not a member of this space"})");
              scope->observe(403);
            });
            return;
          }

          if (!require_permission_sync(space_id, file_id, user_id, "edit")) {
            auto err = json({{"error", "Requires edit permission"}}).dump();
            loop_->defer([res, aborted, scope, err = std::move(err), origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("403")->writeHeader("Content-Type", "application/json")->end(err);
              scope->observe(403);
            });
            return;
          }

          auto file = db.find_space_file(file_id);
          if (!file || file->space_id != space_id || file->is_deleted || file->is_folder) {
            loop_->defer([res, aborted, scope, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("404")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"File not found"})");
              scope->observe(404);
            });
            return;
          }

          auto version = db.get_file_version(version_id);
          if (!version || version->file_id != file_id) {
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

          // Create a new version that copies the old version's data
          auto new_version = db.create_file_version(
            file_id, version->disk_file_id, version->file_size, version->mime_type, user_id);
          json resp = space_file_version_to_json(new_version);
          std::string resp_str = resp.dump();

          loop_->defer([res, aborted, scope, resp_str = std::move(resp_str), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(resp_str);
            scope->observe(200);
          });
        });
      });
    });

  // --- Admin storage endpoint ---

  app.get("/api/admin/storage", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("GET", "/api/admin/storage");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    std::string token = extract_session_token(req);

    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });

    pool_.submit([this, res, aborted, scope, token, origin]() {
      if (*aborted) return;

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

      auto user = db.find_user_by_id(*user_id_opt);
      if (!user || (user->role != "admin" && user->role != "owner")) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          res->writeStatus("403")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Admin access required"})");
          scope->observe(403);
        });
        return;
      }

      auto spaces = db.get_all_space_storage();
      int64_t total_used = db.get_total_file_size();

      json arr = json::array();
      for (const auto& s : spaces) {
        arr.push_back(
          {{"space_id", s.space_id},
           {"space_name", s.space_name},
           {"storage_used", s.storage_used},
           {"storage_limit", s.storage_limit},
           {"file_count", s.file_count},
           {"is_personal", s.is_personal},
           {"personal_owner_name", s.personal_owner_name}});
      }

      json resp = {{"spaces", arr}, {"total_used", total_used}};
      std::string resp_str = resp.dump();

      loop_->defer([res, aborted, scope, resp_str = std::move(resp_str), origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(resp_str);
        scope->observe(200);
      });
    });
  });

  // Download folder as ZIP
  // Auth via header or query param (same pattern as single-file download)
  app.get("/api/spaces/:spaceId/files/:folderId/download-zip", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>(
      "GET", "/api/spaces/:spaceId/files/:folderId/download-zip");
    handler_utils::set_request_id_header(res, *scope);
    std::string origin(req->getHeader("origin"));
    std::string token = extract_session_token(req);
    if (token.empty()) token = std::string(req->getQuery("token"));
    std::string space_id(req->getParameter(0));
    std::string folder_id(req->getParameter(1));

    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });

    pool_.submit([this, res, aborted, scope, token, space_id, folder_id, origin]() {
      if (*aborted) return;

      auto user_id_opt = db.validate_session(token);
      if (!user_id_opt) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Unauthorized"})");
          scope->observe(401);
        });
        return;
      }
      std::string user_id = *user_id_opt;

      if (!check_space_access_sync(space_id, user_id)) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("403")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Not a member of this space"})");
          scope->observe(403);
        });
        return;
      }

      auto folder = db.find_space_file(folder_id);
      if (!folder || folder->space_id != space_id || folder->is_deleted || !folder->is_folder) {
        loop_->defer([res, aborted, scope, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("404")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Folder not found"})");
          scope->observe(404);
        });
        return;
      }

      auto entries = db.list_space_files_recursive(space_id, folder_id);

      ZipBuilder zip;
      for (const auto& entry : entries) {
        if (entry.disk_file_id.empty()) continue;
        if (!file_access_utils::is_valid_hex_id(entry.disk_file_id)) continue;
        std::string path = config.upload_dir + "/" + entry.disk_file_id;
        std::ifstream in(path, std::ios::binary);
        if (!in) continue;
        std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        zip.add_file(entry.relative_path, data);
      }

      auto archive = std::make_shared<std::string>(zip.build());
      std::string disposition = file_access_utils::attachment_disposition(folder->name + ".zip");

      loop_->defer([res, aborted, scope, archive, disposition = std::move(disposition), origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/zip")
          ->writeHeader("Content-Disposition", disposition)
          ->end(*archive);
        scope->observe(200);
      });
    });
  });
}

template <bool SSL>
std::string SpaceFileHandler<SSL>::get_user_id(uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req) {
  return validate_session_or_401(res, req, db);
}

// Sync version: returns true if user has space access, false otherwise. No response writing.
template <bool SSL>
bool SpaceFileHandler<SSL>::check_space_access_sync(
  const std::string& space_id, const std::string& user_id) {
  if (db.is_space_member(space_id, user_id)) return true;
  auto user = db.find_user_by_id(user_id);
  if (user && (user->role == "admin" || user->role == "owner")) return true;

  // Allow access to personal spaces if user has per-resource permissions
  auto space = db.find_space_by_id(space_id);
  if (space && space->is_personal) {
    if (db.has_resource_permission_in_space(space_id, user_id, "files")) return true;
  }

  return false;
}

template <bool SSL>
bool SpaceFileHandler<SSL>::check_space_access(
  uWS::HttpResponse<SSL>* res,
  const std::string& space_id,
  const std::string& user_id,
  const std::string& origin) {
  if (check_space_access_sync(space_id, user_id)) return true;
  cors::apply(res, origin);
  res->writeStatus("403")
    ->writeHeader("Content-Type", "application/json")
    ->end(R"({"error":"Not a member of this space"})");
  return false;
}

template <bool SSL>
std::string SpaceFileHandler<SSL>::get_access_level(
  const std::string& space_id, const std::string& file_id, const std::string& user_id) {
  // Server admin/owner -> full access
  auto user = db.find_user_by_id(user_id);
  if (user && (user->role == "admin" || user->role == "owner")) return "owner";

  // Space admin/owner -> full access
  auto space_role = db.get_space_member_role(space_id, user_id);
  if (space_role == "admin" || space_role == "owner") return "owner";

  // For personal spaces, non-members only get access via explicit file permissions
  auto space = db.find_space_by_id(space_id);
  if (space && space->is_personal && space_role.empty()) {
    if (file_id.empty()) return "none";
    auto file_perm = db.get_effective_file_permission(file_id, user_id);
    return file_perm.empty() ? "none" : file_perm;
  }

  // "user" role members default to "view"; file-level permissions can override
  if (file_id.empty()) return "view";

  // Get file-level permission
  auto file_perm = db.get_effective_file_permission(file_id, user_id);
  if (!file_perm.empty()) return file_perm;

  return "view";
}

// Sync version: returns true if permission is sufficient, false otherwise. No response writing.
template <bool SSL>
bool SpaceFileHandler<SSL>::require_permission_sync(
  const std::string& space_id,
  const std::string& file_id,
  const std::string& user_id,
  const std::string& required_level) {
  auto level = get_access_level(space_id, file_id, user_id);
  return perm_rank(level) >= perm_rank(required_level);
}

template <bool SSL>
bool SpaceFileHandler<SSL>::require_permission(
  uWS::HttpResponse<SSL>* res,
  const std::string& space_id,
  const std::string& file_id,
  const std::string& user_id,
  const std::string& required_level,
  const std::string& origin) {
  if (require_permission_sync(space_id, file_id, user_id, required_level)) return true;

  cors::apply(res, origin);
  res->writeStatus("403")
    ->writeHeader("Content-Type", "application/json")
    ->end(json({{"error", "Requires " + required_level + " permission"}}).dump());
  return false;
}

template <bool SSL>
int SpaceFileHandler<SSL>::perm_rank(const std::string& p) {
  if (p == "owner") return 2;
  if (p == "edit") return 1;
  return 0;
}

// Sync version: returns empty string if OK, or error JSON string if limit exceeded.
template <bool SSL>
std::string SpaceFileHandler<SSL>::check_personal_total_limit_sync(
  const std::string& space_id, int64_t upload_size) {
  auto space = db.find_space_by_id(space_id);
  if (!space || !space->is_personal) return "";

  auto limit_str = db.get_setting("personal_spaces_total_storage_limit");
  if (!limit_str) return "";
  int64_t limit = 0;
  try {
    limit = std::stoll(*limit_str);
  } catch (...) {
    return "";
  }
  if (limit <= 0) return "";

  int64_t total_used = db.get_total_personal_spaces_storage_used();
  if (total_used + upload_size > limit) {
    return R"({"error":"Total personal spaces storage limit reached"})";
  }
  return "";
}

template <bool SSL>
bool SpaceFileHandler<SSL>::check_personal_total_limit(
  uWS::HttpResponse<SSL>* res,
  const std::string& space_id,
  int64_t upload_size,
  const std::string& origin) {
  auto err = check_personal_total_limit_sync(space_id, upload_size);
  if (err.empty()) return false;

  cors::apply(res, origin);
  res->writeStatus("413")->writeHeader("Content-Type", "application/json")->end(err);
  return true;
}

template struct SpaceFileHandler<false>;
template struct SpaceFileHandler<true>;
