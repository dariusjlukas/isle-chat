#pragma once
#include <App.h>
#include <nlohmann/json.hpp>
#include "db/database.h"
#include "handlers/handler_utils.h"

using json = nlohmann::json;

template <bool SSL>
struct NotificationHandler {
    Database& db;

    void register_routes(uWS::TemplatedApp<SSL>& app) {
        // List notifications
        app.get("/api/notifications", [this](auto* res, auto* req) {
            std::string user_id = validate_session_or_401(res, req, db);
            if (user_id.empty()) return;

            std::string limit_str(req->getQuery("limit"));
            std::string offset_str(req->getQuery("offset"));
            int limit = limit_str.empty() ? 50 : std::min(std::stoi(limit_str), 100);
            int offset = offset_str.empty() ? 0 : std::stoi(offset_str);

            try {
                auto notifications = db.get_notifications(user_id, limit, offset);
                int unread_count = db.get_unread_notification_count(user_id);

                json arr = json::array();
                for (const auto& n : notifications) {
                    arr.push_back({
                        {"id", n.id}, {"user_id", n.user_id}, {"type", n.type},
                        {"source_user_id", n.source_user_id},
                        {"source_username", n.source_username},
                        {"channel_id", n.channel_id},
                        {"channel_name", n.channel_name},
                        {"message_id", n.message_id},
                        {"space_id", n.space_id},
                        {"content", n.content},
                        {"created_at", n.created_at},
                        {"is_read", n.is_read}
                    });
                }

                json resp = {{"notifications", arr}, {"unread_count", unread_count}};
                res->writeHeader("Content-Type", "application/json")->end(resp.dump());
            } catch (const std::exception& e) {
                res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                    ->end(json({{"error", e.what()}}).dump());
            }
        });

        // Mark single notification as read
        app.post("/api/notifications/:id/read", [this](auto* res, auto* req) {
            std::string user_id = validate_session_or_401(res, req, db);
            if (user_id.empty()) return;

            std::string notification_id(req->getParameter("id"));

            res->onAborted([]() {});
            // Read body (even if empty) to satisfy uWS
            std::string body;
            res->onData([res, this, user_id, notification_id, body = std::move(body)](std::string_view chunk, bool last) mutable {
                body.append(chunk);
                if (!last) return;

                try {
                    db.mark_notification_read(notification_id, user_id);
                    res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
                } catch (const std::exception& e) {
                    res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                        ->end(json({{"error", e.what()}}).dump());
                }
            });
        });

        // Mark all notifications in a channel as read
        app.post("/api/notifications/read-by-channel/:channelId", [this](auto* res, auto* req) {
            std::string user_id = validate_session_or_401(res, req, db);
            if (user_id.empty()) return;

            std::string channel_id(req->getParameter("channelId"));

            res->onAborted([]() {});
            std::string body;
            res->onData([res, this, user_id, channel_id, body = std::move(body)](std::string_view chunk, bool last) mutable {
                body.append(chunk);
                if (!last) return;

                try {
                    int count = db.mark_channel_notifications_read(channel_id, user_id);
                    json resp = {{"ok", true}, {"marked_count", count}};
                    res->writeHeader("Content-Type", "application/json")->end(resp.dump());
                } catch (const std::exception& e) {
                    res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                        ->end(json({{"error", e.what()}}).dump());
                }
            });
        });

        // Mark all notifications as read
        app.post("/api/notifications/read-all", [this](auto* res, auto* req) {
            std::string user_id = validate_session_or_401(res, req, db);
            if (user_id.empty()) return;

            res->onAborted([]() {});
            std::string body;
            res->onData([res, this, user_id, body = std::move(body)](std::string_view chunk, bool last) mutable {
                body.append(chunk);
                if (!last) return;

                try {
                    db.mark_all_notifications_read(user_id);
                    res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
                } catch (const std::exception& e) {
                    res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                        ->end(json({{"error", e.what()}}).dump());
                }
            });
        });
    }
};
