#include "handlers/admin_handler.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <pqxx/pqxx>
#include <sstream>
#include <thread>
#include "auth/password.h"
#include "auth/webauthn.h"
#include "handlers/admin_approval_utils.h"
#include "handlers/admin_settings_utils.h"
#include "handlers/format_utils.h"
#include "handlers/request_scope.h"

namespace {

// Read /proc/meminfo and extract a value in kB by key
int64_t read_meminfo_kb(const std::string& key) {
  std::ifstream f("/proc/meminfo");
  std::string line;
  while (std::getline(f, line)) {
    if (line.rfind(key, 0) == 0) {
      std::istringstream iss(line.substr(key.size()));
      int64_t val = 0;
      iss >> val;
      return val;
    }
  }
  return -1;
}

// Parse /proc/stat to get total and idle jiffies
struct CpuJiffies {
  int64_t total = 0;
  int64_t idle = 0;
};

CpuJiffies read_cpu_jiffies() {
  std::ifstream f("/proc/stat");
  std::string line;
  if (std::getline(f, line) && line.rfind("cpu ", 0) == 0) {
    std::istringstream iss(line.substr(4));
    int64_t user, nice, system, idle, iowait, irq, softirq, steal;
    user = nice = system = idle = iowait = irq = softirq = steal = 0;
    iss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
    CpuJiffies j;
    j.idle = idle + iowait;
    j.total = user + nice + system + idle + iowait + irq + softirq + steal;
    return j;
  }
  return {};
}

// Parse /proc/net/dev for cumulative bytes rx/tx (skip loopback)
struct NetCounters {
  int64_t rx_bytes = 0;
  int64_t tx_bytes = 0;
};

NetCounters read_net_counters() {
  std::ifstream f("/proc/net/dev");
  std::string line;
  NetCounters total;
  // Skip the two header lines
  std::getline(f, line);
  std::getline(f, line);
  while (std::getline(f, line)) {
    auto colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string iface = line.substr(0, colon);
    // Trim whitespace from interface name
    auto start = iface.find_first_not_of(' ');
    if (start != std::string::npos) iface = iface.substr(start);
    if (iface == "lo") continue;  // skip loopback

    std::istringstream iss(line.substr(colon + 1));
    int64_t rx_bytes, rx_packets, rx_errs, rx_drop, rx_fifo, rx_frame, rx_compressed, rx_multicast;
    int64_t tx_bytes;
    iss >> rx_bytes >> rx_packets >> rx_errs >> rx_drop >> rx_fifo >> rx_frame >> rx_compressed >>
      rx_multicast >> tx_bytes;
    total.rx_bytes += rx_bytes;
    total.tx_bytes += tx_bytes;
  }
  return total;
}

}  // anonymous namespace

using json = nlohmann::json;

