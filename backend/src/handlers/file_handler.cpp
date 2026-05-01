#include "handlers/file_handler.h"
#include <filesystem>
#include <fstream>
#include "handlers/cors_utils.h"
#include "handlers/file_access_utils.h"
#include "handlers/format_utils.h"

template <bool SSL>
void FileHandler<SSL>::register_routes(uWS::TemplatedApp<SSL>& app) {
  app_ = &app;

  // Upload file to a channel
  app.post("/api/channels/:id/upload", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    std::string channel_id(req->getParameter("id"));
    std::string filename(req->getQuery("filename"));
    std::string content_type(req->getQuery("content_type"));
    std::string message_text(req->getQuery("message"));
    std::string token = extract_bearer_token(req);

    if (filename.empty()) filename = "upload";
    if (content_type.empty()) content_type = "application/octet-stream";

    auto body = std::make_shared<std::string>();
    auto aborted = std::make_shared<bool>(false);

    res->onAborted([aborted, origin]() { *aborted = true; });

    res->onData(
      [this, res, body, aborted, channel_id, token, filename, content_type, message_text, origin](
        std::string_view data, bool last) mutable {
        body->append(data);
        if (!last) return;

        pool_->submit([this,
                       res,
                       body,
                       aborted,
                       channel_id,
                       token,
                       filename,
                       content_type,
                       message_text,
                       origin]() {
          if (*aborted) return;

          auto user_id_opt = db.validate_session(token);
          if (!user_id_opt) {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              res->writeStatus("401")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Unauthorized"})");
            });
            return;
          }
          std::string user_id = *user_id_opt;

          // Check if server is archived
          if (db.is_server_archived()) {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              res->writeStatus("403")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Server is archived. No new content can be created."})");
            });
            return;
          }

          // Check if channel is archived
          auto ch = db.find_channel_by_id(channel_id);
          if (ch && ch->is_archived) {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              res->writeStatus("403")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"This channel is archived"})");
            });
            return;
          }

          // Check channel membership and write permission
          std::string role = db.get_effective_role(channel_id, user_id);
          if (role.empty() || role == "read") {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              res->writeStatus("403")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"No write access to this channel"})");
            });
            return;
          }

          // Read effective max file size from DB setting, fall back to config
          int64_t max_size = file_access_utils::parse_max_file_size(
            db.get_setting("max_file_size"), config.max_file_size);

          if (file_access_utils::exceeds_file_size_limit(
                max_size, static_cast<int64_t>(body->size()))) {
            std::string msg = file_access_utils::file_too_large_message(max_size);
            auto err_json = json{{"error", msg}}.dump();
            loop_->defer([res, aborted, err_json = std::move(err_json), origin]() {
              if (*aborted) return;
              res->writeStatus("413")
                ->writeHeader("Content-Type", "application/json")
                ->end(err_json);
            });
            return;
          }

          try {
            // Check storage limit
            int64_t max_storage =
              file_access_utils::parse_max_storage_size(db.get_setting("max_storage_size"));
            if (
              max_storage > 0 &&
              file_access_utils::exceeds_storage_limit(
                max_storage, db.get_total_file_size(), static_cast<int64_t>(body->size()))) {
              loop_->defer([res, aborted, origin]() {
                if (*aborted) return;
                res->writeStatus("413")
                  ->writeHeader("Content-Type", "application/json")
                  ->end(R"({"error":"Server storage limit reached"})");
              });
              return;
            }

            // Generate file ID
            std::string file_id = format_utils::random_hex(32);

            // Save to disk
            std::string path = config.upload_dir + "/" + file_id;
            std::ofstream out(path, std::ios::binary);
            if (!out) {
              loop_->defer([res, aborted, origin]() {
                if (*aborted) return;
                res->writeStatus("500")
                  ->writeHeader("Content-Type", "application/json")
                  ->end(R"({"error":"Failed to save file"})");
              });
              return;
            }
            out.write(body->data(), body->size());
            out.close();

            int64_t file_size = static_cast<int64_t>(body->size());

            // Create message with file attachment
            auto msg = db.create_file_message(
              channel_id, user_id, message_text, file_id, filename, file_size, content_type);

            // Broadcast via WebSocket
            json broadcast = {
              {"type", "new_message"},
              {"message",
               {{"id", msg.id},
                {"channel_id", msg.channel_id},
                {"user_id", msg.user_id},
                {"username", msg.username},
                {"content", msg.content},
                {"created_at", msg.created_at},
                {"is_deleted", false},
                {"file_id", msg.file_id},
                {"file_name", msg.file_name},
                {"file_size", msg.file_size},
                {"file_type", msg.file_type}}}};

            std::string broadcast_str = broadcast.dump();
            json resp = broadcast["message"];
            std::string resp_str = resp.dump();

            loop_->defer([this,
                          res,
                          aborted,
                          channel_id,
                          broadcast_str = std::move(broadcast_str),
                          resp_str = std::move(resp_str),
                          origin]() {
              if (*aborted) return;
              app_->publish("channel:" + channel_id, broadcast_str, uWS::OpCode::TEXT);
              cors::apply(res, origin);
              res->writeHeader("Content-Type", "application/json")->end(resp_str);
            });
          } catch (const std::exception& e) {
            std::string err = json{{"error", e.what()}}.dump();
            loop_->defer([res, aborted, err = std::move(err), origin]() {
              if (*aborted) return;
              res->writeStatus("500")->writeHeader("Content-Type", "application/json")->end(err);
            });
          }
        });
      });
  });

  // --- Chunked upload to channel: init ---
  app.post("/api/channels/:id/upload/init", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    std::string channel_id(req->getParameter("id"));
    std::string token = extract_bearer_token(req);

    auto body = std::make_shared<std::string>();
    auto aborted = std::make_shared<bool>(false);

    res->onAborted([aborted, origin]() { *aborted = true; });

    res->onData(
      [this, res, body, aborted, channel_id, token, origin](std::string_view data, bool last) {
        body->append(data);
        if (!last) return;

        pool_->submit([this, res, body, aborted, channel_id, token, origin]() {
          if (*aborted) return;

          auto user_id_opt = db.validate_session(token);
          if (!user_id_opt) {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              res->writeStatus("401")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Unauthorized"})");
            });
            return;
          }
          std::string user_id = *user_id_opt;

          if (db.is_server_archived()) {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              res->writeStatus("403")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Server is archived. No new content can be created."})");
            });
            return;
          }

          auto ch = db.find_channel_by_id(channel_id);
          if (ch && ch->is_archived) {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              res->writeStatus("403")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"This channel is archived"})");
            });
            return;
          }

          std::string role = db.get_effective_role(channel_id, user_id);
          if (role.empty() || role == "read") {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              res->writeStatus("403")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"No write access to this channel"})");
            });
            return;
          }

          try {
            auto j = json::parse(*body);
            std::string filename = j.value("filename", "upload");
            std::string content_type = j.value("content_type", "application/octet-stream");
            std::string message_text = j.value("message", "");
            int64_t total_size = j.value("total_size", int64_t(0));
            int chunk_count = j.value("chunk_count", 0);
            int64_t chunk_size = j.value("chunk_size", int64_t(0));

            if (chunk_count <= 0 || chunk_size <= 0) {
              loop_->defer([res, aborted, origin]() {
                if (*aborted) return;
                res->writeStatus("400")
                  ->writeHeader("Content-Type", "application/json")
                  ->end(R"({"error":"Invalid chunk count or size"})");
              });
              return;
            }

            int64_t max_size = file_access_utils::parse_max_file_size(
              db.get_setting("max_file_size"), config.max_file_size);
            if (file_access_utils::exceeds_file_size_limit(max_size, total_size)) {
              std::string msg = file_access_utils::file_too_large_message(max_size);
              auto err_json = json{{"error", msg}}.dump();
              loop_->defer([res, aborted, err_json = std::move(err_json), origin]() {
                if (*aborted) return;
                res->writeStatus("413")
                  ->writeHeader("Content-Type", "application/json")
                  ->end(err_json);
              });
              return;
            }

            int64_t max_storage =
              file_access_utils::parse_max_storage_size(db.get_setting("max_storage_size"));
            if (
              max_storage > 0 && file_access_utils::exceeds_storage_limit(
                                   max_storage, db.get_total_file_size(), total_size)) {
              loop_->defer([res, aborted, origin]() {
                if (*aborted) return;
                res->writeStatus("413")
                  ->writeHeader("Content-Type", "application/json")
                  ->end(R"({"error":"Server storage limit reached"})");
              });
              return;
            }

            json metadata = {
              {"filename", filename},
              {"content_type", content_type},
              {"channel_id", channel_id},
              {"message", message_text}};
            std::string upload_id =
              uploads.create_session(user_id, total_size, chunk_count, chunk_size, metadata);

            auto resp_str = json{{"upload_id", upload_id}}.dump();
            loop_->defer([res, aborted, resp_str = std::move(resp_str), origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeHeader("Content-Type", "application/json")->end(resp_str);
            });
          } catch (...) {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Invalid request body"})");
            });
          }
        });
      });
  });

  // --- Chunked upload to channel: receive chunk ---
  app.post("/api/channels/:id/upload/:uploadId/chunk", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    std::string token = extract_bearer_token(req);
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

    res->onData([this, res, body, aborted, token, upload_id, index, expected_hash, origin](
                  std::string_view data, bool last) {
      body->append(data);
      if (!last) return;

      pool_->submit([this, res, body, aborted, token, upload_id, index, expected_hash, origin]() {
        if (*aborted) return;

        auto user_id_opt = db.validate_session(token);
        if (!user_id_opt) {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            res->writeStatus("401")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Unauthorized"})");
          });
          return;
        }
        std::string user_id = *user_id_opt;

        auto session = uploads.get_session(upload_id);
        if (!session || session->user_id != user_id) {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("404")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Upload session not found"})");
          });
          return;
        }

        if (index < 0 || index >= session->chunk_count) {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Invalid chunk index"})");
          });
          return;
        }

        std::string err = uploads.store_chunk_err(upload_id, index, *body, expected_hash);
        if (!err.empty()) {
          std::string status = (err == "hash_mismatch") ? "409" : "500";
          auto err_json = json{{"error", err}}.dump();
          loop_->defer(
            [res, aborted, status = std::move(status), err_json = std::move(err_json), origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus(status)
                ->writeHeader("Content-Type", "application/json")
                ->end(err_json);
            });
          return;
        }

        loop_->defer([res, aborted, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
        });
      });
    });
  });

  // --- Chunked upload to channel: complete ---
  app.post("/api/channels/:id/upload/:uploadId/complete", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    std::string token = extract_bearer_token(req);
    std::string channel_id(req->getParameter(0));
    std::string upload_id(req->getParameter(1));

    auto aborted = std::make_shared<bool>(false);

    res->onAborted([aborted, origin]() { *aborted = true; });

    res->onData(
      [this, res, aborted, channel_id, upload_id, token, origin](std::string_view, bool last) {
        if (!last) return;

        pool_->submit([this, res, aborted, channel_id, upload_id, token, origin]() {
          if (*aborted) return;

          auto user_id_opt = db.validate_session(token);
          if (!user_id_opt) {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              res->writeStatus("401")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Unauthorized"})");
            });
            return;
          }
          std::string user_id = *user_id_opt;

          auto session = uploads.get_session(upload_id);
          if (!session || session->user_id != user_id) {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("404")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Upload session not found"})");
            });
            return;
          }

          if (!uploads.is_complete(upload_id)) {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Not all chunks have been uploaded"})");
            });
            return;
          }

          try {
            std::string filename = session->metadata.value("filename", "upload");
            std::string content_type =
              session->metadata.value("content_type", "application/octet-stream");
            std::string message_text = session->metadata.value("message", "");

            std::string file_id = format_utils::random_hex(32);
            std::string dest_path = config.upload_dir + "/" + file_id;

            int64_t assembled_size = uploads.assemble(upload_id, dest_path);
            if (assembled_size < 0) {
              uploads.remove_session(upload_id);
              loop_->defer([res, aborted, origin]() {
                if (*aborted) return;
                cors::apply(res, origin);
                res->writeStatus("500")
                  ->writeHeader("Content-Type", "application/json")
                  ->end(R"({"error":"Failed to assemble file"})");
              });
              return;
            }

            if (assembled_size != session->total_size) {
              std::filesystem::remove(dest_path);
              uploads.remove_session(upload_id);
              loop_->defer([res, aborted, origin]() {
                if (*aborted) return;
                cors::apply(res, origin);
                res->writeStatus("400")
                  ->writeHeader("Content-Type", "application/json")
                  ->end(R"({"error":"Assembled file size does not match expected size"})");
              });
              return;
            }

            auto msg = db.create_file_message(
              channel_id, user_id, message_text, file_id, filename, assembled_size, content_type);

            json broadcast = {
              {"type", "new_message"},
              {"message",
               {{"id", msg.id},
                {"channel_id", msg.channel_id},
                {"user_id", msg.user_id},
                {"username", msg.username},
                {"content", msg.content},
                {"created_at", msg.created_at},
                {"is_deleted", false},
                {"file_id", msg.file_id},
                {"file_name", msg.file_name},
                {"file_size", msg.file_size},
                {"file_type", msg.file_type}}}};

            std::string broadcast_str = broadcast.dump();
            json resp = broadcast["message"];
            std::string resp_str = resp.dump();
            uploads.remove_session(upload_id);

            loop_->defer([this,
                          res,
                          aborted,
                          channel_id,
                          broadcast_str = std::move(broadcast_str),
                          resp_str = std::move(resp_str),
                          origin]() {
              if (*aborted) return;
              app_->publish("channel:" + channel_id, broadcast_str, uWS::OpCode::TEXT);
              cors::apply(res, origin);
              res->writeHeader("Content-Type", "application/json")->end(resp_str);
            });
          } catch (const std::exception& e) {
            uploads.remove_session(upload_id);
            std::string err = json{{"error", e.what()}}.dump();
            loop_->defer([res, aborted, err = std::move(err), origin]() {
              if (*aborted) return;
              res->writeStatus("500")->writeHeader("Content-Type", "application/json")->end(err);
            });
          }
        });
      });
  });

  // Download a file
  app.get("/api/files/:id", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    std::string file_id(req->getParameter("id"));

    // Auth via header or query param (needed for <img> tags)
    std::string token = extract_bearer_token(req);
    if (token.empty()) {
      token = std::string(req->getQuery("token"));
    }

    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });

    pool_->submit([this, res, aborted, file_id, token, origin]() {
      if (*aborted) return;

      auto user_id = db.validate_session(token);
      if (!user_id) {
        loop_->defer([res, aborted, origin]() {
          if (*aborted) return;
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Unauthorized"})");
        });
        return;
      }

      // Validate file_id is hex-only (prevent path traversal)
      if (!file_access_utils::is_valid_hex_id(file_id)) {
        loop_->defer([res, aborted, origin]() {
          if (*aborted) return;
          res->writeStatus("400")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Invalid file ID"})");
        });
        return;
      }

      auto info = db.get_file_info(file_id);
      if (!info) {
        loop_->defer([res, aborted, origin]() {
          if (*aborted) return;
          res->writeStatus("404")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"File not found"})");
        });
        return;
      }

      std::string path = config.upload_dir + "/" + file_id;
      std::ifstream in(path, std::ios::binary | std::ios::ate);
      if (!in) {
        loop_->defer([res, aborted, origin]() {
          if (*aborted) return;
          res->writeStatus("404")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"File not found on disk"})");
        });
        return;
      }

      auto size = in.tellg();
      in.seekg(0);
      auto content = std::make_shared<std::string>(size, '\0');
      in.read(content->data(), size);

      std::string disposition = file_access_utils::inline_disposition(info->file_name);
      std::string file_type = info->file_type;

      loop_->defer([res,
                    aborted,
                    content,
                    file_type = std::move(file_type),
                    disposition = std::move(disposition),
                    origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", file_type)
          ->writeHeader("Content-Disposition", disposition)
          ->writeHeader("Cache-Control", "private, max-age=86400")
          ->end(*content);
      });
    });
  });
}

template struct FileHandler<false>;
template struct FileHandler<true>;
