#include "handlers/channel_handler.h"
#include "handlers/request_scope.h"

#include <algorithm>

using json = nlohmann::json;

template <bool SSL>
void ChannelHandler<SSL>::register_routes(uWS::TemplatedApp<SSL>& app) {
  app.get("/api/channels", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("GET", "/api/channels");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string limit_str(req->getQuery("limit"));
    std::string offset_str(req->getQuery("offset"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });

    // Reject negative offset early (400). safe_parse_int returns nullopt on parse
    // failure, in which case we fall back to the default 0.
    auto offset_parsed = handler_utils::safe_parse_int(offset_str);
    if (offset_parsed && *offset_parsed < 0) {
      res->writeStatus("400")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"offset must be non-negative"})");
      return;
    }
    int limit = std::clamp(handler_utils::safe_parse_int(limit_str, 100), 1, 500);
    int offset = offset_parsed.value_or(0);

    pool_.submit([this, res, aborted, scope, token = std::move(token), limit, offset]() {
      auto user_id_opt = db.validate_session(token);
      if (!user_id_opt) {
        loop_->defer([res, aborted, scope]() {
          if (*aborted) return;
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Unauthorized"})");
          scope->observe(401);
        });
        return;
      }
      auto user_id = *user_id_opt;

      auto user = db.find_user_by_id(user_id);
      bool is_server_admin = user && (user->role == "admin" || user->role == "owner");

      // Single SQL query: returns the user's channels (incl DMs/conversations) and,
      // for server admins, also every non-DM channel — replaces the prior pattern of
      // fetching list_user_channels + list_all_channels and merging in C++.
      auto channels = db.list_visible_channels_for_user(user_id, is_server_admin, limit, offset);

      json arr = json::array();
      for (const auto& ch : channels) {
        json members = json::array();
        auto member_list = db.get_channel_members_with_roles(ch.id);
        for (const auto& m : member_list) {
          members.push_back(
            {{"id", m.user_id},
             {"username", m.username},
             {"display_name", m.display_name},
             {"is_online", m.is_online},
             {"last_seen", m.last_seen},
             {"role", m.role}});
        }

        std::string my_role = db.get_effective_role(ch.id, user_id);

        json ch_json = {
          {"id", ch.id},
          {"name", ch.name},
          {"description", ch.description},
          {"is_direct", ch.is_direct},
          {"is_public", ch.is_public},
          {"default_role", ch.default_role},
          {"default_join", ch.default_join},
          {"created_at", ch.created_at},
          {"my_role", my_role},
          {"is_archived", ch.is_archived},
          {"members", members}};
        if (!ch.space_id.empty()) ch_json["space_id"] = ch.space_id;
        if (!ch.conversation_name.empty()) ch_json["conversation_name"] = ch.conversation_name;
        arr.push_back(ch_json);
      }
      auto body = arr.dump();
      loop_->defer([res, aborted, scope, body = std::move(body)]() {
        if (*aborted) return;
        res->writeHeader("Content-Type", "application/json")->end(body);
        scope->observe(200);
      });
    });
  });

  app.post("/api/channels", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("POST", "/api/channels");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    res->onData([this, res, aborted, scope, body = std::move(body), token = std::move(token)](
                  std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this, res, aborted, scope, body = std::move(body), token = std::move(token)]() {
        auto user_id_opt = db.validate_session(token);
        if (!user_id_opt) {
          loop_->defer([res, aborted, scope]() {
            if (*aborted) return;
            res->writeStatus("401")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Unauthorized"})");
            scope->observe(401);
          });
          return;
        }
        auto user_id = *user_id_opt;
        handle_create_channel(res, aborted, body, user_id);
      });
    });
  });

  // Register /public BEFORE /:id routes to avoid route collision
  app.get("/api/channels/public", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("GET", "/api/channels/public");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string search(req->getQuery("search"));
    std::string space_id(req->getQuery("space_id"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    pool_.submit([this,
                  res,
                  aborted,
                  scope,
                  token = std::move(token),
                  search = std::move(search),
                  space_id = std::move(space_id)]() {
      auto user_id_opt = db.validate_session(token);
      if (!user_id_opt) {
        loop_->defer([res, aborted, scope]() {
          if (*aborted) return;
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Unauthorized"})");
          scope->observe(401);
        });
        return;
      }
      auto user_id = *user_id_opt;

      std::vector<Channel> channels;

      if (!space_id.empty()) {
        // Check if user is space admin/owner or server admin/owner
        std::string space_role = db.get_space_member_role(space_id, user_id);
        auto requester = db.find_user_by_id(user_id);
        bool is_space_admin =
          (space_role == "admin" || space_role == "owner") ||
          (requester && (requester->role == "admin" || requester->role == "owner"));

        if (is_space_admin) {
          // Show ALL non-member channels in this space (including private/archived)
          channels = db.list_browsable_space_channels(space_id, user_id, search);
        } else {
          // Show only public non-member channels in this space
          auto all_public = db.list_public_channels(user_id, search);
          for (auto& ch : all_public) {
            if (ch.space_id == space_id) channels.push_back(std::move(ch));
          }
        }
      } else {
        channels = db.list_public_channels(user_id, search);
      }

      json arr = json::array();
      for (const auto& ch : channels) {
        arr.push_back(
          {{"id", ch.id},
           {"name", ch.name},
           {"description", ch.description},
           {"is_public", ch.is_public},
           {"default_role", ch.default_role},
           {"default_join", ch.default_join},
           {"is_archived", ch.is_archived},
           {"space_id", ch.space_id},
           {"created_at", ch.created_at}});
      }
      auto resp_body = arr.dump();
      loop_->defer([res, aborted, scope, resp_body = std::move(resp_body)]() {
        if (*aborted) return;
        res->writeHeader("Content-Type", "application/json")->end(resp_body);
        scope->observe(200);
      });
    });
  });

  app.get("/api/channels/:id/messages", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("GET", "/api/channels/:id/messages");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string channel_id(req->getParameter("id"));
    std::string before(req->getQuery("before"));
    std::string limit_str(req->getQuery("limit"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    pool_.submit([this,
                  res,
                  aborted,
                  scope,
                  token = std::move(token),
                  channel_id = std::move(channel_id),
                  before = std::move(before),
                  limit_str = std::move(limit_str)]() {
      auto user_id_opt = db.validate_session(token);
      if (!user_id_opt) {
        loop_->defer([res, aborted, scope]() {
          if (*aborted) return;
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Unauthorized"})");
          scope->observe(401);
        });
        return;
      }
      auto user_id = *user_id_opt;

      int limit = std::clamp(
        handler_utils::safe_parse_int(limit_str, defaults::MESSAGE_DEFAULT_LIMIT), 1, 500);

      // Server admins can view any channel's messages
      std::string role = db.get_effective_role(channel_id, user_id);
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

      auto messages = db.get_messages(channel_id, limit, before);

      // Batch-load reactions for all messages
      std::vector<std::string> msg_ids;
      for (const auto& msg : messages) msg_ids.push_back(msg.id);
      auto reactions_map = db.get_reactions_for_messages(msg_ids);

      json arr = json::array();
      for (const auto& msg : messages) {
        json m = {
          {"id", msg.id},
          {"channel_id", msg.channel_id},
          {"user_id", msg.user_id},
          {"username", msg.username},
          {"content", msg.content},
          {"created_at", msg.created_at},
          {"is_deleted", msg.is_deleted}};
        if (!msg.edited_at.empty()) m["edited_at"] = msg.edited_at;
        if (!msg.file_id.empty()) {
          m["file_id"] = msg.file_id;
          m["file_name"] = msg.file_name;
          m["file_size"] = msg.file_size;
          m["file_type"] = msg.file_type;
        }
        if (!msg.reply_to_message_id.empty()) {
          m["reply_to_message_id"] = msg.reply_to_message_id;
          m["reply_to_username"] = msg.reply_to_username;
          m["reply_to_content"] = msg.reply_to_content;
          m["reply_to_is_deleted"] = msg.reply_to_is_deleted;
        }
        if (msg.is_ai_assisted) {
          m["is_ai_assisted"] = true;
        }
        auto it = reactions_map.find(msg.id);
        if (it != reactions_map.end() && !it->second.empty()) {
          json rarr = json::array();
          for (const auto& r : it->second) {
            rarr.push_back({{"emoji", r.emoji}, {"user_id", r.user_id}, {"username", r.username}});
          }
          m["reactions"] = rarr;
        }
        arr.push_back(m);
      }
      auto resp_body = arr.dump();
      loop_->defer([res, aborted, scope, resp_body = std::move(resp_body)]() {
        if (*aborted) return;
        res->writeHeader("Content-Type", "application/json")->end(resp_body);
        scope->observe(200);
      });
    });
  });

  app.get("/api/channels/:id/read-receipts", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("GET", "/api/channels/:id/read-receipts");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string channel_id(req->getParameter("id"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    pool_.submit(
      [this, res, aborted, scope, token = std::move(token), channel_id = std::move(channel_id)]() {
        auto user_id_opt = db.validate_session(token);
        if (!user_id_opt) {
          loop_->defer([res, aborted, scope]() {
            if (*aborted) return;
            res->writeStatus("401")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Unauthorized"})");
            scope->observe(401);
          });
          return;
        }
        auto user_id = *user_id_opt;

        std::string role = db.get_effective_role(channel_id, user_id);
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

        auto receipts = db.get_channel_read_receipts(channel_id);
        json arr = json::array();
        for (const auto& r : receipts) {
          arr.push_back(
            {{"user_id", r.user_id},
             {"username", r.username},
             {"last_read_message_id", r.last_read_message_id},
             {"last_read_at", r.last_read_at}});
        }
        auto resp_body = arr.dump();
        loop_->defer([res, aborted, scope, resp_body = std::move(resp_body)]() {
          if (*aborted) return;
          res->writeHeader("Content-Type", "application/json")->end(resp_body);
          scope->observe(200);
        });
      });
  });

  app.post("/api/channels/dm", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("POST", "/api/channels/dm");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    res->onData([this, res, aborted, scope, body = std::move(body), token = std::move(token)](
                  std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this, res, aborted, scope, body = std::move(body), token = std::move(token)]() {
        auto user_id_opt = db.validate_session(token);
        if (!user_id_opt) {
          loop_->defer([res, aborted, scope]() {
            if (*aborted) return;
            res->writeStatus("401")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Unauthorized"})");
            scope->observe(401);
          });
          return;
        }
        auto user_id = *user_id_opt;
        handle_create_dm(res, aborted, body, user_id);
      });
    });
  });

  app.post("/api/channels/:id/join", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("POST", "/api/channels/:id/join");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string channel_id(req->getParameter("id"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    pool_.submit(
      [this, res, aborted, scope, token = std::move(token), channel_id = std::move(channel_id)]() {
        auto user_id_opt = db.validate_session(token);
        if (!user_id_opt) {
          loop_->defer([res, aborted, scope]() {
            if (*aborted) return;
            res->writeStatus("401")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Unauthorized"})");
            scope->observe(401);
          });
          return;
        }
        auto user_id = *user_id_opt;

        auto ch = db.find_channel_by_id(channel_id);
        if (!ch) {
          loop_->defer([res, aborted, scope]() {
            if (*aborted) return;
            res->writeStatus("404")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Channel not found"})");
            scope->observe(404);
          });
          return;
        }
        if (ch->is_direct) {
          loop_->defer([res, aborted, scope]() {
            if (*aborted) return;
            res->writeStatus("400")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Cannot join a DM channel"})");
            scope->observe(400);
          });
          return;
        }

        // Space admins/owners can join any channel in their space
        bool is_space_admin = false;
        if (!ch->space_id.empty()) {
          std::string space_role = db.get_space_member_role(ch->space_id, user_id);
          if (space_role == "admin" || space_role == "owner") {
            is_space_admin = true;
          }
        }
        // Server admins/owners can also bypass
        auto requester = db.find_user_by_id(user_id);
        if (requester && (requester->role == "admin" || requester->role == "owner")) {
          is_space_admin = true;
        }

        if (!ch->is_public && !is_space_admin) {
          loop_->defer([res, aborted, scope]() {
            if (*aborted) return;
            res->writeStatus("403")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"This is a private channel. You need an invite."})");
            scope->observe(403);
          });
          return;
        }

        std::string join_role = is_space_admin ? "admin" : ch->default_role;
        db.add_channel_member(channel_id, user_id, join_role);
        loop_->defer([this, res, aborted, scope, user_id, channel_id]() {
          ws.subscribe_user_to_channel(user_id, channel_id);
          if (*aborted) return;
          res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
          scope->observe(200);
        });
      });
  });

  // Invite user to channel
  app.post("/api/channels/:id/members", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("POST", "/api/channels/:id/members");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string channel_id(req->getParameter("id"));
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 scope,
                 token = std::move(token),
                 channel_id = std::move(channel_id),
                 body = std::move(body)](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this,
                    aborted,
                    scope,
                    res,
                    body = std::move(body),
                    token = std::move(token),
                    channel_id = std::move(channel_id)]() {
        auto user_id_opt = db.validate_session(token);
        if (!user_id_opt) {
          loop_->defer([res, aborted, scope]() {
            if (*aborted) return;
            res->writeStatus("401")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Unauthorized"})");
            scope->observe(401);
          });
          return;
        }
        auto user_id = *user_id_opt;

        std::string role = db.get_effective_role(channel_id, user_id);
        if (role != "admin") {
          loop_->defer([res, aborted, scope]() {
            if (*aborted) return;
            res->writeStatus("403")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Admin permission required"})");
            scope->observe(403);
          });
          return;
        }

        try {
          auto j = json::parse(body);
          std::string target_user_id = j.at("user_id");
          std::string member_role = j.value("role", "");

          if (member_role.empty()) {
            auto ch = db.find_channel_by_id(channel_id);
            member_role = ch ? ch->default_role : "write";
          }

          db.add_channel_member(channel_id, target_user_id, member_role);

          // Build channel data for notification
          auto ch = db.find_channel_by_id(channel_id);
          std::string notify_str;
          if (ch) {
            json members = json::array();
            auto member_list = db.get_channel_members_with_roles(channel_id);
            for (const auto& m : member_list) {
              members.push_back(
                {{"id", m.user_id},
                 {"username", m.username},
                 {"display_name", m.display_name},
                 {"is_online", m.is_online},
                 {"last_seen", m.last_seen},
                 {"role", m.role}});
            }
            json channel_data = {
              {"id", ch->id},
              {"name", ch->name},
              {"description", ch->description},
              {"is_direct", ch->is_direct},
              {"is_public", ch->is_public},
              {"default_role", ch->default_role},
              {"created_at", ch->created_at},
              {"my_role", member_role},
              {"members", members}};
            if (!ch->space_id.empty()) channel_data["space_id"] = ch->space_id;
            json notify = {{"type", "channel_added"}, {"channel", channel_data}};
            notify_str = notify.dump();
          }

          loop_->defer([this,
                        res,
                        aborted,
                        scope,
                        target_user_id,
                        channel_id,
                        notify_str = std::move(notify_str)]() {
            ws.subscribe_user_to_channel(target_user_id, channel_id);
            if (!notify_str.empty()) {
              ws.send_to_user(target_user_id, notify_str);
            }
            if (*aborted) return;
            res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
          });
        } catch (const std::exception& e) {
          auto err = json({{"error", e.what()}}).dump();
          loop_->defer([res, aborted, scope, err = std::move(err)]() {
            if (*aborted) return;
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
            scope->observe(400);
          });
        }
      });
    });
  });

  // Kick user from channel
  app.del("/api/channels/:id/members/:userId", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("DEL", "/api/channels/:id/members/:userId");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string channel_id(req->getParameter(0));
    std::string target_user_id(req->getParameter(1));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    pool_.submit([this,
                  res,
                  aborted,
                  scope,
                  token = std::move(token),
                  channel_id = std::move(channel_id),
                  target_user_id = std::move(target_user_id)]() {
      auto user_id_opt = db.validate_session(token);
      if (!user_id_opt) {
        loop_->defer([res, aborted, scope]() {
          if (*aborted) return;
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Unauthorized"})");
          scope->observe(401);
        });
        return;
      }
      auto user_id = *user_id_opt;

      std::string role = db.get_effective_role(channel_id, user_id);
      if (role != "admin") {
        loop_->defer([res, aborted, scope]() {
          if (*aborted) return;
          res->writeStatus("403")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Admin permission required"})");
          scope->observe(403);
        });
        return;
      }

      if (target_user_id == user_id) {
        loop_->defer([res, aborted, scope]() {
          if (*aborted) return;
          res->writeStatus("400")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Cannot remove yourself"})");
          scope->observe(400);
        });
        return;
      }

      db.remove_channel_member(channel_id, target_user_id);

      json notify = {{"type", "channel_removed"}, {"channel_id", channel_id}};
      auto notify_str = notify.dump();
      loop_->defer([this,
                    res,
                    aborted,
                    scope,
                    target_user_id,
                    channel_id,
                    notify_str = std::move(notify_str)]() {
        ws.unsubscribe_user_from_channel(target_user_id, channel_id);
        ws.send_to_user(target_user_id, notify_str);
        if (*aborted) return;
        res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
        scope->observe(200);
      });
    });
  });

  // Change member role
  app.put("/api/channels/:id/members/:userId", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("PUT", "/api/channels/:id/members/:userId");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string channel_id(req->getParameter(0));
    std::string target_user_id(req->getParameter(1));
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 scope,
                 token = std::move(token),
                 channel_id = std::move(channel_id),
                 target_user_id = std::move(target_user_id),
                 body = std::move(body)](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    scope,
                    body = std::move(body),
                    token = std::move(token),
                    channel_id = std::move(channel_id),
                    target_user_id = std::move(target_user_id)]() {
        auto user_id_opt = db.validate_session(token);
        if (!user_id_opt) {
          loop_->defer([res, aborted, scope]() {
            if (*aborted) return;
            res->writeStatus("401")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Unauthorized"})");
            scope->observe(401);
          });
          return;
        }
        auto user_id = *user_id_opt;

        std::string role = db.get_effective_role(channel_id, user_id);
        if (role != "admin") {
          loop_->defer([res, aborted, scope]() {
            if (*aborted) return;
            res->writeStatus("403")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Admin permission required"})");
            scope->observe(403);
          });
          return;
        }

        try {
          auto j = json::parse(body);
          std::string new_role = j.at("role");
          if (new_role != "admin" && new_role != "write" && new_role != "read") {
            loop_->defer([res, aborted, scope]() {
              if (*aborted) return;
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Invalid role. Must be admin, write, or read"})");
              scope->observe(400);
            });
            return;
          }

          std::string current_role = db.get_member_role(channel_id, target_user_id);
          int actor_rank = channel_role_rank(role);  // effective role (already "admin" if elevated)
          int target_rank = channel_role_rank(current_role);
          int new_rank = channel_role_rank(new_role);

          // Cannot promote above own rank
          if (new_rank > actor_rank) {
            loop_->defer([res, aborted, scope]() {
              if (*aborted) return;
              res->writeStatus("403")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Cannot promote above your own rank"})");
              scope->observe(403);
            });
            return;
          }

          // Cannot demote someone of equal or higher rank (unless self-demotion)
          if (new_rank < target_rank && target_rank >= actor_rank && user_id != target_user_id) {
            loop_->defer([res, aborted, scope]() {
              if (*aborted) return;
              res->writeStatus("403")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Cannot demote a user of equal or higher rank"})");
              scope->observe(403);
            });
            return;
          }

          // Check if demoting last admin
          if (current_role == "admin" && new_role != "admin") {
            int admin_count = db.count_channel_members_with_role(channel_id, "admin");
            if (admin_count <= 1) {
              loop_->defer([res, aborted, scope]() {
                if (*aborted) return;
                res->writeStatus("400")
                  ->writeHeader("Content-Type", "application/json")
                  ->end(R"({"error":"Cannot demote last admin","last_admin":true})");
                scope->observe(400);
              });
              return;
            }
          }

          db.update_member_role(channel_id, target_user_id, new_role);

          json notify = {{"type", "role_changed"}, {"channel_id", channel_id}, {"role", new_role}};
          auto notify_str = notify.dump();

          // Broadcast to all channel members so they can update their member lists
          json broadcast = {
            {"type", "member_role_changed"},
            {"channel_id", channel_id},
            {"user_id", target_user_id},
            {"role", new_role}};
          auto broadcast_str = broadcast.dump();

          loop_->defer([this,
                        res,
                        aborted,
                        scope,
                        target_user_id,
                        channel_id,
                        notify_str = std::move(notify_str),
                        broadcast_str = std::move(broadcast_str)]() {
            ws.send_to_user(target_user_id, notify_str);
            ws.broadcast_to_channel(channel_id, broadcast_str);
            if (*aborted) return;
            res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
            scope->observe(200);
          });
        } catch (const std::exception& e) {
          auto err = json({{"error", e.what()}}).dump();
          loop_->defer([res, aborted, scope, err = std::move(err)]() {
            if (*aborted) return;
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
            scope->observe(400);
          });
        }
      });
    });
  });

  // Update channel settings
  app.put("/api/channels/:id", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("PUT", "/api/channels/:id");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string channel_id(req->getParameter("id"));
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 scope,
                 token = std::move(token),
                 channel_id = std::move(channel_id),
                 body = std::move(body)](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    scope,
                    body = std::move(body),
                    token = std::move(token),
                    channel_id = std::move(channel_id)]() {
        auto user_id_opt = db.validate_session(token);
        if (!user_id_opt) {
          loop_->defer([res, aborted, scope]() {
            if (*aborted) return;
            res->writeStatus("401")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Unauthorized"})");
            scope->observe(401);
          });
          return;
        }
        auto user_id = *user_id_opt;

        std::string role = db.get_effective_role(channel_id, user_id);
        if (role != "admin") {
          loop_->defer([res, aborted, scope]() {
            if (*aborted) return;
            res->writeStatus("403")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Admin permission required"})");
            scope->observe(403);
          });
          return;
        }

        try {
          auto current = db.find_channel_by_id(channel_id);
          if (!current) {
            loop_->defer([res, aborted, scope]() {
              if (*aborted) return;
              res->writeStatus("404")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Channel not found"})");
              scope->observe(404);
            });
            return;
          }

          auto j = json::parse(body);
          std::string name = j.value("name", current->name);
          std::string description = j.value("description", current->description);
          bool is_public = j.value("is_public", current->is_public);
          std::string default_role = j.value("default_role", current->default_role);
          bool default_join = j.value("default_join", current->default_join);

          auto updated =
            db.update_channel(channel_id, name, description, is_public, default_role, default_join);

          json resp = {
            {"id", updated.id},
            {"name", updated.name},
            {"description", updated.description},
            {"is_public", updated.is_public},
            {"default_role", updated.default_role},
            {"default_join", updated.default_join}};

          // Broadcast update to channel members
          json broadcast = {{"type", "channel_updated"}, {"channel", resp}};
          auto broadcast_str = broadcast.dump();
          auto resp_body = resp.dump();

          loop_->defer([this,
                        res,
                        aborted,
                        scope,
                        channel_id,
                        broadcast_str = std::move(broadcast_str),
                        resp_body = std::move(resp_body)]() {
            ws.broadcast_to_channel(channel_id, broadcast_str);
            if (*aborted) return;
            res->writeHeader("Content-Type", "application/json")->end(resp_body);
            scope->observe(200);
          });
        } catch (const std::exception& e) {
          auto err = json({{"error", e.what()}}).dump();
          loop_->defer([res, aborted, scope, err = std::move(err)]() {
            if (*aborted) return;
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
            scope->observe(400);
          });
        }
      });
    });
  });

  // Leave channel
  app.post("/api/channels/:id/leave", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("POST", "/api/channels/:id/leave");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string channel_id(req->getParameter("id"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    pool_.submit([this,
                  res,
                  aborted,
                  scope,
                  token = std::move(token),
                  channel_id = std::move(channel_id)]() {
      auto user_id_opt = db.validate_session(token);
      if (!user_id_opt) {
        loop_->defer([res, aborted, scope]() {
          if (*aborted) return;
          res->writeStatus("401")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Unauthorized"})");
          scope->observe(401);
        });
        return;
      }
      auto user_id = *user_id_opt;

      if (!db.is_channel_member(channel_id, user_id)) {
        loop_->defer([res, aborted, scope]() {
          if (*aborted) return;
          res->writeStatus("400")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Not a member"})");
          scope->observe(400);
        });
        return;
      }

      auto ch = db.find_channel_by_id(channel_id);
      if (!ch) {
        loop_->defer([res, aborted, scope]() {
          if (*aborted) return;
          res->writeStatus("404")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Channel not found"})");
          scope->observe(404);
        });
        return;
      }

      if (ch->is_direct) {
        // DM/conversation: remove member, auto-archive if <=1 remains
        db.remove_channel_member(channel_id, user_id);

        int remaining = db.count_channel_members(channel_id);
        bool should_archive = remaining <= 1;
        if (should_archive) {
          db.archive_channel(channel_id);
        }

        // Prepare WS notifications
        std::string archived_notify_str;
        if (should_archive) {
          json archived_notify = {
            {"type", "channel_updated"}, {"channel", {{"id", channel_id}, {"is_archived", true}}}};
          archived_notify_str = archived_notify.dump();
        }

        json left_notify = {
          {"type", "member_left"}, {"channel_id", channel_id}, {"user_id", user_id}};
        auto left_notify_str = left_notify.dump();

        json removed = {
          {"type", "channel_updated"}, {"channel", {{"id", channel_id}, {"is_archived", true}}}};
        auto removed_str = removed.dump();

        loop_->defer([this,
                      res,
                      aborted,
                      scope,
                      user_id,
                      channel_id,
                      should_archive,
                      archived_notify_str = std::move(archived_notify_str),
                      left_notify_str = std::move(left_notify_str),
                      removed_str = std::move(removed_str)]() {
          ws.unsubscribe_user_from_channel(user_id, channel_id);
          if (should_archive) {
            ws.broadcast_to_channel(channel_id, archived_notify_str);
          }
          // Notify remaining members that someone left
          ws.broadcast_to_channel(channel_id, left_notify_str);
          // Tell leaving user the channel is archived for them
          ws.send_to_user(user_id, removed_str);
          if (*aborted) return;
          res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
          scope->observe(200);
        });
      } else {
        // Regular channel: check last admin (skip if already archived)
        std::string role = db.get_member_role(channel_id, user_id);
        if (role == "admin" && !ch->is_archived) {
          int admin_count = db.count_channel_members_with_role(channel_id, "admin");
          if (admin_count <= 1) {
            loop_->defer([res, aborted, scope]() {
              if (*aborted) return;
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(
                  R"({"error":"You are the last admin. Assign a new admin or archive the channel.","last_admin":true})");
              scope->observe(400);
            });
            return;
          }
        }

        db.remove_channel_member(channel_id, user_id);

        json left_notify = {
          {"type", "member_left"}, {"channel_id", channel_id}, {"user_id", user_id}};
        auto left_notify_str = left_notify.dump();

        json removed = {{"type", "channel_removed"}, {"channel_id", channel_id}};
        auto removed_str = removed.dump();

        loop_->defer([this,
                      res,
                      aborted,
                      scope,
                      user_id,
                      channel_id,
                      left_notify_str = std::move(left_notify_str),
                      removed_str = std::move(removed_str)]() {
          ws.unsubscribe_user_from_channel(user_id, channel_id);
          ws.broadcast_to_channel(channel_id, left_notify_str);
          ws.send_to_user(user_id, removed_str);
          if (*aborted) return;
          res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
          scope->observe(200);
        });
      }
    });
  });

  // Archive channel
  app.post("/api/channels/:id/archive", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("POST", "/api/channels/:id/archive");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string channel_id(req->getParameter("id"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    pool_.submit(
      [this, res, aborted, scope, token = std::move(token), channel_id = std::move(channel_id)]() {
        auto user_id_opt = db.validate_session(token);
        if (!user_id_opt) {
          loop_->defer([res, aborted, scope]() {
            if (*aborted) return;
            res->writeStatus("401")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Unauthorized"})");
            scope->observe(401);
          });
          return;
        }
        auto user_id = *user_id_opt;

        std::string role = db.get_effective_role(channel_id, user_id);
        auto user = db.find_user_by_id(user_id);
        bool is_server_privileged = user && (user->role == "admin" || user->role == "owner");

        // Check if user is space owner for the channel's space
        auto ch = db.find_channel_by_id(channel_id);
        bool is_space_owner = false;
        if (ch && !ch->space_id.empty()) {
          std::string space_role = db.get_space_member_role(ch->space_id, user_id);
          is_space_owner = (space_role == "owner");
        }

        if (role != "admin" && !is_server_privileged && !is_space_owner) {
          loop_->defer([res, aborted, scope]() {
            if (*aborted) return;
            res->writeStatus("403")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Permission denied"})");
            scope->observe(403);
          });
          return;
        }

        db.archive_channel(channel_id);
        json notify = {
          {"type", "channel_updated"}, {"channel", {{"id", channel_id}, {"is_archived", true}}}};
        auto notify_str = notify.dump();
        loop_->defer([this, res, aborted, scope, channel_id, notify_str = std::move(notify_str)]() {
          ws.broadcast_to_channel(channel_id, notify_str);
          if (*aborted) return;
          res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
          scope->observe(200);
        });
      });
  });

  // Unarchive channel
  app.post("/api/channels/:id/unarchive", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("POST", "/api/channels/:id/unarchive");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string channel_id(req->getParameter("id"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    pool_.submit(
      [this, res, aborted, scope, token = std::move(token), channel_id = std::move(channel_id)]() {
        auto user_id_opt = db.validate_session(token);
        if (!user_id_opt) {
          loop_->defer([res, aborted, scope]() {
            if (*aborted) return;
            res->writeStatus("401")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Unauthorized"})");
            scope->observe(401);
          });
          return;
        }
        auto user_id = *user_id_opt;

        std::string role = db.get_effective_role(channel_id, user_id);
        auto user = db.find_user_by_id(user_id);
        bool is_server_privileged = user && (user->role == "admin" || user->role == "owner");

        auto ch = db.find_channel_by_id(channel_id);
        bool is_space_owner = false;
        if (ch && !ch->space_id.empty()) {
          std::string space_role = db.get_space_member_role(ch->space_id, user_id);
          is_space_owner = (space_role == "owner");
          // Reject if parent space is archived
          auto sp = db.find_space_by_id(ch->space_id);
          if (sp && sp->is_archived) {
            loop_->defer([res, aborted, scope]() {
              if (*aborted) return;
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Cannot unarchive: parent space is archived"})");
              scope->observe(400);
            });
            return;
          }
        }

        if (role != "admin" && !is_server_privileged && !is_space_owner) {
          loop_->defer([res, aborted, scope]() {
            if (*aborted) return;
            res->writeStatus("403")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Permission denied"})");
            scope->observe(403);
          });
          return;
        }

        db.unarchive_channel(channel_id);
        json notify = {
          {"type", "channel_updated"}, {"channel", {{"id", channel_id}, {"is_archived", false}}}};
        auto notify_str = notify.dump();
        loop_->defer([this, res, aborted, scope, channel_id, notify_str = std::move(notify_str)]() {
          ws.broadcast_to_channel(channel_id, notify_str);
          if (*aborted) return;
          res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
          scope->observe(200);
        });
      });
  });
}

