#pragma once
#include <App.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <random>
#include <sstream>
#include <iomanip>
#include "db/database.h"
#include "config.h"

using json = nlohmann::json;

template <bool SSL>
struct FileHandler {
    Database& db;
    const Config& config;
    uWS::TemplatedApp<SSL>* app_ = nullptr;

    void register_routes(uWS::TemplatedApp<SSL>& app) {
        app_ = &app;

        // Upload file to a channel
        app.post("/api/channels/:id/upload", [this](auto* res, auto* req) {
            std::string channel_id(req->getParameter("id"));
            std::string token(req->getHeader("authorization"));
            if (token.rfind("Bearer ", 0) == 0) token = token.substr(7);

            std::string filename(req->getQuery("filename"));
            std::string content_type(req->getQuery("content_type"));
            std::string message_text(req->getQuery("message"));

            auto user_id_opt = db.validate_session(token);
            if (!user_id_opt) {
                res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"Unauthorized"})");
                return;
            }
            std::string user_id = *user_id_opt;

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
            auto max_file_setting = db.get_setting("max_file_size");
            int64_t max_size = max_file_setting ? std::stoll(*max_file_setting) : config.max_file_size;

            res->onData([this, res, body, max_size, channel_id, user_id, filename,
                         content_type, message_text](std::string_view data, bool last) mutable {
                body->append(data);

                if (static_cast<int64_t>(body->size()) > max_size) {
                    std::string msg = "File too large (max " + format_size(max_size) + ")";
                    res->writeStatus("413")->writeHeader("Content-Type", "application/json")
                        ->end(json{{"error", msg}}.dump());
                    return;
                }

                if (!last) return;

                try {
                    // Check storage limit
                    auto max_storage_setting = db.get_setting("max_storage_size");
                    int64_t max_storage = max_storage_setting ? std::stoll(*max_storage_setting) : 0;
                    if (max_storage > 0) {
                        int64_t used = db.get_total_file_size();
                        if (used + static_cast<int64_t>(body->size()) > max_storage) {
                            res->writeStatus("413")->writeHeader("Content-Type", "application/json")
                                ->end(R"({"error":"Server storage limit reached"})");
                            return;
                        }
                    }

                    // Generate file ID
                    std::string file_id = random_hex(32);

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

        // Download a file
        app.get("/api/files/:id", [this](auto* res, auto* req) {
            std::string file_id(req->getParameter("id"));

            // Auth via header or query param (needed for <img> tags)
            std::string token(req->getHeader("authorization"));
            if (token.rfind("Bearer ", 0) == 0) token = token.substr(7);
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
            for (char c : file_id) {
                if (!std::isxdigit(static_cast<unsigned char>(c))) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"Invalid file ID"})");
                    return;
                }
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

            std::string disposition = "inline; filename=\"" + info->file_name + "\"";

            res->writeHeader("Content-Type", info->file_type)
                ->writeHeader("Content-Disposition", disposition)
                ->writeHeader("Access-Control-Allow-Origin", "*")
                ->writeHeader("Cache-Control", "private, max-age=86400")
                ->end(content);
        });
    }

private:
    static std::string format_size(int64_t bytes) {
        if (bytes < 1024) return std::to_string(bytes) + " B";
        if (bytes < 1024 * 1024) return std::to_string(bytes / 1024) + " KB";
        if (bytes < 1024LL * 1024 * 1024) return std::to_string(bytes / (1024 * 1024)) + " MB";
        return std::to_string(bytes / (1024LL * 1024 * 1024)) + " GB";
    }

    static std::string random_hex(int bytes) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        std::ostringstream oss;
        for (int i = 0; i < bytes; ++i)
            oss << std::hex << std::setfill('0') << std::setw(2) << dis(gen);
        return oss.str();
    }
};