template <bool SSL>
void AdminHandler<SSL>::register_routes(uWS::TemplatedApp<SSL>& app) {
  app.post("/api/admin/invites", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("POST", "/api/admin/invites");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    res->onData([this, res, aborted, scope, token = std::move(token), body = std::move(body)](
                  std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this, res, aborted, scope, body = std::move(body), token = std::move(token)]() {
        auto user_id = get_admin_id(res, token, aborted);
        if (user_id.empty()) return;

        int expiry = defaults::INVITE_EXPIRY_HOURS;
        int max_uses = 1;
        try {
          auto j = json::parse(body);
          expiry = j.value("expiry_hours", defaults::INVITE_EXPIRY_HOURS);
          if (expiry < 1) expiry = 1;
          if (expiry > defaults::MAX_INVITE_EXPIRY_HOURS)
            expiry = defaults::MAX_INVITE_EXPIRY_HOURS;
          max_uses = j.value("max_uses", 1);
          if (max_uses < 0) max_uses = 0;
        } catch (...) {}

        auto invite_token = db.create_invite(user_id, expiry, max_uses);
        json resp = {{"token", invite_token}};
        auto resp_str = resp.dump();
        loop_->defer([res, aborted, scope, resp_str = std::move(resp_str)]() {
          if (*aborted) return;
          res->writeHeader("Content-Type", "application/json")->end(resp_str);
          scope->observe(200);
        });
      });
    });
  });

  app.get("/api/admin/invites", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("GET", "/api/admin/invites");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    pool_.submit([this, res, aborted, scope, token = std::move(token)]() {
      auto user_id = get_admin_id(res, token, aborted);
      if (user_id.empty()) return;

      auto invites = db.list_invites();
      json arr = json::array();
      for (const auto& inv : invites) {
        json obj = {
          {"id", inv.id},
          {"token", inv.token},
          {"created_by", inv.created_by_username},
          {"used", inv.used},
          {"expires_at", inv.expires_at},
          {"created_at", inv.created_at},
          {"max_uses", inv.max_uses},
          {"use_count", inv.use_count}};
        json uses_arr = json::array();
        for (const auto& u : inv.uses) {
          uses_arr.push_back({{"username", u.username}, {"used_at", u.used_at}});
        }
        obj["uses"] = uses_arr;
        arr.push_back(obj);
      }
      auto resp_str = arr.dump();
      loop_->defer([res, aborted, scope, resp_str = std::move(resp_str)]() {
        if (*aborted) return;
        res->writeHeader("Content-Type", "application/json")->end(resp_str);
        scope->observe(200);
      });
    });
  });

  app.del("/api/admin/invites/:id", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("DEL", "/api/admin/invites/:id");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    auto invite_id = std::string(req->getParameter("id"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    pool_.submit(
      [this, res, aborted, scope, token = std::move(token), invite_id = std::move(invite_id)]() {
        auto user_id = get_admin_id(res, token, aborted);
        if (user_id.empty()) return;

        bool deleted = db.revoke_invite(invite_id);
        if (!deleted) {
          loop_->defer([res, aborted, scope]() {
            if (*aborted) return;
            res->writeStatus("404")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Invite not found or already used"})");
            scope->observe(404);
          });
          return;
        }
        loop_->defer([res, aborted, scope]() {
          if (*aborted) return;
          res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
          scope->observe(200);
        });
      });
  });

  app.get("/api/admin/requests", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("GET", "/api/admin/requests");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    pool_.submit([this, res, aborted, scope, token = std::move(token)]() {
      auto user_id = get_admin_id(res, token, aborted);
      if (user_id.empty()) return;

      auto requests = db.list_pending_requests();
      json arr = json::array();
      for (const auto& r : requests) {
        arr.push_back(
          {{"id", r.id},
           {"username", r.username},
           {"display_name", r.display_name},
           {"auth_method", r.auth_method},
           {"status", r.status},
           {"created_at", r.created_at}});
      }
      auto resp_str = arr.dump();
      loop_->defer([res, aborted, scope, resp_str = std::move(resp_str)]() {
        if (*aborted) return;
        res->writeHeader("Content-Type", "application/json")->end(resp_str);
        scope->observe(200);
      });
    });
  });

  app.post("/api/admin/requests/:id/approve", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("POST", "/api/admin/requests/:id/approve");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    auto request_id = std::string(req->getParameter("id"));
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 scope,
                 token = std::move(token),
                 request_id = std::move(request_id),
                 body = std::move(body)](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    scope,
                    token = std::move(token),
                    request_id = std::move(request_id)]() {
        auto admin_id = get_admin_id(res, token, aborted);
        if (admin_id.empty()) return;
        handle_approve(res, request_id, admin_id, aborted);
      });
    });
  });

  app.post("/api/admin/requests/:id/deny", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("POST", "/api/admin/requests/:id/deny");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    auto request_id = std::string(req->getParameter("id"));
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 scope,
                 token = std::move(token),
                 request_id = std::move(request_id),
                 body = std::move(body)](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    scope,
                    token = std::move(token),
                    request_id = std::move(request_id)]() {
        auto admin_id = get_admin_id(res, token, aborted);
        if (admin_id.empty()) return;

        db.update_join_request(request_id, "denied", admin_id);
        loop_->defer([res, aborted, scope]() {
          if (*aborted) return;
          res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
          scope->observe(200);
        });
      });
    });
  });

  app.get("/api/admin/settings", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("GET", "/api/admin/settings");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    pool_.submit([this, res, aborted, scope, token = std::move(token)]() {
      auto user_id = get_owner_id(res, token, aborted);
      if (user_id.empty()) return;

      json resp = build_settings_response();
      auto resp_str = resp.dump();
      loop_->defer([res, aborted, scope, resp_str = std::move(resp_str)]() {
        if (*aborted) return;
        res->writeHeader("Content-Type", "application/json")->end(resp_str);
        scope->observe(200);
      });
    });
  });

  app.put("/api/admin/settings", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("PUT", "/api/admin/settings");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    res->onData([this, res, aborted, scope, token = std::move(token), body = std::move(body)](
                  std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this, res, aborted, scope, token = std::move(token), body = std::move(body)]() {
        auto admin_id = get_owner_id(res, token, aborted);
        if (admin_id.empty()) return;
        save_settings(res, body, aborted);
      });
    });
  });

  app.post("/api/admin/setup", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("POST", "/api/admin/setup");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    res->onData([this, res, aborted, scope, token = std::move(token), body = std::move(body)](
                  std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this, res, aborted, scope, token = std::move(token), body = std::move(body)]() {
        auto admin_id = get_owner_id(res, token, aborted);
        if (admin_id.empty()) return;

        auto completed = db.get_setting("setup_completed");
        if (completed && *completed == "true") {
          loop_->defer([res, aborted, scope]() {
            if (*aborted) return;
            res->writeStatus("400")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Setup already completed"})");
            scope->observe(400);
          });
          return;
        }

        save_settings(res, body, aborted, true);
      });
    });
  });

  app.post("/api/admin/recovery-tokens", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("POST", "/api/admin/recovery-tokens");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    res->onData([this, res, aborted, scope, token = std::move(token), body = std::move(body)](
                  std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this, res, aborted, scope, token = std::move(token), body = std::move(body)]() {
        auto admin_id = get_admin_id(res, token, aborted);
        if (admin_id.empty()) return;

        try {
          auto j = json::parse(body);
          std::string user_id = j.at("user_id");
          int expiry = j.value("expiry_hours", defaults::RECOVERY_TOKEN_EXPIRY_HOURS);

          auto user = db.find_user_by_id(user_id);
          if (!user) {
            loop_->defer([res, aborted, scope]() {
              if (*aborted) return;
              res->writeStatus("404")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"User not found"})");
              scope->observe(404);
            });
            return;
          }

          auto recovery_token = db.create_recovery_token(admin_id, user_id, expiry);
          json resp = {{"token", recovery_token}};
          auto resp_str = resp.dump();
          loop_->defer([res, aborted, scope, resp_str = std::move(resp_str)]() {
            if (*aborted) return;
            res->writeHeader("Content-Type", "application/json")->end(resp_str);
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

  app.get("/api/admin/recovery-tokens", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("GET", "/api/admin/recovery-tokens");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    pool_.submit([this, res, aborted, scope, token = std::move(token)]() {
      auto user_id = get_admin_id(res, token, aborted);
      if (user_id.empty()) return;

      auto tokens = db.list_recovery_tokens();
      json arr = json::array();
      for (const auto& t : tokens) {
        json obj = {
          {"id", t.id},
          {"token", t.token},
          {"created_by", t.created_by_username},
          {"for_user", t.for_username},
          {"for_user_id", t.for_user_id},
          {"used", t.used},
          {"expires_at", t.expires_at},
          {"created_at", t.created_at}};
        if (!t.used_at.empty()) obj["used_at"] = t.used_at;
        arr.push_back(obj);
      }
      auto resp_str = arr.dump();
      loop_->defer([res, aborted, scope, resp_str = std::move(resp_str)]() {
        if (*aborted) return;
        res->writeHeader("Content-Type", "application/json")->end(resp_str);
        scope->observe(200);
      });
    });
  });

  app.del("/api/admin/recovery-tokens/:id", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("DEL", "/api/admin/recovery-tokens/:id");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    auto token_id = std::string(req->getParameter("id"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    pool_.submit(
      [this, res, aborted, scope, token = std::move(token), token_id = std::move(token_id)]() {
        auto admin_id = get_admin_id(res, token, aborted);
        if (admin_id.empty()) return;

        if (db.delete_recovery_token(token_id)) {
          loop_->defer([res, aborted, scope]() {
            if (*aborted) return;
            res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
            scope->observe(200);
          });
        } else {
          loop_->defer([res, aborted, scope]() {
            if (*aborted) return;
            res->writeStatus("404")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Token not found or already used"})");
            scope->observe(404);
          });
        }
      });
  });

  // Upload server icon (owner only)
  app.post("/api/admin/server-icon", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("POST", "/api/admin/server-icon");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    auto body = std::make_shared<std::string>();
    int64_t max_size = 50 * 1024 * 1024;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });

    res->onData([this, res, aborted, scope, body, max_size, token = std::move(token)](
                  std::string_view data, bool last) mutable {
      body->append(data);
      if (static_cast<int64_t>(body->size()) > max_size) {
        loop_->defer([res, aborted, scope]() {
          if (*aborted) return;
          res->writeStatus("413")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"json({"error":"Icon too large (max 50MB)"})json");
          scope->observe(413);
        });
        return;
      }
      if (!last) return;

      pool_.submit([this, res, aborted, scope, body, token = std::move(token)]() {
        auto user_id = get_owner_id(res, token, aborted);
        if (user_id.empty()) return;

        try {
          // Delete old icon file if exists
          auto old_id = db.get_setting("server_icon_file_id");
          if (old_id && !old_id->empty()) {
            std::string old_path = config.upload_dir + "/" + *old_id;
            std::filesystem::remove(old_path);
          }

          // Save new icon
          std::string file_id = format_utils::random_hex(32);
          std::string path = config.upload_dir + "/" + file_id;
          std::ofstream out(path, std::ios::binary);
          if (!out) {
            loop_->defer([res, aborted, scope]() {
              if (*aborted) return;
              res->writeStatus("500")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Failed to save icon"})");
              scope->observe(500);
            });
            return;
          }
          out.write(body->data(), body->size());
          out.close();

          db.set_setting("server_icon_file_id", file_id);

          json resp = {{"server_icon_file_id", file_id}};
          auto resp_str = resp.dump();
          loop_->defer([res, aborted, scope, resp_str = std::move(resp_str)]() {
            if (*aborted) return;
            res->writeHeader("Content-Type", "application/json")->end(resp_str);
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

  // Delete server icon (owner only)
  app.del("/api/admin/server-icon", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("DEL", "/api/admin/server-icon");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    pool_.submit([this, res, aborted, scope, token = std::move(token)]() {
      auto user_id = get_owner_id(res, token, aborted);
      if (user_id.empty()) return;

      try {
        auto old_id = db.get_setting("server_icon_file_id");
        if (old_id && !old_id->empty()) {
          std::string old_path = config.upload_dir + "/" + *old_id;
          std::filesystem::remove(old_path);
        }
        db.set_setting("server_icon_file_id", "");
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

  // Upload dark mode server icon (owner only)
  app.post("/api/admin/server-icon-dark", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("POST", "/api/admin/server-icon-dark");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    auto body = std::make_shared<std::string>();
    int64_t max_size = 50 * 1024 * 1024;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });

    res->onData([this, res, aborted, scope, body, max_size, token = std::move(token)](
                  std::string_view data, bool last) mutable {
      body->append(data);
      if (static_cast<int64_t>(body->size()) > max_size) {
        loop_->defer([res, aborted, scope]() {
          if (*aborted) return;
          res->writeStatus("413")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"json({"error":"Icon too large (max 50MB)"})json");
          scope->observe(413);
        });
        return;
      }
      if (!last) return;

      pool_.submit([this, res, aborted, scope, body, token = std::move(token)]() {
        auto user_id = get_owner_id(res, token, aborted);
        if (user_id.empty()) return;

        try {
          // Delete old dark icon file if exists
          auto old_id = db.get_setting("server_icon_dark_file_id");
          if (old_id && !old_id->empty()) {
            std::string old_path = config.upload_dir + "/" + *old_id;
            std::filesystem::remove(old_path);
          }

          // Save new dark icon
          std::string file_id = format_utils::random_hex(32);
          std::string path = config.upload_dir + "/" + file_id;
          std::ofstream out(path, std::ios::binary);
          if (!out) {
            loop_->defer([res, aborted, scope]() {
              if (*aborted) return;
              res->writeStatus("500")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Failed to save icon"})");
              scope->observe(500);
            });
            return;
          }
          out.write(body->data(), body->size());
          out.close();

          db.set_setting("server_icon_dark_file_id", file_id);

          json resp = {{"server_icon_dark_file_id", file_id}};
          auto resp_str = resp.dump();
          loop_->defer([res, aborted, scope, resp_str = std::move(resp_str)]() {
            if (*aborted) return;
            res->writeHeader("Content-Type", "application/json")->end(resp_str);
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

  // Delete dark mode server icon (owner only)
  app.del("/api/admin/server-icon-dark", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("DEL", "/api/admin/server-icon-dark");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    pool_.submit([this, res, aborted, scope, token = std::move(token)]() {
      auto user_id = get_owner_id(res, token, aborted);
      if (user_id.empty()) return;

      try {
        auto old_id = db.get_setting("server_icon_dark_file_id");
        if (old_id && !old_id->empty()) {
          std::string old_path = config.upload_dir + "/" + *old_id;
          std::filesystem::remove(old_path);
        }
        db.set_setting("server_icon_dark_file_id", "");
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

  // Archive server (owner only)
  app.post("/api/admin/archive-server", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("POST", "/api/admin/archive-server");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    res->onData([this, res, aborted, scope, token = std::move(token), body = std::move(body)](
                  std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this, res, aborted, scope, token = std::move(token)]() {
        auto user_id = get_owner_id(res, token, aborted);
        if (user_id.empty()) return;
        db.set_server_archived(true);
        json notify = {{"type", "server_archived_changed"}, {"archived", true}};
        auto notify_str = notify.dump();
        loop_->defer([this, res, aborted, scope, notify_str = std::move(notify_str)]() {
          ws.broadcast_to_presence(notify_str);
          if (*aborted) return;
          res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
          scope->observe(200);
        });
      });
    });
  });

  app.post("/api/admin/unarchive-server", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("POST", "/api/admin/unarchive-server");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    res->onData([this, res, aborted, scope, token = std::move(token), body = std::move(body)](
                  std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this, res, aborted, scope, token = std::move(token)]() {
        auto user_id = get_owner_id(res, token, aborted);
        if (user_id.empty()) return;
        db.set_server_archived(false);
        json notify = {{"type", "server_archived_changed"}, {"archived", false}};
        auto notify_str = notify.dump();
        loop_->defer([this, res, aborted, scope, notify_str = std::move(notify_str)]() {
          ws.broadcast_to_presence(notify_str);
          if (*aborted) return;
          res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
          scope->observe(200);
        });
      });
    });
  });

  // Lockdown server (owner only) — kicks all non-admin users
  app.post("/api/admin/lockdown-server", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("POST", "/api/admin/lockdown-server");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    res->onData([this, res, aborted, scope, token = std::move(token), body = std::move(body)](
                  std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this, res, aborted, scope, token = std::move(token)]() {
        auto user_id = get_owner_id(res, token, aborted);
        if (user_id.empty()) return;
        db.set_server_locked_down(true);
        json notify = {{"type", "server_lockdown_changed"}, {"locked_down", true}};
        auto notify_str = notify.dump();
        json kick = {{"type", "server_locked_down"}};
        auto kick_str = kick.dump();
        loop_->defer([this,
                      res,
                      aborted,
                      scope,
                      notify_str = std::move(notify_str),
                      kick_str = std::move(kick_str)]() {
          ws.broadcast_to_presence(notify_str);
          ws.disconnect_non_admins(kick_str);
          if (*aborted) return;
          res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
          scope->observe(200);
        });
      });
    });
  });

  app.post("/api/admin/unlock-server", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("POST", "/api/admin/unlock-server");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    res->onData([this, res, aborted, scope, token = std::move(token), body = std::move(body)](
                  std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this, res, aborted, scope, token = std::move(token)]() {
        auto user_id = get_owner_id(res, token, aborted);
        if (user_id.empty()) return;
        db.set_server_locked_down(false);
        json notify = {{"type", "server_lockdown_changed"}, {"locked_down", false}};
        auto notify_str = notify.dump();
        loop_->defer([this, res, aborted, scope, notify_str = std::move(notify_str)]() {
          ws.broadcast_to_presence(notify_str);
          if (*aborted) return;
          res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
          scope->observe(200);
        });
      });
    });
  });

  // List users (admin or owner)
  app.get("/api/admin/users", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("GET", "/api/admin/users");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    pool_.submit([this, res, aborted, scope, token = std::move(token)]() {
      auto user_id = get_admin_id(res, token, aborted);
      if (user_id.empty()) return;

      auto users = db.list_users();
      json arr = json::array();
      for (const auto& u : users) {
        arr.push_back(
          {{"id", u.id},
           {"username", u.username},
           {"display_name", u.display_name},
           {"role", u.role},
           {"is_online", u.is_online},
           {"last_seen", u.last_seen},
           {"is_banned", u.is_banned}});
      }
      auto resp_str = arr.dump();
      loop_->defer([res, aborted, scope, resp_str = std::move(resp_str)]() {
        if (*aborted) return;
        res->writeHeader("Content-Type", "application/json")->end(resp_str);
        scope->observe(200);
      });
    });
  });

  // Change user role (admin or owner, with hierarchy enforcement)
  app.put("/api/admin/users/:userId/role", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("PUT", "/api/admin/users/:userId/role");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    auto target_user_id = std::string(req->getParameter("userId"));
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 scope,
                 token = std::move(token),
                 target_user_id = std::move(target_user_id),
                 body = std::move(body)](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    scope,
                    token = std::move(token),
                    target_user_id = std::move(target_user_id),
                    body = std::move(body)]() {
        auto actor_id = get_admin_id(res, token, aborted);
        if (actor_id.empty()) return;

        try {
          auto j = json::parse(body);
          std::string new_role = j.at("role");
          if (new_role != "owner" && new_role != "admin" && new_role != "user") {
            loop_->defer([res, aborted, scope]() {
              if (*aborted) return;
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Invalid role. Must be owner, admin, or user"})");
              scope->observe(400);
            });
            return;
          }

          auto actor = db.find_user_by_id(actor_id);
          auto target = db.find_user_by_id(target_user_id);
          if (!actor || !target) {
            loop_->defer([res, aborted, scope]() {
              if (*aborted) return;
              res->writeStatus("404")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"User not found"})");
              scope->observe(404);
            });
            return;
          }

          int actor_rank = server_role_rank(actor->role);
          int target_rank = server_role_rank(target->role);
          int new_rank = server_role_rank(new_role);

          // Cannot promote anyone to a rank above your own
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
          if (new_rank < target_rank && target_rank >= actor_rank && actor_id != target_user_id) {
            loop_->defer([res, aborted, scope]() {
              if (*aborted) return;
              res->writeStatus("403")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Cannot demote a user of equal or higher rank"})");
              scope->observe(403);
            });
            return;
          }

          // Prevent demoting the last owner
          if (target->role == "owner" && new_role != "owner") {
            int owner_count = db.count_users_with_role("owner");
            if (owner_count <= 1) {
              loop_->defer([res, aborted, scope]() {
                if (*aborted) return;
                res->writeStatus("400")
                  ->writeHeader("Content-Type", "application/json")
                  ->end(
                    R"({"error":"Cannot demote the last owner. Assign a new owner or archive the server first.","last_owner":true})");
                scope->observe(400);
              });
              return;
            }
          }

          db.update_user_role(target_user_id, new_role);

          // Notify the target user of their new server role
          json notify = {{"type", "server_role_changed"}, {"role", new_role}};
          auto notify_str = notify.dump();

          // Broadcast to all connected users so they can update user cards
          json broadcast = {
            {"type", "user_role_changed"}, {"user_id", target_user_id}, {"role", new_role}};
          auto broadcast_str = broadcast.dump();

          loop_->defer([this,
                        res,
                        aborted,
                        scope,
                        target_user_id,
                        notify_str = std::move(notify_str),
                        broadcast_str = std::move(broadcast_str)]() {
            ws.send_to_user(target_user_id, notify_str);
            ws.broadcast_to_presence(broadcast_str);
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

  // Ban user (admin or owner, with hierarchy enforcement)
  app.post("/api/admin/users/:userId/ban", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("POST", "/api/admin/users/:userId/ban");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    auto target_user_id = std::string(req->getParameter("userId"));
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 scope,
                 token = std::move(token),
                 target_user_id = std::move(target_user_id),
                 body = std::move(body)](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    scope,
                    token = std::move(token),
                    target_user_id = std::move(target_user_id)]() {
        auto actor_id = get_admin_id(res, token, aborted);
        if (actor_id.empty()) return;

        try {
          auto actor = db.find_user_by_id(actor_id);
          auto target = db.find_user_by_id(target_user_id);
          if (!actor || !target) {
            loop_->defer([res, aborted, scope]() {
              if (*aborted) return;
              res->writeStatus("404")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"User not found"})");
              scope->observe(404);
            });
            return;
          }

          if (target->is_banned) {
            loop_->defer([res, aborted, scope]() {
              if (*aborted) return;
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"User is already banned"})");
              scope->observe(400);
            });
            return;
          }

          int actor_rank = server_role_rank(actor->role);
          int target_rank = server_role_rank(target->role);

          // Cannot ban someone of equal or higher rank
          if (target_rank >= actor_rank) {
            loop_->defer([res, aborted, scope]() {
              if (*aborted) return;
              res->writeStatus("403")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Cannot ban a user of equal or higher rank"})");
              scope->observe(403);
            });
            return;
          }

          // Cannot ban yourself
          if (actor_id == target_user_id) {
            loop_->defer([res, aborted, scope]() {
              if (*aborted) return;
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Cannot ban yourself"})");
              scope->observe(400);
            });
            return;
          }

          db.ban_user(target_user_id, actor_id);

          // Notify the banned user before disconnecting
          json notify = {{"type", "banned"}};
          auto notify_str = notify.dump();

          // Broadcast to all connected users
          json broadcast = {{"type", "user_banned"}, {"user_id", target_user_id}};
          auto broadcast_str = broadcast.dump();

          loop_->defer([this,
                        res,
                        aborted,
                        scope,
                        target_user_id,
                        notify_str = std::move(notify_str),
                        broadcast_str = std::move(broadcast_str)]() {
            ws.send_to_user(target_user_id, notify_str);
            ws.disconnect_user(target_user_id);
            ws.broadcast_to_presence(broadcast_str);
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

  // Unban user (admin or owner, with hierarchy enforcement)
  app.del("/api/admin/users/:userId/ban", [this](auto* res, auto* req) {
    auto scope =
      std::make_shared<handler_utils::RequestScope>("DEL", "/api/admin/users/:userId/ban");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    auto target_user_id = std::string(req->getParameter("userId"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    pool_.submit([this,
                  res,
                  aborted,
                  scope,
                  token = std::move(token),
                  target_user_id = std::move(target_user_id)]() {
      auto actor_id = get_admin_id(res, token, aborted);
      if (actor_id.empty()) return;

      auto actor = db.find_user_by_id(actor_id);
      auto target = db.find_user_by_id(target_user_id);
      if (!actor || !target) {
        loop_->defer([res, aborted, scope]() {
          if (*aborted) return;
          res->writeStatus("404")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"User not found"})");
          scope->observe(404);
        });
        return;
      }

      if (!target->is_banned) {
        loop_->defer([res, aborted, scope]() {
          if (*aborted) return;
          res->writeStatus("400")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"User is not banned"})");
          scope->observe(400);
        });
        return;
      }

      int actor_rank = server_role_rank(actor->role);
      int target_rank = server_role_rank(target->role);

      // Cannot unban someone of equal or higher rank
      if (target_rank >= actor_rank) {
        loop_->defer([res, aborted, scope]() {
          if (*aborted) return;
          res->writeStatus("403")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Cannot unban a user of equal or higher rank"})");
          scope->observe(403);
        });
        return;
      }

      db.unban_user(target_user_id);

      // Broadcast to all connected users
      json broadcast = {{"type", "user_unbanned"}, {"user_id", target_user_id}};
      auto broadcast_str = broadcast.dump();

      loop_->defer([this, res, aborted, scope, broadcast_str = std::move(broadcast_str)]() {
        ws.broadcast_to_presence(broadcast_str);
        if (*aborted) return;
        res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
        scope->observe(200);
      });
    });
  });

  // System resource monitoring
  app.get("/api/admin/system-stats", [this](auto* res, auto* req) {
    auto scope = std::make_shared<handler_utils::RequestScope>("GET", "/api/admin/system-stats");
    handler_utils::set_request_id_header(res, *scope);
    auto token = extract_session_token(req);
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    pool_.submit([this, res, aborted, scope, token = std::move(token)]() {
      auto user_id = get_admin_id(res, token, aborted);
      if (user_id.empty()) return;

      // Memory from /proc/meminfo (values in kB)
      int64_t mem_total = read_meminfo_kb("MemTotal:");
      int64_t mem_available = read_meminfo_kb("MemAvailable:");
      int64_t swap_total = read_meminfo_kb("SwapTotal:");
      int64_t swap_free = read_meminfo_kb("SwapFree:");

      // CPU: sample two readings ~100ms apart
      auto cpu1 = read_cpu_jiffies();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      auto cpu2 = read_cpu_jiffies();

      double cpu_percent = 0.0;
      int64_t total_diff = cpu2.total - cpu1.total;
      int64_t idle_diff = cpu2.idle - cpu1.idle;
      if (total_diff > 0) {
        cpu_percent =
          100.0 * (1.0 - static_cast<double>(idle_diff) / static_cast<double>(total_diff));
      }

      // Load averages from /proc/loadavg
      double load1 = 0, load5 = 0, load15 = 0;
      {
        std::ifstream f("/proc/loadavg");
        f >> load1 >> load5 >> load15;
      }

      // Network cumulative counters
      auto net = read_net_counters();

      json resp = {
        {"cpu_percent", std::round(cpu_percent * 10.0) / 10.0},
        {"load_1m", std::round(load1 * 100.0) / 100.0},
        {"load_5m", std::round(load5 * 100.0) / 100.0},
        {"load_15m", std::round(load15 * 100.0) / 100.0},
        {"mem_total_kb", mem_total},
        {"mem_available_kb", mem_available},
        {"swap_total_kb", swap_total},
        {"swap_free_kb", swap_free},
        {"net_rx_bytes", net.rx_bytes},
        {"net_tx_bytes", net.tx_bytes},
      };

      auto resp_str = resp.dump();
      loop_->defer([res, aborted, scope, resp_str = std::move(resp_str)]() {
        if (*aborted) return;
        res->writeHeader("Content-Type", "application/json")->end(resp_str);
        scope->observe(200);
      });
    });
  });
}

