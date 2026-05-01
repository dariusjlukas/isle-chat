#include "handlers/notification_handler.h"
#include "handlers/request_scope.h"

#include <algorithm>

template <bool SSL>
void NotificationHandler<SSL>::register_routes(uWS::TemplatedApp<SSL>& app) {
  // List notifications
  app.get("/api/notifications", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("GET", "/api/notifications");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string limit_str(req->getQuery("limit"));
    std::string offset_str(req->getQuery("offset"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    pool_.submit([this,
                  res,
                  aborted,
                  scope,
                  token = std::move(token),
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

      int limit = std::clamp(handler_utils::safe_parse_int(limit_str, 50), 1, 100);
      int offset = std::max(0, handler_utils::safe_parse_int(offset_str, 0));

      try {
        auto notifications = db.get_notifications(*user_id, limit, offset);
        int unread_count = db.get_unread_notification_count(*user_id);

        json arr = json::array();
        for (const auto& n : notifications) {
          arr.push_back(
            {{"id", n.id},
             {"user_id", n.user_id},
             {"type", n.type},
             {"source_user_id", n.source_user_id},
             {"source_username", n.source_username},
             {"channel_id", n.channel_id},
             {"channel_name", n.channel_name},
             {"message_id", n.message_id},
             {"space_id", n.space_id},
             {"content", n.content},
             {"created_at", n.created_at},
             {"is_read", n.is_read}});
        }

        auto body = json({{"notifications", arr}, {"unread_count", unread_count}}).dump();
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

  // Mark single notification as read
  app.post("/api/notifications/:id/read", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("POST", "/api/notifications/:id/read");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string notification_id(req->getParameter("id"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    // Read body (even if empty) to satisfy uWS
    std::string body;
    res->onData([this,
                 res,
                 aborted,
                 scope,
                 token = std::move(token),
                 notification_id = std::move(notification_id),
                 body = std::move(body)](std::string_view chunk, bool last) mutable {
      body.append(chunk);
      if (!last) return;

      pool_.submit([this,
                    res,
                    aborted,
                    scope,
                    token = std::move(token),
                    notification_id = std::move(notification_id)]() {
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

        try {
          db.mark_notification_read(notification_id, *user_id);
          loop_->defer([res, aborted, scope]() {
            if (*aborted) return;
            res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
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
  });

  // Mark all notifications in a channel as read
  app.post("/api/notifications/read-by-channel/:channelId", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>(
      "POST", "/api/notifications/read-by-channel/:channelId");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string channel_id(req->getParameter("channelId"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    std::string body;
    res->onData([this,
                 res,
                 aborted,
                 scope,
                 token = std::move(token),
                 channel_id = std::move(channel_id),
                 body = std::move(body)](std::string_view chunk, bool last) mutable {
      body.append(chunk);
      if (!last) return;

      pool_.submit([this,
                    res,
                    aborted,
                    scope,
                    token = std::move(token),
                    channel_id = std::move(channel_id)]() {
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

        try {
          int count = db.mark_channel_notifications_read(channel_id, *user_id);
          auto resp_body = json({{"ok", true}, {"marked_count", count}}).dump();
          loop_->defer([res, aborted, scope, resp_body = std::move(resp_body)]() {
            if (*aborted) return;
            res->writeHeader("Content-Type", "application/json")->end(resp_body);
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
  });

  // Mark all notifications as read
  app.post("/api/notifications/read-all", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("POST", "/api/notifications/read-all");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    std::string body;
    res->onData([this, res, aborted, scope, token = std::move(token), body = std::move(body)](
                  std::string_view chunk, bool last) mutable {
      body.append(chunk);
      if (!last) return;

      pool_.submit([this, res, aborted, scope, token = std::move(token)]() {
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

        try {
          db.mark_all_notifications_read(*user_id);
          loop_->defer([res, aborted, scope]() {
            if (*aborted) return;
            res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
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
  });
}

template struct NotificationHandler<false>;
template struct NotificationHandler<true>;
