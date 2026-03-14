#include "handlers/file_handler.h"
#include "handlers/file_access_utils.h"
#include "handlers/format_utils.h"
#include <fstream>
#include <filesystem>

template <bool SSL>
void FileHandler<SSL>::register_routes(uWS::TemplatedApp<SSL>& app) {
    app_ = &app;

    // Upload file to a channel
    app.post("/api/channels/:id/upload", [this](auto* res, auto* req) {
        std::string channel_id(req->getParameter("id"));
        std::string filename(req->getQuery("filename"));
        std::string content_type(req->getQuery("content_type"));
        std::string message_text(req->getQuery("message"));

        std::string user_id = validate_session_or_401(res, req, db);
        if (user_id.empty()) return;

        // Check if server is archived
        if (db.is_server_archived()) {
            res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Server is archived. No new content can be created."})");
            return;
        }

        // Check if channel is archived
        auto ch = db.find_channel_by_id(channel_id);
        if (ch && ch->is_archived) {
            res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"This channel is archived"})");
            return;
        }

        // Check channel membership and write permission
        std::string role = db.get_effective_role(channel_id, user_id);
        if (role.empty() || role == "read") {
            res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"No write access to this channel"})");
            return;
        }

        if (filename.empty()) filename = "upload";
        if (content_type.empty()) content_type = "application/octet-stream";

        auto body = std::make_shared<std::string>();

        // Read effective max file size from DB setting, fall back to config
        int64_t max_size = file_access_utils::parse_max_file_size(
            db.get_setting("max_file_size"), config.max_file_size);

        res->onData([this, res, body, max_size, channel_id, user_id, filename,
                     content_type, message_text](std::string_view data, bool last) mutable {
            body->append(data);

            if (file_access_utils::exceeds_file_size_limit(max_size, static_cast<int64_t>(body->size()))) {
                std::string msg = file_access_utils::file_too_large_message(max_size);
                res->writeStatus("413")->writeHeader("Content-Type", "application/json")
                    ->end(json{{"error", msg}}.dump());
                return;
            }

            if (!last) return;

            try {
                // Check storage limit
                int64_t max_storage = file_access_utils::parse_max_storage_size(
                    db.get_setting("max_storage_size"));
                if (max_storage > 0 && file_access_utils::exceeds_storage_limit(
                        max_storage, db.get_total_file_size(), static_cast<int64_t>(body->size()))) {
                    res->writeStatus("413")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Server storage limit reached"})");
                    return;
                }

                // Generate file ID
                std::string file_id = format_utils::random_hex(32);

                // Save to disk
                std::string path = config.upload_dir + "/" + file_id;
                std::ofstream out(path, std::ios::binary);
                if (!out) {
                    res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Failed to save file"})");
                    return;
                }
                out.write(body->data(), body->size());
                out.close();

                int64_t file_size = static_cast<int64_t>(body->size());

                // Create message with file attachment
                auto msg = db.create_file_message(channel_id, user_id, message_text,
                                                   file_id, filename, file_size, content_type);

                // Broadcast via WebSocket
                json broadcast = {
                    {"type", "new_message"},
                    {"message", {
                        {"id", msg.id},
                        {"channel_id", msg.channel_id},
                        {"user_id", msg.user_id},
                        {"username", msg.username},
                        {"content", msg.content},
                        {"created_at", msg.created_at},
                        {"is_deleted", false},
                        {"file_id", msg.file_id},
                        {"file_name", msg.file_name},
                        {"file_size", msg.file_size},
                        {"file_type", msg.file_type}
                    }}
                };
                app_->publish("channel:" + channel_id, broadcast.dump(), uWS::OpCode::TEXT);

                // Return message JSON
                json resp = broadcast["message"];
                res->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(resp.dump());
            } catch (const std::exception& e) {
                res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                    ->end(json{{"error", e.what()}}.dump());
            }
        });
        res->onAborted([]() {});
    });

    // --- Chunked upload to channel: init ---
    app.post("/api/channels/:id/upload/init", [this](auto* res, auto* req) {
        std::string channel_id(req->getParameter("id"));
        std::string user_id = validate_session_or_401(res, req, db);
        if (user_id.empty()) return;

        if (db.is_server_archived()) {
            res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Server is archived. No new content can be created."})");
            return;
        }

        auto ch = db.find_channel_by_id(channel_id);
        if (ch && ch->is_archived) {
            res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"This channel is archived"})");
            return;
        }

        std::string role = db.get_effective_role(channel_id, user_id);
        if (role.empty() || role == "read") {
            res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"No write access to this channel"})");
            return;
        }

        auto body = std::make_shared<std::string>();
        res->onData([this, res, body, channel_id, user_id](std::string_view data, bool last) {
            body->append(data);
            if (!last) return;

            try {
                auto j = json::parse(*body);
                std::string filename = j.value("filename", "upload");
                std::string content_type = j.value("content_type", "application/octet-stream");
                std::string message_text = j.value("message", "");
                int64_t total_size = j.value("total_size", int64_t(0));
                int chunk_count = j.value("chunk_count", 0);
                int64_t chunk_size = j.value("chunk_size", int64_t(0));

                if (chunk_count <= 0 || chunk_size <= 0) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Invalid chunk count or size"})");
                    return;
                }

                int64_t max_size = file_access_utils::parse_max_file_size(
                    db.get_setting("max_file_size"), config.max_file_size);
                if (file_access_utils::exceeds_file_size_limit(max_size, total_size)) {
                    std::string msg = file_access_utils::file_too_large_message(max_size);
                    res->writeStatus("413")->writeHeader("Content-Type", "application/json")
                        ->end(json{{"error", msg}}.dump());
                    return;
                }

                int64_t max_storage = file_access_utils::parse_max_storage_size(
                    db.get_setting("max_storage_size"));
                if (max_storage > 0 && file_access_utils::exceeds_storage_limit(
                        max_storage, db.get_total_file_size(), total_size)) {
                    res->writeStatus("413")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Server storage limit reached"})");
                    return;
                }

                json metadata = {
                    {"filename", filename}, {"content_type", content_type},
                    {"channel_id", channel_id}, {"message", message_text}
                };
                std::string upload_id = uploads.create_session(
                    user_id, total_size, chunk_count, chunk_size, metadata);

                res->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(json{{"upload_id", upload_id}}.dump());
            } catch (...) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Invalid request body"})");
            }
        });
        res->onAborted([]() {});
    });

    // --- Chunked upload to channel: receive chunk ---
    app.post("/api/channels/:id/upload/:uploadId/chunk", [this](auto* res, auto* req) {
        std::string user_id = validate_session_or_401(res, req, db);
        if (user_id.empty()) return;
        std::string upload_id(req->getParameter(1));
        std::string index_str(req->getQuery("index"));
        std::string expected_hash(req->getQuery("hash"));

        int index = -1;
        try { index = std::stoi(index_str); } catch (...) {}

        auto body = std::make_shared<std::string>();
        res->onData([this, res, body, upload_id, user_id, index, expected_hash](std::string_view data, bool last) {
            body->append(data);
            if (!last) return;

            auto* session = uploads.get_session(upload_id);
            if (!session || session->user_id != user_id) {
                res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(R"({"error":"Upload session not found"})");
                return;
            }

            if (index < 0 || index >= session->chunk_count) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(R"({"error":"Invalid chunk index"})");
                return;
            }

            std::string err = uploads.store_chunk_err(upload_id, index, *body, expected_hash);
            if (!err.empty()) {
                std::string status = (err == "hash_mismatch") ? "409" : "500";
                res->writeStatus(status)->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(json{{"error", err}}.dump());
                return;
            }

            res->writeHeader("Content-Type", "application/json")
                ->writeHeader("Access-Control-Allow-Origin", "*")
                ->end(R"({"ok":true})");
        });
        res->onAborted([]() {});
    });

    // --- Chunked upload to channel: complete ---
    app.post("/api/channels/:id/upload/:uploadId/complete", [this](auto* res, auto* req) {
        std::string user_id = validate_session_or_401(res, req, db);
        if (user_id.empty()) return;
        std::string channel_id(req->getParameter(0));
        std::string upload_id(req->getParameter(1));

        res->onData([this, res, channel_id, upload_id, user_id](std::string_view, bool last) {
            if (!last) return;

            auto* session = uploads.get_session(upload_id);
            if (!session || session->user_id != user_id) {
                res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(R"({"error":"Upload session not found"})");
                return;
            }

            if (!uploads.is_complete(upload_id)) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(R"({"error":"Not all chunks have been uploaded"})");
                return;
            }

            try {
                std::string filename = session->metadata.value("filename", "upload");
                std::string content_type = session->metadata.value("content_type", "application/octet-stream");
                std::string message_text = session->metadata.value("message", "");

                std::string file_id = format_utils::random_hex(32);
                std::string dest_path = config.upload_dir + "/" + file_id;

                int64_t assembled_size = uploads.assemble(upload_id, dest_path);
                if (assembled_size < 0) {
                    res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(R"({"error":"Failed to assemble file"})");
                    uploads.remove_session(upload_id);
                    return;
                }

                if (assembled_size != session->total_size) {
                    std::filesystem::remove(dest_path);
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(R"({"error":"Assembled file size does not match expected size"})");
                    uploads.remove_session(upload_id);
                    return;
                }

                auto msg = db.create_file_message(channel_id, user_id, message_text,
                                                   file_id, filename, assembled_size, content_type);

                json broadcast = {
                    {"type", "new_message"},
                    {"message", {
                        {"id", msg.id},
                        {"channel_id", msg.channel_id},
                        {"user_id", msg.user_id},
                        {"username", msg.username},
                        {"content", msg.content},
                        {"created_at", msg.created_at},
                        {"is_deleted", false},
                        {"file_id", msg.file_id},
                        {"file_name", msg.file_name},
                        {"file_size", msg.file_size},
                        {"file_type", msg.file_type}
                    }}
                };
                app_->publish("channel:" + channel_id, broadcast.dump(), uWS::OpCode::TEXT);

                json resp = broadcast["message"];
                uploads.remove_session(upload_id);

                res->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(resp.dump());
            } catch (const std::exception& e) {
                uploads.remove_session(upload_id);
                res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                    ->end(json{{"error", e.what()}}.dump());
            }
        });
        res->onAborted([]() {});
    });

    // Download a file
    app.get("/api/files/:id", [this](auto* res, auto* req) {
        std::string file_id(req->getParameter("id"));

        // Auth via header or query param (needed for <img> tags)
        std::string token = extract_bearer_token(req);
        if (token.empty()) {
            token = std::string(req->getQuery("token"));
        }

        auto user_id = db.validate_session(token);
        if (!user_id) {
            res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Unauthorized"})");
            return;
        }

        // Validate file_id is hex-only (prevent path traversal)
        if (!file_access_utils::is_valid_hex_id(file_id)) {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Invalid file ID"})");
            return;
        }

        auto info = db.get_file_info(file_id);
        if (!info) {
            res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"File not found"})");
            return;
        }

        std::string path = config.upload_dir + "/" + file_id;
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in) {
            res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"File not found on disk"})");
            return;
        }

        auto size = in.tellg();
        in.seekg(0);
        std::string content(size, '\0');
        in.read(content.data(), size);

        std::string disposition = file_access_utils::inline_disposition(info->file_name);

        res->writeHeader("Content-Type", info->file_type)
            ->writeHeader("Content-Disposition", disposition)
            ->writeHeader("Access-Control-Allow-Origin", "*")
            ->writeHeader("Cache-Control", "private, max-age=86400")
            ->end(content);
    });
}

template struct FileHandler<false>;
template struct FileHandler<true>;