template <bool SSL>
std::string AdminHandler<SSL>::get_admin_id(
  uWS::HttpResponse<SSL>* res, const std::string& token, std::shared_ptr<bool> aborted) {
  auto user_id = db.validate_session(token);
  if (!user_id) {
    loop_->defer([res, aborted]() {
      if (*aborted) return;
      res->writeStatus("401")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Unauthorized"})");
    });
    return "";
  }
  auto user = db.find_user_by_id(*user_id);
  if (!user || (user->role != "admin" && user->role != "owner")) {
    loop_->defer([res, aborted]() {
      if (*aborted) return;
      res->writeStatus("403")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Admin access required"})");
    });
    return "";
  }
  return *user_id;
}

template <bool SSL>
std::string AdminHandler<SSL>::get_owner_id(
  uWS::HttpResponse<SSL>* res, const std::string& token, std::shared_ptr<bool> aborted) {
  auto user_id = db.validate_session(token);
  if (!user_id) {
    loop_->defer([res, aborted]() {
      if (*aborted) return;
      res->writeStatus("401")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Unauthorized"})");
    });
    return "";
  }
  auto user = db.find_user_by_id(*user_id);
  if (!user || user->role != "owner") {
    loop_->defer([res, aborted]() {
      if (*aborted) return;
      res->writeStatus("403")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Owner access required"})");
    });
    return "";
  }
  return *user_id;
}

