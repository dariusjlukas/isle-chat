#include "handlers/channel_handler.h"

using json = nlohmann::json;

template <bool SSL>
void ChannelHandler<SSL>::register_routes(uWS::TemplatedApp<SSL>& app) {
  app.get("/api/channels", [this](auto* res, auto* req) {
    std::string user_id = get_user_id(res, req);
    if (user_id.empty()) return;

    auto user = db.find_user_by_id(user_id);
    bool is_server_admin = user && (user->role == "admin" || user->role == "owner");

    auto channels = db.list_user_channels(user_id);

    // Server admins see all non-DM channels
    if (is_server_admin) {
      auto all_channels = db.list_all_channels();
      std::unordered_set<std::string> existing_ids;
      for (const auto& ch : channels) existing_ids.insert(ch.id);
      for (const auto& ch : all_channels) {
        if (existing_ids.find(ch.id) == existing_ids.end()) {
          channels.push_back(ch);
        }
      }
    }

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
    res->writeHeader("Content-Type", "application/json")->end(arr.dump());
  });

  app.post("/api/channels", [this](auto* res, auto* req) {
    auto user_id_copy = get_user_id(res, req);
    std::string body;
    res->onData([this, res, user_id = std::move(user_id_copy), body = std::move(body)](
                  std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      if (user_id.empty()) return;
      handle_create_channel(res, body, user_id);
    });
    res->onAborted([]() {});
  });

  // Register /public BEFORE /:id routes to avoid route collision
  app.get("/api/channels/public", [this](auto* res, auto* req) {
    std::string user_id = get_user_id(res, req);
    if (user_id.empty()) return;

    std::string search(req->getQuery("search"));
    std::string space_id(req->getQuery("space_id"));

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
    res->writeHeader("Content-Type", "application/json")->end(arr.dump());
  });

  app.get("/api/channels/:id/messages", [this](auto* res, auto* req) {
    std::string user_id = get_user_id(res, req);
    if (user_id.empty()) return;

    std::string channel_id(req->getParameter("id"));
    std::string before(req->getQuery("before"));
    std::string limit_str(req->getQuery("limit"));
    int limit = limit_str.empty() ? defaults::MESSAGE_DEFAULT_LIMIT : std::stoi(limit_str);

    // Server admins can view any channel's messages
    std::string role = db.get_effective_role(channel_id, user_id);
    if (role.empty()) {
      res->writeStatus("403")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Not a member of this channel"})");
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
    res->writeHeader("Content-Type", "application/json")->end(arr.dump());
  });

  app.get("/api/channels/:id/read-receipts", [this](auto* res, auto* req) {
    std::string user_id = get_user_id(res, req);
    if (user_id.empty()) return;

    std::string channel_id(req->getParameter("id"));

    std::string role = db.get_effective_role(channel_id, user_id);
    if (role.empty()) {
      res->writeStatus("403")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Not a member of this channel"})");
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
    res->writeHeader("Content-Type", "application/json")->end(arr.dump());
  });

  app.post("/api/channels/dm", [this](auto* res, auto* req) {
    auto user_id_copy = get_user_id(res, req);
    std::string body;
    res->onData([this, res, user_id = std::move(user_id_copy), body = std::move(body)](
                  std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      if (user_id.empty()) return;
      handle_create_dm(res, body, user_id);
    });
    res->onAborted([]() {});
  });

  app.post("/api/channels/:id/join", [this](auto* res, auto* req) {
    std::string user_id = get_user_id(res, req);
    if (user_id.empty()) return;
    std::string channel_id(req->getParameter("id"));

    auto ch = db.find_channel_by_id(channel_id);
    if (!ch) {
      res->writeStatus("404")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Channel not found"})");
      return;
    }
    if (ch->is_direct) {
      res->writeStatus("400")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Cannot join a DM channel"})");
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
      res->writeStatus("403")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"This is a private channel. You need an invite."})");
      return;
    }

    std::string join_role = is_space_admin ? "admin" : ch->default_role;
    db.add_channel_member(channel_id, user_id, join_role);
    ws.subscribe_user_to_channel(user_id, channel_id);
    res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
  });

  // Invite user to channel
  app.post("/api/channels/:id/members", [this](auto* res, auto* req) {
    auto user_id_copy = get_user_id(res, req);
    std::string channel_id(req->getParameter("id"));
    std::string body;
    res->onData([this,
                 res,
                 user_id = std::move(user_id_copy),
                 channel_id = std::move(channel_id),
                 body = std::move(body)](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      if (user_id.empty()) return;

      std::string role = db.get_effective_role(channel_id, user_id);
      if (role != "admin") {
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Admin permission required"})");
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
        ws.subscribe_user_to_channel(target_user_id, channel_id);

        // Build channel data for notification
        auto ch = db.find_channel_by_id(channel_id);
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
          ws.send_to_user(target_user_id, notify.dump());
        }

        res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
      } catch (const std::exception& e) {
        res->writeStatus("400")
          ->writeHeader("Content-Type", "application/json")
          ->end(json({{"error", e.what()}}).dump());
      }
    });
    res->onAborted([]() {});
  });

  // Kick user from channel
  app.del("/api/channels/:id/members/:userId", [this](auto* res, auto* req) {
    std::string user_id = get_user_id(res, req);
    if (user_id.empty()) return;

    std::string channel_id(req->getParameter(0));
    std::string target_user_id(req->getParameter(1));

    std::string role = db.get_effective_role(channel_id, user_id);
    if (role != "admin") {
      res->writeStatus("403")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Admin permission required"})");
      return;
    }

    if (target_user_id == user_id) {
      res->writeStatus("400")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Cannot remove yourself"})");
      return;
    }

    db.remove_channel_member(channel_id, target_user_id);
    ws.unsubscribe_user_from_channel(target_user_id, channel_id);

    json notify = {{"type", "channel_removed"}, {"channel_id", channel_id}};
    ws.send_to_user(target_user_id, notify.dump());

    res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
  });

  // Change member role
  app.put("/api/channels/:id/members/:userId", [this](auto* res, auto* req) {
    auto user_id_copy = get_user_id(res, req);
    std::string channel_id(req->getParameter(0));
    std::string target_user_id(req->getParameter(1));
    std::string body;
    res->onData([this,
                 res,
                 user_id = std::move(user_id_copy),
                 channel_id = std::move(channel_id),
                 target_user_id = std::move(target_user_id),
                 body = std::move(body)](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      if (user_id.empty()) return;

      std::string role = db.get_effective_role(channel_id, user_id);
      if (role != "admin") {
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Admin permission required"})");
        return;
      }

      try {
        auto j = json::parse(body);
        std::string new_role = j.at("role");
        if (new_role != "admin" && new_role != "write" && new_role != "read") {
          res->writeStatus("400")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Invalid role. Must be admin, write, or read"})");
          return;
        }

        std::string current_role = db.get_member_role(channel_id, target_user_id);
        int actor_rank = channel_role_rank(role);  // effective role (already "admin" if elevated)
        int target_rank = channel_role_rank(current_role);
        int new_rank = channel_role_rank(new_role);

        // Cannot promote above own rank
        if (new_rank > actor_rank) {
          res->writeStatus("403")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Cannot promote above your own rank"})");
          return;
        }

        // Cannot demote someone of equal or higher rank (unless self-demotion)
        if (new_rank < target_rank && target_rank >= actor_rank && user_id != target_user_id) {
          res->writeStatus("403")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Cannot demote a user of equal or higher rank"})");
          return;
        }

        // Check if demoting last admin
        if (current_role == "admin" && new_role != "admin") {
          int admin_count = db.count_channel_members_with_role(channel_id, "admin");
          if (admin_count <= 1) {
            res->writeStatus("400")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Cannot demote last admin","last_admin":true})");
            return;
          }
        }

        db.update_member_role(channel_id, target_user_id, new_role);

        json notify = {{"type", "role_changed"}, {"channel_id", channel_id}, {"role", new_role}};
        ws.send_to_user(target_user_id, notify.dump());

        // Broadcast to all channel members so they can update their member lists
        json broadcast = {
          {"type", "member_role_changed"},
          {"channel_id", channel_id},
          {"user_id", target_user_id},
          {"role", new_role}};
        ws.broadcast_to_channel(channel_id, broadcast.dump());

        res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
      } catch (const std::exception& e) {
        res->writeStatus("400")
          ->writeHeader("Content-Type", "application/json")
          ->end(json({{"error", e.what()}}).dump());
      }
    });
    res->onAborted([]() {});
  });

  // Update channel settings
  app.put("/api/channels/:id", [this](auto* res, auto* req) {
    auto user_id_copy = get_user_id(res, req);
    std::string channel_id(req->getParameter("id"));
    std::string body;
    res->onData([this,
                 res,
                 user_id = std::move(user_id_copy),
                 channel_id = std::move(channel_id),
                 body = std::move(body)](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      if (user_id.empty()) return;

      std::string role = db.get_effective_role(channel_id, user_id);
      if (role != "admin") {
        res->writeStatus("403")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Admin permission required"})");
        return;
      }

      try {
        auto current = db.find_channel_by_id(channel_id);
        if (!current) {
          res->writeStatus("404")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Channel not found"})");
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
        ws.broadcast_to_channel(channel_id, broadcast.dump());

        res->writeHeader("Content-Type", "application/json")->end(resp.dump());
      } catch (const std::exception& e) {
        res->writeStatus("400")
          ->writeHeader("Content-Type", "application/json")
          ->end(json({{"error", e.what()}}).dump());
      }
    });
    res->onAborted([]() {});
  });

  // Leave channel
  app.post("/api/channels/:id/leave", [this](auto* res, auto* req) {
    std::string user_id = get_user_id(res, req);
    if (user_id.empty()) return;
    std::string channel_id(req->getParameter("id"));

    if (!db.is_channel_member(channel_id, user_id)) {
      res->writeStatus("400")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Not a member"})");
      return;
    }

    auto ch = db.find_channel_by_id(channel_id);
    if (!ch) {
      res->writeStatus("404")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Channel not found"})");
      return;
    }

    if (ch->is_direct) {
      // DM/conversation: remove member, auto-archive if <=1 remains
      db.remove_channel_member(channel_id, user_id);
      ws.unsubscribe_user_from_channel(user_id, channel_id);

      int remaining = db.count_channel_members(channel_id);
      if (remaining <= 1) {
        db.archive_channel(channel_id);
        json archived_notify = {
          {"type", "channel_updated"}, {"channel", {{"id", channel_id}, {"is_archived", true}}}};
        ws.broadcast_to_channel(channel_id, archived_notify.dump());
      }

      // Notify remaining members that someone left
      json left_notify = {
        {"type", "member_left"}, {"channel_id", channel_id}, {"user_id", user_id}};
      ws.broadcast_to_channel(channel_id, left_notify.dump());

      // Tell leaving user the channel is archived for them
      json removed = {
        {"type", "channel_updated"}, {"channel", {{"id", channel_id}, {"is_archived", true}}}};
      ws.send_to_user(user_id, removed.dump());
    } else {
      // Regular channel: check last admin (skip if already archived)
      std::string role = db.get_member_role(channel_id, user_id);
      if (role == "admin" && !ch->is_archived) {
        int admin_count = db.count_channel_members_with_role(channel_id, "admin");
        if (admin_count <= 1) {
          res->writeStatus("400")
            ->writeHeader("Content-Type", "application/json")
            ->end(
              R"({"error":"You are the last admin. Assign a new admin or archive the channel.","last_admin":true})");
          return;
        }
      }

      db.remove_channel_member(channel_id, user_id);
      ws.unsubscribe_user_from_channel(user_id, channel_id);

      json left_notify = {
        {"type", "member_left"}, {"channel_id", channel_id}, {"user_id", user_id}};
      ws.broadcast_to_channel(channel_id, left_notify.dump());

      json removed = {{"type", "channel_removed"}, {"channel_id", channel_id}};
      ws.send_to_user(user_id, removed.dump());
    }

    res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
  });

  // Archive channel
  app.post("/api/channels/:id/archive", [this](auto* res, auto* req) {
    std::string user_id = get_user_id(res, req);
    if (user_id.empty()) return;
    std::string channel_id(req->getParameter("id"));

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
      res->writeStatus("403")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Permission denied"})");
      return;
    }

    db.archive_channel(channel_id);
    json notify = {
      {"type", "channel_updated"}, {"channel", {{"id", channel_id}, {"is_archived", true}}}};
    ws.broadcast_to_channel(channel_id, notify.dump());
    res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
  });

  // Unarchive channel
  app.post("/api/channels/:id/unarchive", [this](auto* res, auto* req) {
    std::string user_id = get_user_id(res, req);
    if (user_id.empty()) return;
    std::string channel_id(req->getParameter("id"));

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
        res->writeStatus("400")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Cannot unarchive: parent space is archived"})");
        return;
      }
    }

    if (role != "admin" && !is_server_privileged && !is_space_owner) {
      res->writeStatus("403")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Permission denied"})");
      return;
    }

    db.unarchive_channel(channel_id);
    json notify = {
      {"type", "channel_updated"}, {"channel", {{"id", channel_id}, {"is_archived", false}}}};
    ws.broadcast_to_channel(channel_id, notify.dump());
    res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
  });
}