template <bool SSL>
std::string ChannelHandler<SSL>::get_user_id(uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req) {
  return validate_session_or_401(res, req, db);
}

template <bool SSL>
void ChannelHandler<SSL>::handle_create_channel(
  uWS::HttpResponse<SSL>* res,
  std::shared_ptr<bool> aborted,
  const std::string& body,
  const std::string& user_id) {
  try {
    auto j = json::parse(body);
    std::string name = j.at("name");
    std::string description = j.value("description", "");
    bool is_public = j.value("is_public", true);
    std::string default_role = j.value("default_role", "write");

    if (default_role != "admin" && default_role != "write" && default_role != "read") {
      default_role = "write";
    }

    std::vector<std::string> member_ids = {user_id};
    if (j.contains("member_ids")) {
      for (const auto& mid : j["member_ids"]) {
        std::string id = mid.get<std::string>();
        if (id != user_id) member_ids.push_back(id);
      }
    }

    auto ch =
      db.create_channel(name, description, false, user_id, member_ids, is_public, default_role);

    // Build member list for notification
    json members = json::array();
    auto member_list = db.get_channel_members_with_roles(ch.id);
    for (const auto& m : member_list) {
      members.push_back(
        {{"id", m.user_id},
         {"username", m.username},
         {"display_name", m.display_name},
         {"is_online", m.is_online},
         {"last_seen", m.last_seen},
         {"role", m.role}});
    }

    json channel_data = {
      {"id", ch.id},
      {"name", ch.name},
      {"description", ch.description},
      {"is_direct", ch.is_direct},
      {"is_public", ch.is_public},
      {"default_role", ch.default_role},
      {"default_join", ch.default_join},
      {"created_at", ch.created_at},
      {"members", members}};
    if (!ch.space_id.empty()) channel_data["space_id"] = ch.space_id;

    // Prepare per-member notifications
    auto actual_member_ids = db.get_channel_member_ids(ch.id);
    struct MemberNotify {
      std::string mid;
      std::string notify_str;
    };
    std::vector<MemberNotify> notifications;
    for (const auto& mid : actual_member_ids) {
      std::string my_role = db.get_effective_role(ch.id, mid);
      json notify_data = channel_data;
      notify_data["my_role"] = my_role;
      if (mid != user_id) {
        json notify = {{"type", "channel_added"}, {"channel", notify_data}};
        notifications.push_back({mid, notify.dump()});
      }
    }

    json resp = channel_data;
    resp["my_role"] = "admin";
    auto resp_body = resp.dump();
    auto ch_id = ch.id;

    loop_->defer([this,
                  res,
                  aborted,
                  actual_member_ids,
                  ch_id,
                  notifications = std::move(notifications),
                  resp_body = std::move(resp_body)]() {
      for (const auto& mid : actual_member_ids) {
        ws.subscribe_user_to_channel(mid, ch_id);
      }
      for (const auto& n : notifications) {
        ws.send_to_user(n.mid, n.notify_str);
      }
      // Subscribe server admins who aren't already members
      ws.subscribe_admins_to_channel(db, ch_id);
      if (*aborted) return;
      res->writeHeader("Content-Type", "application/json")->end(resp_body);
    });
  } catch (const std::exception& e) {
    auto err = json({{"error", e.what()}}).dump();
    loop_->defer([res, aborted, err = std::move(err)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
    });
  }
}