template <bool SSL>
std::string AdminHandler<SSL>::get_setting_or(const std::string& key, const std::string& fallback) {
  auto val = db.get_setting(key);
  return val.value_or(fallback);
}

template <bool SSL>
json AdminHandler<SSL>::build_settings_response() {
  admin_settings::Snapshot snapshot;
  snapshot.config_max_file_size = config.max_file_size;
  snapshot.config_session_expiry_hours = config.session_expiry_hours;
  snapshot.storage_used = db.get_total_file_size();
  snapshot.server_archived = db.is_server_archived();
  snapshot.server_locked_down = db.is_server_locked_down();

  snapshot.max_file_size = db.get_setting("max_file_size");
  snapshot.max_storage_size = db.get_setting("max_storage_size");
  snapshot.auth_methods = db.get_setting("auth_methods");
  snapshot.server_name = db.get_setting("server_name");
  snapshot.server_icon_file_id = db.get_setting("server_icon_file_id");
  snapshot.server_icon_dark_file_id = db.get_setting("server_icon_dark_file_id");
  snapshot.registration_mode = db.get_setting("registration_mode");
  snapshot.file_uploads_enabled = db.get_setting("file_uploads_enabled");
  snapshot.session_expiry_hours = db.get_setting("session_expiry_hours");
  snapshot.setup_completed = db.get_setting("setup_completed");
  snapshot.password_min_length = db.get_setting("password_min_length");
  snapshot.password_require_uppercase = db.get_setting("password_require_uppercase");
  snapshot.password_require_lowercase = db.get_setting("password_require_lowercase");
  snapshot.password_require_number = db.get_setting("password_require_number");
  snapshot.password_require_special = db.get_setting("password_require_special");
  snapshot.password_max_age_days = db.get_setting("password_max_age_days");
  snapshot.password_history_count = db.get_setting("password_history_count");
  snapshot.mfa_required_password = db.get_setting("mfa_required_password");
  snapshot.mfa_required_pki = db.get_setting("mfa_required_pki");
  snapshot.mfa_required_passkey = db.get_setting("mfa_required_passkey");

  snapshot.default_space_storage_limit = db.get_setting("default_space_storage_limit");

  snapshot.personal_spaces_enabled = db.get_setting("personal_spaces_enabled");
  snapshot.personal_spaces_files_enabled = db.get_setting("personal_spaces_files_enabled");
  snapshot.personal_spaces_calendar_enabled = db.get_setting("personal_spaces_calendar_enabled");
  snapshot.personal_spaces_tasks_enabled = db.get_setting("personal_spaces_tasks_enabled");
  snapshot.personal_spaces_wiki_enabled = db.get_setting("personal_spaces_wiki_enabled");
  snapshot.personal_spaces_minigames_enabled = db.get_setting("personal_spaces_minigames_enabled");
  snapshot.personal_spaces_sandbox_enabled = db.get_setting("personal_spaces_sandbox_enabled");
  snapshot.personal_spaces_storage_limit = db.get_setting("personal_spaces_storage_limit");
  snapshot.personal_spaces_total_storage_limit =
    db.get_setting("personal_spaces_total_storage_limit");

  snapshot.llm_enabled = db.get_setting("llm_enabled");
  snapshot.llm_api_url = db.get_setting("llm_api_url");
  snapshot.llm_model = db.get_setting("llm_model");
  snapshot.llm_api_key = db.get_setting("llm_api_key");
  snapshot.llm_max_tokens = db.get_setting("llm_max_tokens");
  snapshot.llm_system_prompt = db.get_setting("llm_system_prompt");

  return admin_settings::build_settings_response(snapshot);
}