template <bool SSL>
std::string ChannelHandler<SSL>::get_user_id(uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req) {
  return validate_session_or_401(res, req, db);
}

template <bool SSL>
void ChannelHandler<SSL>::handle_create_channel(
  uWS::HttpResponse<SSL>* res, const std::string& body, const std::string& user_id) {
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

    // Notify and subscribe all members
    auto actual_member_ids = db.get_channel_member_ids(ch.id);
    for (const auto& mid : actual_member_ids) {
      ws.subscribe_user_to_channel(mid, ch.id);
      std::string my_role = db.get_effective_role(ch.id, mid);
      json notify_data = channel_data;
      notify_data["my_role"] = my_role;
      if (mid != user_id) {
        json notify = {{"type", "channel_added"}, {"channel", notify_data}};
        ws.send_to_user(mid, notify.dump());
      }
    }

    // Subscribe server admins who aren't already members
    ws.subscribe_admins_to_channel(db, ch.id);

    json resp = channel_data;
    resp["my_role"] = "admin";
    res->writeHeader("Content-Type", "application/json")->end(resp.dump());
  } catch (const std::exception& e) {
    res->writeStatus("400")
      ->writeHeader("Content-Type", "application/json")
      ->end(json({{"error", e.what()}}).dump());
  }
}

template <bool SSL>
void ChannelHandler<SSL>::handle_create_dm(
  uWS::HttpResponse<SSL>* res, const std::string& body, const std::string& user_id) {
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
      res->writeHeader("Content-Type", "application/json")->end(resp.dump());
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

    // Notify all members via WebSocket and subscribe them to the channel
    for (const auto& mid : member_ids) {
      ws.subscribe_user_to_channel(mid, ch.id);
      if (mid != user_id) {
        json notify = {{"type", "channel_added"}, {"channel", channel_data}};
        ws.send_to_user(mid, notify.dump());
      }
    }

    json resp = {
      {"id", ch.id}, {"name", ch.name}, {"is_direct", true}, {"created_at", ch.created_at}};
    res->writeHeader("Content-Type", "application/json")->end(resp.dump());
  } catch (const std::exception& e) {
    res->writeStatus("400")
      ->writeHeader("Content-Type", "application/json")
      ->end(json({{"error", e.what()}}).dump());
  }
}

template struct ChannelHandler<false>;
template struct ChannelHandler<true>;
