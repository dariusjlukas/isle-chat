#include "handlers/space_handler.h"

#include <algorithm>

#include "handlers/cors_utils.h"
#include "handlers/format_utils.h"

using json = nlohmann::json;

template <bool SSL>
void SpaceHandler<SSL>::register_routes(uWS::TemplatedApp<SSL>& app) {
  // List user's spaces
  app.get("/api/spaces", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    std::string limit_str(req->getQuery("limit"));
    std::string offset_str(req->getQuery("offset"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });

    // Reject negative offset early (400).
    auto offset_parsed = handler_utils::safe_parse_int(offset_str);
    if (offset_parsed && *offset_parsed < 0) {
      res->writeStatus("400")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"offset must be non-negative"})");
      return;
    }
    int limit = std::clamp(handler_utils::safe_parse_int(limit_str, 100), 1, 500);
    int offset = offset_parsed.value_or(0);

    pool_.submit([this, res, aborted, token = std::move(token), limit, offset, origin]() {
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
      auto user_id = *user_id_opt;

      auto user = db.find_user_by_id(user_id);
      bool is_server_admin = user && (user->role == "admin" || user->role == "owner");

      // Single SQL query: spaces the user is a member of, plus all spaces if admin.
      // Replaces fetch-all + merge pattern.
      auto spaces = db.list_visible_spaces_for_user(user_id, is_server_admin, limit, offset);

      // Personal space: auto-create if enabled, filter out if disabled
      bool personal_spaces_enabled =
        db.get_setting("personal_spaces_enabled").value_or("false") == "true";
      if (personal_spaces_enabled) {
        bool has_personal = false;
        for (const auto& sp : spaces) {
          if (sp.is_personal && sp.personal_owner_id == user_id) {
            has_personal = true;
            break;
          }
        }
        if (!has_personal && user) {
          auto ps = db.get_or_create_personal_space(user_id, user->display_name);
          db.sync_personal_space_tools(ps.id);
          spaces.insert(spaces.begin(), ps);
        }
        // Sync tools for existing personal space
        for (auto& sp : spaces) {
          if (sp.is_personal && sp.personal_owner_id == user_id) {
            db.sync_personal_space_tools(sp.id);
            break;
          }
        }
      }

      json arr = json::array();
      for (const auto& sp : spaces) {
        // Hide personal spaces when feature is disabled (unless it's an admin viewing)
        if (sp.is_personal && !personal_spaces_enabled && sp.personal_owner_id != user_id) continue;
        if (sp.is_personal && !personal_spaces_enabled && !is_server_admin) continue;
        auto members = db.get_space_members_with_roles(sp.id);
        json members_arr = json::array();
        for (const auto& m : members) {
          members_arr.push_back(
            {{"id", m.user_id},
             {"username", m.username},
             {"display_name", m.display_name},
             {"is_online", m.is_online},
             {"last_seen", m.last_seen},
             {"role", m.role}});
        }
        std::string my_role = db.get_space_member_role(sp.id, user_id);
        if (my_role.empty() && is_server_admin) my_role = "admin";

        auto tools = db.get_space_tools(sp.id);
        json tools_arr = json::array();
        for (const auto& t : tools) tools_arr.push_back(t);

        json space_json = {
          {"id", sp.id},
          {"name", sp.name},
          {"description", sp.description},
          {"is_public", sp.is_public},
          {"default_role", sp.default_role},
          {"created_at", sp.created_at},
          {"is_archived", sp.is_archived},
          {"avatar_file_id", sp.avatar_file_id},
          {"profile_color", sp.profile_color},
          {"is_personal", sp.is_personal},
          {"personal_owner_id", sp.personal_owner_id},
          {"my_role", my_role},
          {"members", members_arr},
          {"enabled_tools", tools_arr}};

        // For personal spaces, include which tools the admin allows
        if (sp.is_personal) {
          json allowed = json::array();
          const std::string tool_names[] = {
            "files", "calendar", "tasks", "wiki", "minigames", "sandbox"};
          for (const auto& t : tool_names) {
            if (db.get_setting("personal_spaces_" + t + "_enabled").value_or("true") != "false") {
              allowed.push_back(t);
            }
          }
          space_json["allowed_tools"] = allowed;
        }

        arr.push_back(space_json);
      }
      auto resp_body = arr.dump();
      loop_->defer([res, aborted, resp_body = std::move(resp_body), origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(resp_body);
      });
    });
  });

  // Browse public spaces
  app.get("/api/spaces/public", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    std::string search(req->getQuery("search"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit(
      [this, res, aborted, token = std::move(token), search = std::move(search), origin]() {
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
        auto user_id = *user_id_opt;

        auto spaces = db.list_public_spaces(user_id, search);
        json arr = json::array();
        for (const auto& sp : spaces) {
          if (sp.is_personal) continue;  // Exclude personal spaces from public listing
          arr.push_back(
            {{"id", sp.id},
             {"name", sp.name},
             {"description", sp.description},
             {"is_public", sp.is_public},
             {"default_role", sp.default_role},
             {"is_archived", sp.is_archived},
             {"avatar_file_id", sp.avatar_file_id},
             {"profile_color", sp.profile_color},
             {"created_at", sp.created_at}});
        }
        auto resp_body = arr.dump();
        loop_->defer([res, aborted, resp_body = std::move(resp_body), origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeHeader("Content-Type", "application/json")->end(resp_body);
        });
      });
  });

  // Create space
  app.post("/api/spaces", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    res->onData([this, res, aborted, token = std::move(token), body = std::move(body), origin](
                  std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit(
        [this, res, aborted, body = std::move(body), token = std::move(token), origin]() {
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
          auto user_id = *user_id_opt;

          try {
            // Only server admins/owners can create spaces
            auto creator = db.find_user_by_id(user_id);
            if (!creator || (creator->role != "admin" && creator->role != "owner")) {
              loop_->defer([res, aborted, origin]() {
                if (*aborted) return;
                res->writeStatus("403")
                  ->writeHeader("Content-Type", "application/json")
                  ->end(R"({"error":"Only server admins can create spaces"})");
              });
              return;
            }

            auto j = json::parse(body);
            std::string name = j.at("name");
            std::string description = j.value("description", "");
            bool is_public = j.value("is_public", true);
            std::string default_role = j.value("default_role", "user");

            auto sp = db.create_space(name, description, is_public, user_id, default_role);

            auto members = db.get_space_members_with_roles(sp.id);
            json members_arr = json::array();
            for (const auto& m : members) {
              members_arr.push_back(
                {{"id", m.user_id},
                 {"username", m.username},
                 {"display_name", m.display_name},
                 {"is_online", m.is_online},
                 {"last_seen", m.last_seen},
                 {"role", m.role}});
            }

            json resp = {
              {"id", sp.id},
              {"name", sp.name},
              {"description", sp.description},
              {"is_public", sp.is_public},
              {"default_role", sp.default_role},
              {"created_at", sp.created_at},
              {"is_archived", sp.is_archived},
              {"avatar_file_id", sp.avatar_file_id},
              {"profile_color", sp.profile_color},
              {"is_personal", sp.is_personal},
              {"personal_owner_id", sp.personal_owner_id},
              {"my_role", "owner"},
              {"members", members_arr}};

            auto resp_body = resp.dump();
            auto sp_id = sp.id;

            loop_->defer([this, res, aborted, sp_id, resp_body = std::move(resp_body), origin]() {
              // Notify admins about new space
              ws.subscribe_admins_to_space(db, sp_id);
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeHeader("Content-Type", "application/json")->end(resp_body);
            });
          } catch (const std::exception& e) {
            auto err = json({{"error", e.what()}}).dump();
            loop_->defer([res, aborted, err = std::move(err), origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
            });
          }
        });
    });
  });

  // Get space details
  app.get("/api/spaces/:id", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    std::string space_id(req->getParameter("id"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit(
      [this, res, aborted, token = std::move(token), space_id = std::move(space_id), origin]() {
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
        auto user_id = *user_id_opt;

        auto sp = db.find_space_by_id(space_id);
        if (!sp) {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("404")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Space not found"})");
          });
          return;
        }

        auto members = db.get_space_members_with_roles(space_id);
        json members_arr = json::array();
        for (const auto& m : members) {
          members_arr.push_back(
            {{"id", m.user_id},
             {"username", m.username},
             {"display_name", m.display_name},
             {"is_online", m.is_online},
             {"last_seen", m.last_seen},
             {"role", m.role}});
        }
        std::string my_role = db.get_space_member_role(space_id, user_id);
        auto user = db.find_user_by_id(user_id);
        if (my_role.empty() && user && (user->role == "admin" || user->role == "owner"))
          my_role = "admin";

        json resp = {
          {"id", sp->id},
          {"name", sp->name},
          {"description", sp->description},
          {"is_public", sp->is_public},
          {"default_role", sp->default_role},
          {"created_at", sp->created_at},
          {"is_archived", sp->is_archived},
          {"avatar_file_id", sp->avatar_file_id},
          {"profile_color", sp->profile_color},
          {"is_personal", sp->is_personal},
          {"personal_owner_id", sp->personal_owner_id},
          {"my_role", my_role},
          {"members", members_arr}};

        auto resp_body = resp.dump();
        loop_->defer([res, aborted, resp_body = std::move(resp_body), origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeHeader("Content-Type", "application/json")->end(resp_body);
        });
      });
  });

  // Update space settings
  app.put("/api/spaces/:id", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    std::string space_id(req->getParameter("id"));
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 token = std::move(token),
                 space_id = std::move(space_id),
                 body = std::move(body),
                 origin](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    body = std::move(body),
                    token = std::move(token),
                    space_id = std::move(space_id),
                    origin]() {
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
        auto user_id = *user_id_opt;

        std::string role = db.get_space_member_role(space_id, user_id);
        auto user = db.find_user_by_id(user_id);
        if (
          role != "admin" && role != "owner" &&
          !(user && (user->role == "admin" || user->role == "owner"))) {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("403")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Admin permission required"})");
          });
          return;
        }

        try {
          auto current = db.find_space_by_id(space_id);
          if (!current) {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("404")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Space not found"})");
            });
            return;
          }

          auto j = json::parse(body);
          std::string name = j.value("name", current->name);
          std::string description = j.value("description", current->description);
          bool is_public = j.value("is_public", current->is_public);
          std::string default_role = j.value("default_role", current->default_role);
          std::string profile_color = j.value("profile_color", current->profile_color);

          auto updated =
            db.update_space(space_id, name, description, is_public, default_role, profile_color);

          json resp = {
            {"id", updated.id},
            {"name", updated.name},
            {"description", updated.description},
            {"is_public", updated.is_public},
            {"default_role", updated.default_role},
            {"avatar_file_id", updated.avatar_file_id},
            {"profile_color", updated.profile_color},
            {"is_personal", updated.is_personal},
            {"personal_owner_id", updated.personal_owner_id}};

          // Broadcast to space members
          json broadcast = {{"type", "space_updated"}, {"space", resp}};
          auto broadcast_str = broadcast.dump();
          auto resp_body = resp.dump();

          loop_->defer([this,
                        res,
                        aborted,
                        space_id,
                        broadcast_str = std::move(broadcast_str),
                        resp_body = std::move(resp_body),
                        origin]() {
            ws.broadcast_to_space(space_id, broadcast_str);
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(resp_body);
          });
        } catch (const std::exception& e) {
          auto err = json({{"error", e.what()}}).dump();
          loop_->defer([res, aborted, err = std::move(err), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
          });
        }
      });
    });
  });

  // Join public space
  app.post("/api/spaces/:id/join", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    std::string space_id(req->getParameter("id"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit(
      [this, res, aborted, token = std::move(token), space_id = std::move(space_id), origin]() {
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
        auto user_id = *user_id_opt;

        auto sp = db.find_space_by_id(space_id);
        if (!sp) {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("404")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Space not found"})");
          });
          return;
        }
        if (!sp->is_public) {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("403")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"This is a private space. You need an invite."})");
          });
          return;
        }

        db.add_space_member(space_id, user_id, sp->default_role);

        // Auto-join default channels and prepare notifications
        auto default_channels = db.get_default_join_channels(space_id);
        struct ChannelNotify {
          std::string ch_id;
          std::string notify_str;
        };
        std::vector<ChannelNotify> channel_notifies;

        for (const auto& ch : default_channels) {
          if (!db.is_channel_member(ch.id, user_id)) {
            db.add_channel_member(ch.id, user_id, ch.default_role);

            // Send channel_added to the new member
            auto member_list = db.get_channel_members_with_roles(ch.id);
            json members = json::array();
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
            json ch_data = {
              {"id", ch.id},
              {"name", ch.name},
              {"description", ch.description},
              {"is_direct", ch.is_direct},
              {"is_public", ch.is_public},
              {"default_role", ch.default_role},
              {"default_join", ch.default_join},
              {"space_id", ch.space_id},
              {"is_archived", ch.is_archived},
              {"created_at", ch.created_at},
              {"my_role", my_role},
              {"members", members}};
            json notify = {{"type", "channel_added"}, {"channel", ch_data}};
            channel_notifies.push_back({ch.id, notify.dump()});
          }
        }

        loop_->defer([this,
                      res,
                      aborted,
                      user_id,
                      space_id,
                      channel_notifies = std::move(channel_notifies),
                      origin]() {
          ws.subscribe_user_to_space(user_id, space_id);
          for (const auto& cn : channel_notifies) {
            ws.subscribe_user_to_channel(user_id, cn.ch_id);
            ws.send_to_user(user_id, cn.notify_str);
          }
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
        });
      });
  });

  // Add member to space
  app.post("/api/spaces/:id/members", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    std::string space_id(req->getParameter("id"));
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 token = std::move(token),
                 space_id = std::move(space_id),
                 body = std::move(body),
                 origin](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    body = std::move(body),
                    token = std::move(token),
                    space_id = std::move(space_id),
                    origin]() {
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
        auto user_id = *user_id_opt;

        // Block invitations to personal spaces
        auto space_check_invite = db.find_space_by_id(space_id);
        if (space_check_invite && space_check_invite->is_personal) {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Cannot invite members to a personal space"})");
          });
          return;
        }

        std::string role = db.get_space_member_role(space_id, user_id);
        auto user = db.find_user_by_id(user_id);
        if (
          role != "admin" && role != "owner" &&
          !(user && (user->role == "admin" || user->role == "owner"))) {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("403")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Admin permission required"})");
          });
          return;
        }

        try {
          auto j = json::parse(body);
          std::string target_user_id = j.at("user_id");
          auto sp = db.find_space_by_id(space_id);
          std::string member_role = j.value("role", sp ? sp->default_role : "user");

          if (db.is_space_member(space_id, target_user_id)) {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"User is already a member of this space"})");
            });
            return;
          }
          if (db.has_pending_space_invite(space_id, target_user_id)) {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"An invite is already pending for this user"})");
            });
            return;
          }

          auto invite_id = db.create_space_invite(space_id, target_user_id, user_id, member_role);
          auto inviter = db.find_user_by_id(user_id);

          // Send invite notification to target user
          json invite_data = {
            {"id", invite_id},
            {"space_id", space_id},
            {"space_name", sp ? sp->name : ""},
            {"invited_by_username", inviter ? inviter->username : ""},
            {"role", member_role},
            {"created_at", ""}};
          json notify = {{"type", "space_invite"}, {"invite", invite_data}};
          auto notify_str = notify.dump();

          // Create bell notification for the invite
          std::string notif_content = "Invited you to " + (sp ? sp->name : "a space");
          auto nid =
            db.create_notification(target_user_id, "space_invite", user_id, "", "", notif_content);
          json notif = {
            {"type", "new_notification"},
            {"notification",
             {{"id", nid},
              {"user_id", target_user_id},
              {"type", "space_invite"},
              {"source_user_id", user_id},
              {"source_username", inviter ? inviter->username : ""},
              {"channel_id", ""},
              {"channel_name", ""},
              {"message_id", ""},
              {"space_id", space_id},
              {"content", notif_content},
              {"created_at", ""},
              {"is_read", false}}}};
          auto notif_str = notif.dump();

          loop_->defer([this,
                        res,
                        aborted,
                        target_user_id,
                        notify_str = std::move(notify_str),
                        notif_str = std::move(notif_str),
                        origin]() {
            ws.send_to_user(target_user_id, notify_str);
            ws.send_to_user(target_user_id, notif_str);
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
          });
        } catch (const std::exception& e) {
          auto err = json({{"error", e.what()}}).dump();
          loop_->defer([res, aborted, err = std::move(err), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
          });
        }
      });
    });
  });

  // Remove member from space
  app.del("/api/spaces/:id/members/:userId", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    std::string space_id(req->getParameter(0));
    std::string target_user_id(req->getParameter(1));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit([this,
                  res,
                  aborted,
                  token = std::move(token),
                  space_id = std::move(space_id),
                  target_user_id = std::move(target_user_id),
                  origin]() {
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
      auto user_id = *user_id_opt;

      std::string role = db.get_space_member_role(space_id, user_id);
      auto user = db.find_user_by_id(user_id);
      if (
        role != "admin" && role != "owner" &&
        !(user && (user->role == "admin" || user->role == "owner"))) {
        loop_->defer([res, aborted, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("403")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Admin permission required"})");
        });
        return;
      }

      // Collect channel ids the user is a member of in this space BEFORE removing
      // them from the space (remove_space_member cascades the channel_member
      // delete in the DB, but the WS subscriptions live in process memory and
      // must be cleaned up explicitly). P1.6: missing this caused kicked users
      // to keep receiving channel broadcasts until reconnect.
      std::vector<std::string> space_channel_ids;
      auto target_channels = db.list_user_channels(target_user_id);
      for (const auto& ch : target_channels) {
        if (ch.space_id == space_id) {
          space_channel_ids.push_back(ch.id);
        }
      }

      db.remove_space_member(space_id, target_user_id);

      json notify = {{"type", "space_removed"}, {"space_id", space_id}};
      auto notify_str = notify.dump();

      loop_->defer([this,
                    res,
                    aborted,
                    target_user_id,
                    space_id,
                    space_channel_ids = std::move(space_channel_ids),
                    notify_str = std::move(notify_str),
                    origin]() {
        for (const auto& ch_id : space_channel_ids) {
          ws.unsubscribe_user_from_channel(target_user_id, ch_id);
        }
        ws.unsubscribe_user_from_space(target_user_id, space_id);
        ws.send_to_user(target_user_id, notify_str);
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
      });
    });
  });

  // Change space member role
  app.put("/api/spaces/:id/members/:userId", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    std::string space_id(req->getParameter(0));
    std::string target_user_id(req->getParameter(1));
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 token = std::move(token),
                 space_id = std::move(space_id),
                 target_user_id = std::move(target_user_id),
                 body = std::move(body),
                 origin](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    body = std::move(body),
                    token = std::move(token),
                    space_id = std::move(space_id),
                    target_user_id = std::move(target_user_id),
                    origin]() {
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
        auto user_id = *user_id_opt;

        std::string role = db.get_space_member_role(space_id, user_id);
        auto user = db.find_user_by_id(user_id);
        if (
          role != "admin" && role != "owner" &&
          !(user && (user->role == "admin" || user->role == "owner"))) {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("403")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Admin permission required"})");
          });
          return;
        }

        try {
          auto j = json::parse(body);
          std::string new_role = j.at("role");
          if (new_role != "owner" && new_role != "admin" && new_role != "user") {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Invalid role"})");
            });
            return;
          }

          // Actor's effective rank is the higher of space role and server role
          int actor_space_rank = space_role_rank(role);
          if (user && user->role == "owner")
            actor_space_rank = space_role_rank("owner");
          else if (user && user->role == "admin" && actor_space_rank < space_role_rank("admin"))
            actor_space_rank = space_role_rank("admin");

          std::string current_role = db.get_space_member_role(space_id, target_user_id);
          int target_rank = space_role_rank(current_role);
          int new_rank = space_role_rank(new_role);

          // Cannot promote anyone to a rank above your own
          if (new_rank > actor_space_rank) {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("403")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Cannot promote above your own rank"})");
            });
            return;
          }

          // Cannot demote someone of equal or higher rank (unless self-demotion)
          if (
            new_rank < target_rank && target_rank >= actor_space_rank &&
            user_id != target_user_id) {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("403")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Cannot demote a user of equal or higher rank"})");
            });
            return;
          }
          if (current_role == "owner" && new_role != "owner") {
            int owner_count = db.count_space_members_with_role(space_id, "owner");
            if (owner_count <= 1) {
              loop_->defer([res, aborted, origin]() {
                if (*aborted) return;
                cors::apply(res, origin);
                res->writeStatus("400")
                  ->writeHeader("Content-Type", "application/json")
                  ->end(R"({"error":"Cannot demote last owner","last_owner":true})");
              });
              return;
            }
          }

          db.update_space_member_role(space_id, target_user_id, new_role);

          // Notify the target user of their new space role
          json notify = {
            {"type", "space_role_changed"}, {"space_id", space_id}, {"role", new_role}};
          auto notify_str = notify.dump();

          // Broadcast to all space members so they can update their member lists
          json broadcast = {
            {"type", "space_member_role_changed"},
            {"space_id", space_id},
            {"user_id", target_user_id},
            {"role", new_role}};
          auto broadcast_str = broadcast.dump();

          loop_->defer([this,
                        res,
                        aborted,
                        target_user_id,
                        space_id,
                        notify_str = std::move(notify_str),
                        broadcast_str = std::move(broadcast_str),
                        origin]() {
            ws.send_to_user(target_user_id, notify_str);
            ws.broadcast_to_space(space_id, broadcast_str);
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
          });
        } catch (const std::exception& e) {
          auto err = json({{"error", e.what()}}).dump();
          loop_->defer([res, aborted, err = std::move(err), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
          });
        }
      });
    });
  });

  // List channels in space
  app.get("/api/spaces/:id/channels", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    std::string space_id(req->getParameter("id"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit(
      [this, res, aborted, token = std::move(token), space_id = std::move(space_id), origin]() {
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
        auto user_id = *user_id_opt;

        auto channels = db.list_user_channels(user_id);
        json arr = json::array();
        for (const auto& ch : channels) {
          if (ch.space_id != space_id) continue;
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
          arr.push_back(
            {{"id", ch.id},
             {"name", ch.name},
             {"description", ch.description},
             {"is_direct", ch.is_direct},
             {"is_public", ch.is_public},
             {"default_role", ch.default_role},
             {"default_join", ch.default_join},
             {"space_id", ch.space_id},
             {"is_archived", ch.is_archived},
             {"created_at", ch.created_at},
             {"my_role", my_role},
             {"members", members}});
        }
        auto resp_body = arr.dump();
        loop_->defer([res, aborted, resp_body = std::move(resp_body), origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeHeader("Content-Type", "application/json")->end(resp_body);
        });
      });
  });

  // Create channel in space
  app.post("/api/spaces/:id/channels", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    std::string space_id(req->getParameter("id"));
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 token = std::move(token),
                 space_id = std::move(space_id),
                 body = std::move(body),
                 origin](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    body = std::move(body),
                    token = std::move(token),
                    space_id = std::move(space_id),
                    origin]() {
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
        auto user_id = *user_id_opt;

        // Block channel creation in personal spaces
        auto space_check = db.find_space_by_id(space_id);
        if (space_check && space_check->is_personal) {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Personal spaces cannot have channels"})");
          });
          return;
        }

        // Must be a space admin/owner or server admin/owner
        auto u = db.find_user_by_id(user_id);
        bool is_server_admin = u && (u->role == "admin" || u->role == "owner");
        if (!is_server_admin) {
          std::string space_role = db.get_space_member_role(space_id, user_id);
          if (space_role != "admin" && space_role != "owner") {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("403")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Only space admins and owners can create channels"})");
            });
            return;
          }
        }

        try {
          auto j = json::parse(body);
          std::string name = j.at("name");
          std::string description = j.value("description", "");
          bool is_public = j.value("is_public", true);
          std::string default_role = j.value("default_role", "write");
          bool default_join = j.value("default_join", false);

          std::vector<std::string> member_ids = {user_id};
          if (j.contains("member_ids")) {
            for (const auto& mid : j["member_ids"]) {
              std::string id = mid.get<std::string>();
              if (id != user_id) member_ids.push_back(id);
            }
          }

          auto ch = db.create_channel(
            name,
            description,
            false,
            user_id,
            member_ids,
            is_public,
            default_role,
            space_id,
            default_join);

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
            {"space_id", ch.space_id},
            {"is_archived", ch.is_archived},
            {"created_at", ch.created_at},
            {"members", members}};

          // For public space channels, auto-add all space members
          if (is_public) {
            auto space_members = db.get_space_members_with_roles(space_id);
            for (const auto& sm : space_members) {
              if (!db.is_channel_member(ch.id, sm.user_id)) {
                db.add_channel_member(ch.id, sm.user_id, ch.default_role);
              }
            }
            // Refresh members list for the response
            auto updated_members = db.get_channel_members_with_roles(ch.id);
            members = json::array();
            for (const auto& m : updated_members) {
              members.push_back(
                {{"id", m.user_id},
                 {"username", m.username},
                 {"display_name", m.display_name},
                 {"is_online", m.is_online},
                 {"last_seen", m.last_seen},
                 {"role", m.role}});
            }
            channel_data["members"] = members;
          }

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
                        resp_body = std::move(resp_body),
                        origin]() {
            for (const auto& mid : actual_member_ids) {
              ws.subscribe_user_to_channel(mid, ch_id);
            }
            for (const auto& n : notifications) {
              ws.send_to_user(n.mid, n.notify_str);
            }
            ws.subscribe_admins_to_channel(db, ch_id);
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(resp_body);
          });
        } catch (const std::exception& e) {
          auto err = json({{"error", e.what()}}).dump();
          loop_->defer([res, aborted, err = std::move(err), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
          });
        }
      });
    });
  });

  // Leave space
  app.post("/api/spaces/:id/leave", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    std::string space_id(req->getParameter("id"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit([this,
                  res,
                  aborted,
                  token = std::move(token),
                  space_id = std::move(space_id),
                  origin]() {
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
      auto user_id = *user_id_opt;

      // Block leaving personal spaces
      auto space_leave_check = db.find_space_by_id(space_id);
      if (space_leave_check && space_leave_check->is_personal) {
        loop_->defer([res, aborted, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("400")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Cannot leave your personal space"})");
        });
        return;
      }

      if (!db.is_space_member(space_id, user_id)) {
        loop_->defer([res, aborted, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("400")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Not a member"})");
        });
        return;
      }

      std::string role = db.get_space_member_role(space_id, user_id);
      auto sp = db.find_space_by_id(space_id);
      if (role == "owner" && sp && !sp->is_archived) {
        int owner_count = db.count_space_members_with_role(space_id, "owner");
        if (owner_count <= 1) {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")
              ->writeHeader("Content-Type", "application/json")
              ->end(
                R"({"error":"You are the last owner. Assign a new owner or archive the space.","last_owner":true})");
          });
          return;
        }
      }

      // Remove from all space channels and prepare WS notifications
      auto channels = db.list_user_channels(user_id);
      struct ChannelLeave {
        std::string ch_id;
        std::string left_notify_str;
      };
      std::vector<ChannelLeave> channel_leaves;
      for (const auto& ch : channels) {
        if (ch.space_id == space_id) {
          db.remove_channel_member(ch.id, user_id);
          json left_notify = {{"type", "member_left"}, {"channel_id", ch.id}, {"user_id", user_id}};
          channel_leaves.push_back({ch.id, left_notify.dump()});
        }
      }

      db.remove_space_member(space_id, user_id);

      json left_notify = {
        {"type", "space_member_left"}, {"space_id", space_id}, {"user_id", user_id}};
      auto left_notify_str = left_notify.dump();

      json removed = {{"type", "space_removed"}, {"space_id", space_id}};
      auto removed_str = removed.dump();

      loop_->defer([this,
                    res,
                    aborted,
                    user_id,
                    space_id,
                    channel_leaves = std::move(channel_leaves),
                    left_notify_str = std::move(left_notify_str),
                    removed_str = std::move(removed_str),
                    origin]() {
        for (const auto& cl : channel_leaves) {
          ws.unsubscribe_user_from_channel(user_id, cl.ch_id);
          ws.broadcast_to_channel(cl.ch_id, cl.left_notify_str);
        }
        ws.unsubscribe_user_from_space(user_id, space_id);
        ws.broadcast_to_space(space_id, left_notify_str);
        ws.send_to_user(user_id, removed_str);
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
      });
    });
  });

  // Archive space
  app.post("/api/spaces/:id/archive", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    std::string space_id(req->getParameter("id"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit(
      [this, res, aborted, token = std::move(token), space_id = std::move(space_id), origin]() {
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
        auto user_id = *user_id_opt;

        // Block archiving personal spaces
        auto space_archive_check = db.find_space_by_id(space_id);
        if (space_archive_check && space_archive_check->is_personal) {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Cannot archive a personal space"})");
          });
          return;
        }

        std::string role = db.get_space_member_role(space_id, user_id);
        auto user = db.find_user_by_id(user_id);
        bool is_server_privileged = user && (user->role == "admin" || user->role == "owner");

        if (role != "owner" && !is_server_privileged) {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("403")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Space owner or server admin required"})");
          });
          return;
        }

        db.archive_space(space_id);

        json notify = {
          {"type", "space_updated"}, {"space", {{"id", space_id}, {"is_archived", true}}}};
        auto notify_str = notify.dump();

        loop_->defer([this, res, aborted, space_id, notify_str = std::move(notify_str), origin]() {
          ws.broadcast_to_space(space_id, notify_str);
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
        });
      });
  });

  // Unarchive space
  app.post("/api/spaces/:id/unarchive", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    std::string space_id(req->getParameter("id"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit(
      [this, res, aborted, token = std::move(token), space_id = std::move(space_id), origin]() {
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
        auto user_id = *user_id_opt;

        std::string role = db.get_space_member_role(space_id, user_id);
        auto user = db.find_user_by_id(user_id);
        bool is_server_privileged = user && (user->role == "admin" || user->role == "owner");

        if (role != "owner" && !is_server_privileged) {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("403")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Space owner or server admin required"})");
          });
          return;
        }

        // Reject if server is archived
        if (db.is_server_archived()) {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Cannot unarchive: server is archived"})");
          });
          return;
        }

        db.unarchive_space(space_id);

        json notify = {
          {"type", "space_updated"}, {"space", {{"id", space_id}, {"is_archived", false}}}};
        auto notify_str = notify.dump();

        loop_->defer([this, res, aborted, space_id, notify_str = std::move(notify_str), origin]() {
          ws.broadcast_to_space(space_id, notify_str);
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
        });
      });
  });

  // --- Conversation routes ---

  // List conversations
  app.get("/api/conversations", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit([this, res, aborted, token = std::move(token), origin]() {
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
      auto user_id = *user_id_opt;

      auto conversations = db.list_user_conversations(user_id);
      json arr = json::array();
      for (const auto& ch : conversations) {
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
        arr.push_back(
          {{"id", ch.id},
           {"name", ch.conversation_name},
           {"is_direct", true},
           {"is_archived", ch.is_archived},
           {"created_at", ch.created_at},
           {"my_role", "write"},
           {"members", members}});
      }
      auto resp_body = arr.dump();
      loop_->defer([res, aborted, resp_body = std::move(resp_body), origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(resp_body);
      });
    });
  });

  // Create conversation
  app.post("/api/conversations", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    res->onData([this, res, aborted, token = std::move(token), body = std::move(body), origin](
                  std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit(
        [this, res, aborted, body = std::move(body), token = std::move(token), origin]() {
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
          auto user_id = *user_id_opt;

          try {
            auto j = json::parse(body);
            std::string name = j.value("name", "");
            std::vector<std::string> member_ids = {user_id};
            if (j.contains("member_ids")) {
              for (const auto& mid : j["member_ids"]) {
                std::string id = mid.get<std::string>();
                if (id != user_id) member_ids.push_back(id);
              }
            }

            // For 1:1, check if existing conversation exists
            if (member_ids.size() == 2 && name.empty()) {
              auto existing = db.find_dm_channel(user_id, member_ids[1]);
              if (existing) {
                json members = json::array();
                auto ml = db.get_channel_members_with_roles(existing->id);
                for (const auto& m : ml) {
                  members.push_back(
                    {{"id", m.user_id},
                     {"username", m.username},
                     {"display_name", m.display_name},
                     {"is_online", m.is_online},
                     {"last_seen", m.last_seen},
                     {"role", m.role}});
                }
                json resp = {
                  {"id", existing->id},
                  {"name", existing->conversation_name},
                  {"is_direct", true},
                  {"created_at", existing->created_at},
                  {"my_role", "write"},
                  {"members", members}};
                auto resp_body = resp.dump();
                loop_->defer([res, aborted, resp_body = std::move(resp_body), origin]() {
                  if (*aborted) return;
                  cors::apply(res, origin);
                  res->writeHeader("Content-Type", "application/json")->end(resp_body);
                });
                return;
              }
            }

            auto ch = db.create_conversation(user_id, member_ids, name);

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
              {"name", ch.conversation_name},
              {"description", ""},
              {"is_direct", true},
              {"is_public", false},
              {"default_role", "write"},
              {"created_at", ch.created_at},
              {"my_role", "write"},
              {"members", members}};

            json notify = {{"type", "channel_added"}, {"channel", channel_data}};
            auto notify_str = notify.dump();
            auto resp_body = channel_data.dump();
            auto ch_id = ch.id;

            // Notify and subscribe all members
            loop_->defer([this,
                          res,
                          aborted,
                          member_ids,
                          ch_id,
                          user_id,
                          notify_str = std::move(notify_str),
                          resp_body = std::move(resp_body),
                          origin]() {
              for (const auto& mid : member_ids) {
                ws.subscribe_user_to_channel(mid, ch_id);
                if (mid != user_id) {
                  ws.send_to_user(mid, notify_str);
                }
              }
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeHeader("Content-Type", "application/json")->end(resp_body);
            });
          } catch (const std::exception& e) {
            auto err = json({{"error", e.what()}}).dump();
            loop_->defer([res, aborted, err = std::move(err), origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
            });
          }
        });
    });
  });

  // Add member to conversation
  app.post("/api/conversations/:id/members", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    std::string channel_id(req->getParameter("id"));
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 token = std::move(token),
                 channel_id = std::move(channel_id),
                 body = std::move(body),
                 origin](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    body = std::move(body),
                    token = std::move(token),
                    channel_id = std::move(channel_id),
                    origin]() {
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
        auto user_id = *user_id_opt;

        // Must be a member of the conversation
        if (!db.is_channel_member(channel_id, user_id)) {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("403")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Not a member of this conversation"})");
          });
          return;
        }

        try {
          auto j = json::parse(body);
          std::string target_user_id = j.at("user_id");
          db.add_conversation_member(channel_id, target_user_id);

          // Build notification
          auto ch = db.find_channel_by_id(channel_id);
          std::string notify_str;
          std::string member_notify_str;
          if (ch) {
            json members = json::array();
            auto ml = db.get_channel_members_with_roles(channel_id);
            for (const auto& m : ml) {
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
              {"name", ch->conversation_name},
              {"description", ""},
              {"is_direct", true},
              {"is_public", false},
              {"default_role", "write"},
              {"created_at", ch->created_at},
              {"my_role", "write"},
              {"members", members}};
            json notify = {{"type", "channel_added"}, {"channel", channel_data}};
            notify_str = notify.dump();

            // Notify existing members about new member
            json member_notify = {
              {"type", "conversation_member_added"},
              {"channel_id", channel_id},
              {"members", members}};
            member_notify_str = member_notify.dump();
          }

          loop_->defer([this,
                        res,
                        aborted,
                        target_user_id,
                        channel_id,
                        notify_str = std::move(notify_str),
                        member_notify_str = std::move(member_notify_str),
                        origin]() {
            ws.subscribe_user_to_channel(target_user_id, channel_id);
            if (!notify_str.empty()) {
              ws.send_to_user(target_user_id, notify_str);
            }
            if (!member_notify_str.empty()) {
              ws.broadcast_to_channel(channel_id, member_notify_str);
            }
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
          });
        } catch (const std::exception& e) {
          auto err = json({{"error", e.what()}}).dump();
          loop_->defer([res, aborted, err = std::move(err), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
          });
        }
      });
    });
  });

  // Rename conversation
  app.put("/api/conversations/:id", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    std::string channel_id(req->getParameter("id"));
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 token = std::move(token),
                 channel_id = std::move(channel_id),
                 body = std::move(body),
                 origin](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    body = std::move(body),
                    token = std::move(token),
                    channel_id = std::move(channel_id),
                    origin]() {
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
        auto user_id = *user_id_opt;

        if (!db.is_channel_member(channel_id, user_id)) {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("403")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Not a member"})");
          });
          return;
        }

        try {
          auto j = json::parse(body);
          std::string name = j.value("name", "");
          db.rename_conversation(channel_id, name);

          json broadcast = {
            {"type", "conversation_renamed"}, {"channel_id", channel_id}, {"name", name}};
          auto broadcast_str = broadcast.dump();

          loop_->defer(
            [this, res, aborted, channel_id, broadcast_str = std::move(broadcast_str), origin]() {
              ws.broadcast_to_channel(channel_id, broadcast_str);
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
            });
        } catch (const std::exception& e) {
          auto err = json({{"error", e.what()}}).dump();
          loop_->defer([res, aborted, err = std::move(err), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
          });
        }
      });
    });
  });

  // List pending space invites for the authenticated user
  app.get("/api/space-invites", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit([this, res, aborted, token = std::move(token), origin]() {
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
      auto user_id = *user_id_opt;

      auto invites = db.list_pending_space_invites(user_id);
      json arr = json::array();
      for (const auto& inv : invites) {
        arr.push_back(
          {{"id", inv.id},
           {"space_id", inv.space_id},
           {"space_name", inv.space_name},
           {"invited_by_username", inv.invited_by_username},
           {"role", inv.role},
           {"created_at", inv.created_at}});
      }
      auto resp_body = arr.dump();
      loop_->defer([res, aborted, resp_body = std::move(resp_body), origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(resp_body);
      });
    });
  });

  // Accept space invite
  app.post("/api/space-invites/:id/accept", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    std::string invite_id(req->getParameter("id"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit(
      [this, res, aborted, token = std::move(token), invite_id = std::move(invite_id), origin]() {
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
        auto user_id = *user_id_opt;

        auto invite = db.get_space_invite(invite_id);
        if (!invite || invite->invited_user_id != user_id) {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("404")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Invite not found"})");
          });
          return;
        }
        if (invite->status != "pending") {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Invite is no longer pending"})");
          });
          return;
        }

        db.update_space_invite_status(invite_id, "accepted");
        db.add_space_member(invite->space_id, user_id, invite->role);

        // Auto-join default channels and prepare notifications
        auto default_channels = db.get_default_join_channels(invite->space_id);
        struct ChannelNotify {
          std::string ch_id;
          std::string notify_str;
        };
        std::vector<ChannelNotify> channel_notifies;

        for (const auto& ch : default_channels) {
          if (!db.is_channel_member(ch.id, user_id)) {
            db.add_channel_member(ch.id, user_id, ch.default_role);

            auto ch_members = db.get_channel_members_with_roles(ch.id);
            json ch_members_arr = json::array();
            for (const auto& m : ch_members) {
              ch_members_arr.push_back(
                {{"id", m.user_id},
                 {"username", m.username},
                 {"display_name", m.display_name},
                 {"is_online", m.is_online},
                 {"last_seen", m.last_seen},
                 {"role", m.role}});
            }
            std::string my_role = db.get_effective_role(ch.id, user_id);
            json ch_data = {
              {"id", ch.id},
              {"name", ch.name},
              {"description", ch.description},
              {"is_direct", ch.is_direct},
              {"is_public", ch.is_public},
              {"default_role", ch.default_role},
              {"default_join", ch.default_join},
              {"space_id", ch.space_id},
              {"is_archived", ch.is_archived},
              {"created_at", ch.created_at},
              {"my_role", my_role},
              {"members", ch_members_arr}};
            json ch_notify = {{"type", "channel_added"}, {"channel", ch_data}};
            channel_notifies.push_back({ch.id, ch_notify.dump()});
          }
        }

        // Send space_added to the accepting user
        std::string space_notify_str;
        std::string space_update_str;
        auto sp = db.find_space_by_id(invite->space_id);
        if (sp) {
          auto members = db.get_space_members_with_roles(invite->space_id);
          json members_arr = json::array();
          for (const auto& m : members) {
            members_arr.push_back(
              {{"id", m.user_id},
               {"username", m.username},
               {"display_name", m.display_name},
               {"is_online", m.is_online},
               {"last_seen", m.last_seen},
               {"role", m.role}});
          }
          json space_data = {
            {"id", sp->id},
            {"name", sp->name},
            {"description", sp->description},
            {"is_public", sp->is_public},
            {"default_role", sp->default_role},
            {"created_at", sp->created_at},
            {"is_archived", sp->is_archived},
            {"avatar_file_id", sp->avatar_file_id},
            {"profile_color", sp->profile_color},
            {"is_personal", sp->is_personal},
            {"personal_owner_id", sp->personal_owner_id},
            {"my_role", invite->role},
            {"members", members_arr}};
          json notify = {{"type", "space_added"}, {"space", space_data}};
          space_notify_str = notify.dump();

          // Notify existing space members about the new member
          json update = {
            {"type", "space_updated"}, {"space", {{"id", sp->id}, {"members", members_arr}}}};
          space_update_str = update.dump();
        }

        auto invite_space_id = invite->space_id;
        loop_->defer([this,
                      res,
                      aborted,
                      user_id,
                      invite_space_id,
                      channel_notifies = std::move(channel_notifies),
                      space_notify_str = std::move(space_notify_str),
                      space_update_str = std::move(space_update_str),
                      origin]() {
          ws.subscribe_user_to_space(user_id, invite_space_id);
          for (const auto& cn : channel_notifies) {
            ws.subscribe_user_to_channel(user_id, cn.ch_id);
            ws.send_to_user(user_id, cn.notify_str);
          }
          if (!space_notify_str.empty()) {
            ws.send_to_user(user_id, space_notify_str);
          }
          if (!space_update_str.empty()) {
            ws.broadcast_to_space(invite_space_id, space_update_str);
          }
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
        });
      });
  });

  // Decline space invite
  app.post("/api/space-invites/:id/decline", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    std::string invite_id(req->getParameter("id"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit(
      [this, res, aborted, token = std::move(token), invite_id = std::move(invite_id), origin]() {
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
        auto user_id = *user_id_opt;

        auto invite = db.get_space_invite(invite_id);
        if (!invite || invite->invited_user_id != user_id) {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("404")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Invite not found"})");
          });
          return;
        }
        if (invite->status != "pending") {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Invite is no longer pending"})");
          });
          return;
        }

        db.update_space_invite_status(invite_id, "declined");
        loop_->defer([res, aborted, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
        });
      });
  });

  // --- Space avatar upload ---
  app.post("/api/spaces/:id/avatar", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    std::string space_id(req->getParameter("id"));
    std::string content_type(req->getQuery("content_type"));
    if (content_type.empty()) content_type = "image/png";

    if (content_type.find("image/") != 0) {
      res->writeStatus("400")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Only image files are allowed"})");
      return;
    }

    auto body = std::make_shared<std::string>();
    int64_t max_size = 50 * 1024 * 1024;

    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    res->onData([this,
                 aborted,
                 res,
                 body,
                 max_size,
                 token = std::move(token),
                 space_id = std::move(space_id),
                 content_type = std::move(content_type),
                 origin](std::string_view data, bool last) mutable {
      body->append(data);

      if (static_cast<int64_t>(body->size()) > max_size) {
        res->writeStatus("413")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"json({"error":"Image too large (max 20MB)"})json");
        return;
      }

      if (!last) return;

      auto body_copy = std::make_shared<std::string>(std::move(*body));
      pool_.submit([this,
                    res,
                    aborted,
                    body_copy,
                    token = std::move(token),
                    space_id = std::move(space_id),
                    content_type = std::move(content_type),
                    origin]() {
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
        auto user_id = *user_id_opt;

        // Check permissions
        std::string role = db.get_space_member_role(space_id, user_id);
        auto user = db.find_user_by_id(user_id);
        if (
          role != "admin" && role != "owner" &&
          !(user && (user->role == "admin" || user->role == "owner"))) {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("403")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Admin permission required"})");
          });
          return;
        }

        try {
          // Delete old avatar file if exists
          auto current = db.find_space_by_id(space_id);
          if (current && !current->avatar_file_id.empty()) {
            std::string old_path = config.upload_dir + "/" + current->avatar_file_id;
            std::filesystem::remove(old_path);
          }

          // Generate file ID and save
          std::string file_id = format_utils::random_hex(32);
          std::string path = config.upload_dir + "/" + file_id;
          std::ofstream out(path, std::ios::binary);
          if (!out) {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              res->writeStatus("500")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Failed to save image"})");
            });
            return;
          }
          out.write(body_copy->data(), body_copy->size());
          out.close();

          db.set_space_avatar(space_id, file_id);
          auto updated = db.find_space_by_id(space_id);

          json resp = {
            {"id", updated->id},
            {"name", updated->name},
            {"description", updated->description},
            {"is_public", updated->is_public},
            {"default_role", updated->default_role},
            {"avatar_file_id", updated->avatar_file_id},
            {"profile_color", updated->profile_color},
            {"is_personal", updated->is_personal},
            {"personal_owner_id", updated->personal_owner_id}};

          // Broadcast to space members
          json broadcast = {{"type", "space_updated"}, {"space", resp}};
          auto broadcast_str = broadcast.dump();
          auto resp_body = resp.dump();

          loop_->defer([this,
                        res,
                        aborted,
                        space_id,
                        broadcast_str = std::move(broadcast_str),
                        resp_body = std::move(resp_body),
                        origin]() {
            ws.broadcast_to_space(space_id, broadcast_str);
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(resp_body);
          });
        } catch (const std::exception& e) {
          auto err = json({{"error", e.what()}}).dump();
          loop_->defer([res, aborted, err = std::move(err), origin]() {
            if (*aborted) return;
            res->writeStatus("500")->writeHeader("Content-Type", "application/json")->end(err);
          });
        }
      });
    });
  });

  // --- Space avatar delete ---
  app.del("/api/spaces/:id/avatar", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    std::string space_id(req->getParameter("id"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit(
      [this, res, aborted, token = std::move(token), space_id = std::move(space_id), origin]() {
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
        auto user_id = *user_id_opt;

        // Check permissions
        std::string role = db.get_space_member_role(space_id, user_id);
        auto user = db.find_user_by_id(user_id);
        if (
          role != "admin" && role != "owner" &&
          !(user && (user->role == "admin" || user->role == "owner"))) {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("403")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Admin permission required"})");
          });
          return;
        }

        try {
          auto current = db.find_space_by_id(space_id);
          if (current && !current->avatar_file_id.empty()) {
            std::string old_path = config.upload_dir + "/" + current->avatar_file_id;
            std::filesystem::remove(old_path);
          }

          db.clear_space_avatar(space_id);
          auto updated = db.find_space_by_id(space_id);

          json resp = {
            {"id", updated->id},
            {"name", updated->name},
            {"description", updated->description},
            {"is_public", updated->is_public},
            {"default_role", updated->default_role},
            {"avatar_file_id", updated->avatar_file_id},
            {"profile_color", updated->profile_color},
            {"is_personal", updated->is_personal},
            {"personal_owner_id", updated->personal_owner_id}};

          // Broadcast to space members
          json broadcast = {{"type", "space_updated"}, {"space", resp}};
          auto broadcast_str = broadcast.dump();
          auto resp_body = resp.dump();

          loop_->defer([this,
                        res,
                        aborted,
                        space_id,
                        broadcast_str = std::move(broadcast_str),
                        resp_body = std::move(resp_body),
                        origin]() {
            ws.broadcast_to_space(space_id, broadcast_str);
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(resp_body);
          });
        } catch (const std::exception& e) {
          auto err = json({{"error", e.what()}}).dump();
          loop_->defer([res, aborted, err = std::move(err), origin]() {
            if (*aborted) return;
            res->writeStatus("500")->writeHeader("Content-Type", "application/json")->end(err);
          });
        }
      });
  });

  // Get enabled tools for a space
  app.get("/api/spaces/:id/tools", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    std::string space_id(req->getParameter("id"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit(
      [this, res, aborted, token = std::move(token), space_id = std::move(space_id), origin]() {
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
        auto user_id = *user_id_opt;

        if (!db.is_space_member(space_id, user_id)) {
          auto user = db.find_user_by_id(user_id);
          if (!user || (user->role != "admin" && user->role != "owner")) {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              res->writeStatus("403")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Not a member of this space"})");
            });
            return;
          }
        }

        auto tools = db.get_space_tools(space_id);
        json arr = json::array();
        for (const auto& t : tools) arr.push_back(t);
        auto resp_body = arr.dump();
        loop_->defer([res, aborted, resp_body = std::move(resp_body), origin]() {
          if (*aborted) return;
          res->writeHeader("Content-Type", "application/json")->end(resp_body);
        });
      });
  });

  // Enable/disable a tool for a space (admin/owner only)
  app.put("/api/spaces/:id/tools", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    std::string space_id_copy(req->getParameter("id"));
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 token = std::move(token),
                 space_id = std::move(space_id_copy),
                 body = std::move(body),
                 origin](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    body = std::move(body),
                    token = std::move(token),
                    space_id = std::move(space_id),
                    origin]() {
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
        auto user_id = *user_id_opt;

        try {
          auto space_tools_check = db.find_space_by_id(space_id);
          bool is_personal_space = space_tools_check && space_tools_check->is_personal;

          if (!is_personal_space) {
            // Regular spaces: check space admin/owner or server admin/owner
            std::string space_role = db.get_space_member_role(space_id, user_id);
            auto user = db.find_user_by_id(user_id);
            bool is_server_admin = user && (user->role == "admin" || user->role == "owner");
            if (space_role != "admin" && space_role != "owner" && !is_server_admin) {
              loop_->defer([res, aborted, origin]() {
                if (*aborted) return;
                res->writeStatus("403")
                  ->writeHeader("Content-Type", "application/json")
                  ->end(R"({"error":"Admin access required"})");
              });
              return;
            }
          } else {
            // Personal spaces: only the owner can toggle
            if (space_tools_check->personal_owner_id != user_id) {
              loop_->defer([res, aborted, origin]() {
                if (*aborted) return;
                res->writeStatus("403")
                  ->writeHeader("Content-Type", "application/json")
                  ->end(R"({"error":"Only the personal space owner can toggle tools"})");
              });
              return;
            }
          }

          auto j = json::parse(body);
          std::string tool = j.at("tool").get<std::string>();
          bool enabled = j.at("enabled").get<bool>();

          // Validate tool name
          if (
            tool != "files" && tool != "calendar" && tool != "tasks" && tool != "wiki" &&
            tool != "minigames" && tool != "sandbox") {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Unknown tool"})");
            });
            return;
          }

          // Personal spaces: can only enable tools the admin has allowed
          if (is_personal_space && enabled) {
            bool admin_allows =
              db.get_setting("personal_spaces_" + tool + "_enabled").value_or("true") != "false";
            if (!admin_allows) {
              loop_->defer([res, aborted, origin]() {
                if (*aborted) return;
                res->writeStatus("400")
                  ->writeHeader("Content-Type", "application/json")
                  ->end(R"({"error":"This tool is not allowed by server administrators"})");
              });
              return;
            }
          }

          if (enabled) {
            db.enable_space_tool(space_id, tool, user_id);
          } else {
            db.disable_space_tool(space_id, tool);
          }

          auto tools = db.get_space_tools(space_id);
          json tools_arr = json::array();
          for (const auto& t : tools) tools_arr.push_back(t);

          json resp = {{"ok", true}, {"enabled_tools", tools_arr}};
          auto resp_body = resp.dump();
          loop_->defer([res, aborted, resp_body = std::move(resp_body), origin]() {
            if (*aborted) return;
            res->writeHeader("Content-Type", "application/json")->end(resp_body);
          });
        } catch (const std::exception& e) {
          auto err = json({{"error", e.what()}}).dump();
          loop_->defer([res, aborted, err = std::move(err), origin]() {
            if (*aborted) return;
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
          });
        }
      });
    });
  });

  // Shared with me -- resources from personal spaces shared with current user
  app.get("/api/shared-with-me", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit([this, res, aborted, token = std::move(token), origin]() {
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
      auto user_id = *user_id_opt;

      auto resources = db.list_shared_with_user(user_id);
      json files = json::array();
      json wiki_pages = json::array();
      json calendar_events = json::array();
      json task_boards = json::array();

      for (const auto& r : resources) {
        json item = {
          {"id", r.id},
          {"name", r.name},
          {"space_id", r.space_id},
          {"owner_username", r.owner_username},
          {"permission", r.permission}};
        if (r.resource_type == "file")
          files.push_back(item);
        else if (r.resource_type == "wiki_page")
          wiki_pages.push_back(item);
        else if (r.resource_type == "calendar")
          calendar_events.push_back(item);
        else if (r.resource_type == "task_board")
          task_boards.push_back(item);
      }

      json resp = {
        {"files", files},
        {"wiki_pages", wiki_pages},
        {"calendar_events", calendar_events},
        {"task_boards", task_boards}};
      auto resp_body = resp.dump();
      loop_->defer([res, aborted, resp_body = std::move(resp_body), origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(resp_body);
      });
    });
  });
}

template <bool SSL>
std::string SpaceHandler<SSL>::get_user_id(uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req) {
  return validate_session_or_401(res, req, db);
}

template struct SpaceHandler<false>;
template struct SpaceHandler<true>;