template <bool SSL>
void AdminHandler<SSL>::save_settings(
  uWS::HttpResponse<SSL>* res,
  const std::string& body,
  std::shared_ptr<bool> aborted,
  bool mark_setup) {
  try {
    auto settings = json::parse(body);
    auto updates = admin_settings::collect_settings_updates(settings, mark_setup);
    for (const auto& [key, value] : updates) {
      db.set_setting(key, value);
    }

    // Propagate default space storage limit to existing non-personal spaces.
    // Page through to avoid loading every row at once.
    // TODO(P1.5+): rewrite as a single bulk UPSERT into server_settings keyed off
    // spaces.is_personal=false, so we don't iterate row-by-row.
    if (updates.count("default_space_storage_limit")) {
      int64_t limit = std::stoll(updates.at("default_space_storage_limit"));
      constexpr int kPageSize = 500;
      int offset = 0;
      while (true) {
        auto page = db.list_all_spaces(kPageSize, offset);
        if (page.empty()) break;
        for (const auto& sp : page) {
          if (!sp.is_personal) {
            db.set_setting("space_storage_limit_" + sp.id, std::to_string(limit));
          }
        }
        if (static_cast<int>(page.size()) < kPageSize) break;
        offset += kPageSize;
      }
    }

    // Propagate personal space storage limit to existing personal spaces.
    // TODO(P1.5+): rewrite as a single bulk UPSERT keyed off spaces.is_personal=true.
    if (updates.count("personal_spaces_storage_limit")) {
      int64_t limit = std::stoll(updates.at("personal_spaces_storage_limit"));
      constexpr int kPageSize = 500;
      int offset = 0;
      while (true) {
        auto page = db.list_all_spaces(kPageSize, offset);
        if (page.empty()) break;
        for (const auto& sp : page) {
          if (sp.is_personal) {
            db.set_setting("space_storage_limit_" + sp.id, std::to_string(limit));
          }
        }
        if (static_cast<int>(page.size()) < kPageSize) break;
        offset += kPageSize;
      }
    }

    // Notify all clients to refresh spaces when personal space settings change
    bool needs_refresh = updates.count("personal_spaces_enabled") ||
                         updates.count("personal_spaces_files_enabled") ||
                         updates.count("personal_spaces_calendar_enabled") ||
                         updates.count("personal_spaces_tasks_enabled") ||
                         updates.count("personal_spaces_wiki_enabled") ||
                         updates.count("personal_spaces_minigames_enabled") ||
                         updates.count("personal_spaces_sandbox_enabled");

    if (needs_refresh) {
      json notify = {{"type", "refresh_spaces"}};
      auto notify_str = notify.dump();
      loop_->defer([this, res, aborted, notify_str = std::move(notify_str)]() {
        ws.broadcast_to_presence(notify_str);
        if (*aborted) return;
        res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
      });
    } else {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
      });
    }
  } catch (const std::exception& e) {
    auto err = json{{"error", e.what()}}.dump();
    loop_->defer([res, aborted, err = std::move(err)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
    });
  }
}