template <bool SSL>
void ChannelHandler<SSL>::handle_create_dm(
  uWS::HttpResponse<SSL>* res,
  std::shared_ptr<bool> aborted,
  const std::string& body,
  const std::string& user_id) {
  try {
    auto j = json::parse(body);
    std::string other_user_id = j.at("user_id");

    // Check if DM already exists
    auto existing = db.find_dm_channel(user_id, other_user_id);
    if (existing) {
      json resp = {
        {"id", existing->id},
        {"name", existing->name},
        {"is_direct", true},
        {"created_at", existing->created_at}};
      auto resp_body = resp.dump();
      loop_->defer([res, aborted, resp_body = std::move(resp_body)]() {
        if (*aborted) return;
        res->writeHeader("Content-Type", "application/json")->end(resp_body);
      });
      return;
    }

    auto ch = db.create_channel("", "", true, user_id, {user_id, other_user_id});

    // Build member list for the notification
    json members = json::array();
    auto member_ids = db.get_channel_member_ids(ch.id);
    for (const auto& mid : member_ids) {
      auto u = db.find_user_by_id(mid);
      if (u) {
        members.push_back(
          {{"id", u->id},
           {"username", u->username},
           {"display_name", u->display_name},
           {"is_online", u->is_online},
           {"last_seen", u->last_seen}});
      }
    }

    json channel_data = {
      {"id", ch.id},
      {"name", ch.name},
      {"description", ""},
      {"is_direct", true},
      {"is_public", false},
      {"default_role", "write"},
      {"created_at", ch.created_at},
      {"my_role", "write"},
      {"members", members}};

    json notify = {{"type", "channel_added"}, {"channel", channel_data}};
    auto notify_str = notify.dump();

    json resp = {
      {"id", ch.id}, {"name", ch.name}, {"is_direct", true}, {"created_at", ch.created_at}};
    auto resp_body = resp.dump();
    auto ch_id = ch.id;

    loop_->defer([this,
                  res,
                  aborted,
                  member_ids,
                  ch_id,
                  user_id,
                  notify_str = std::move(notify_str),
                  resp_body = std::move(resp_body)]() {
      // Notify all members via WebSocket and subscribe them to the channel
      for (const auto& mid : member_ids) {
        ws.subscribe_user_to_channel(mid, ch_id);
        if (mid != user_id) {
          ws.send_to_user(mid, notify_str);
        }
      }
      if (*aborted) return;
      res->writeHeader("Content-Type", "application/json")->end(resp_body);
    });
  } catch (const std::exception& e) {
    auto err = json({{"error", e.what()}}).dump();
    loop_->defer([res, aborted, err = std::move(err)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
    });
  }
}

template struct ChannelHandler<false>;
template struct ChannelHandler<true>;