template <bool SSL>
int AdminHandler<SSL>::get_session_expiry_hours() {
  return ::get_session_expiry(db, config);
}

template <bool SSL>
void AdminHandler<SSL>::handle_approve(
  uWS::HttpResponse<SSL>* res,
  const std::string& request_id,
  const std::string& admin_id,
  std::shared_ptr<bool> aborted) {
  try {
    auto request = db.get_join_request(request_id);
    if (!request) {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("404")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Request not found"})");
      });
      return;
    }

    if (request->status != "pending") {
      loop_->defer([res, aborted]() {
        if (*aborted) return;
        res->writeStatus("400")
          ->writeHeader("Content-Type", "application/json")
          ->end(R"({"error":"Request already processed"})");
      });
      return;
    }

    // Create user account from join request
    auto user = db.create_user(request->username, request->display_name, "", "user");

    // Store credentials based on auth method
    auto parsed = admin_approval_utils::parse_credential_data(
      request->auth_method, request->credential_data, []() {
        return webauthn::generate_recovery_keys();
      });

    if (parsed.pki_public_key) {
      db.store_pki_credential(user.id, *parsed.pki_public_key);
      db.store_recovery_keys(user.id, parsed.recovery_key_hashes);
    } else if (parsed.passkey_credential_id) {
      db.store_webauthn_credential(
        user.id,
        *parsed.passkey_credential_id,
        parsed.passkey_public_key,
        parsed.passkey_sign_count,
        "Passkey",
        parsed.passkey_transports);
    } else if (parsed.password_hash) {
      db.store_password(user.id, *parsed.password_hash);
    }

    // Create session token for polling pickup
    std::string session_token = db.create_session(user.id, get_session_expiry_hours());
    db.set_join_request_session(request_id, session_token);
    db.update_join_request(request_id, "approved", admin_id);

    json resp = {{"ok", true}};
    auto resp_str = resp.dump();
    loop_->defer([res, aborted, resp_str = std::move(resp_str)]() {
      if (*aborted) return;
      res->writeHeader("Content-Type", "application/json")->end(resp_str);
    });
  } catch (const pqxx::unique_violation&) {
    loop_->defer([res, aborted]() {
      if (*aborted) return;
      res->writeStatus("409")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Username already taken"})");
    });
  } catch (const std::exception& e) {
    auto err = json({{"error", e.what()}}).dump();
    loop_->defer([res, aborted, err = std::move(err)]() {
      if (*aborted) return;
      res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
    });
  }
}

template struct AdminHandler<false>;
template struct AdminHandler<true>;
