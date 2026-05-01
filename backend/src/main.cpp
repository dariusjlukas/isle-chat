#include <App.h>
#include <unistd.h>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include "ai/tool_registry.h"
#include "config.h"
#include "db/database.h"
#include "db/db_thread_pool.h"
#include "handlers/admin_handler.h"
#include "handlers/ai_handler.h"
#include "handlers/auth_handler.h"
#include "handlers/calendar_handler.h"
#include "handlers/channel_handler.h"
#include "handlers/cors_utils.h"
#include "handlers/file_handler.h"
#include "handlers/notification_handler.h"
#include "handlers/search_handler.h"
#include "handlers/space_file_handler.h"
#include "handlers/space_handler.h"
#include "handlers/task_board_handler.h"
#include "handlers/user_handler.h"
#include "handlers/wiki_handler.h"
#include "logging/logger.h"
#include "metrics/metrics.h"
#include "upload_manager.h"
#include "ws/ws_handler.h"

us_listen_socket_t* global_listen_socket = nullptr;
uWS::Loop* global_loop = nullptr;
std::function<void()> global_close_connections;

void shutdown_handler(int signum) {
  // Signal-handler-safe: cannot call into spdlog/malloc. Use raw write() with
  // a precomputed buffer + a single signal-number digit. This intentionally
  // bypasses the structured JSON logger.
  static constexpr char kPrefix[] = "\n[Server] Received signal ";
  static constexpr char kSuffix[] = ", shutting down...\n";
  ssize_t n = ::write(STDERR_FILENO, kPrefix, sizeof(kPrefix) - 1);
  (void)n;
  // Format signum as decimal manually (signal-safe).
  char num_buf[16];
  int idx = static_cast<int>(sizeof(num_buf));
  num_buf[--idx] = '\0';
  int v = signum >= 0 ? signum : -signum;
  if (v == 0) {
    num_buf[--idx] = '0';
  } else {
    while (v > 0 && idx > 0) {
      num_buf[--idx] = static_cast<char>('0' + (v % 10));
      v /= 10;
    }
  }
  if (signum < 0 && idx > 0) num_buf[--idx] = '-';
  n = ::write(STDERR_FILENO, num_buf + idx, std::strlen(num_buf + idx));
  (void)n;
  n = ::write(STDERR_FILENO, kSuffix, sizeof(kSuffix) - 1);
  (void)n;
  if (global_loop) {
    global_loop->defer([]() {
      if (global_listen_socket) {
        us_listen_socket_close(0, global_listen_socket);
        global_listen_socket = nullptr;
      }
      if (global_close_connections) {
        global_close_connections();
      }
    });
  }
}

template <bool SSL>
void run_server(
  uWS::TemplatedApp<SSL>&& app,
  Config& config,
  Database& db,
  UploadManager& upload_manager,
  DbThreadPool& pool) {
  auto* loop = uWS::Loop::get();

  WsHandler<SSL> ws_handler(db, config, loop, pool);
  AuthHandler<SSL> auth_handler{db, config, ws_handler, loop, pool};
  ChannelHandler<SSL> channel_handler{db, ws_handler, loop, pool};
  SpaceHandler<SSL> space_handler{db, ws_handler, config, loop, pool};
  UserHandler<SSL> user_handler{db, ws_handler, config, loop, pool};
  AdminHandler<SSL> admin_handler{db, config, ws_handler, loop, pool};
  FileHandler<SSL> file_handler{db, config, upload_manager, nullptr, loop, &pool};
  SearchHandler<SSL> search_handler{db, loop, pool};
  NotificationHandler<SSL> notification_handler{db, loop, pool};
  SpaceFileHandler<SSL> space_file_handler{db, config, upload_manager, loop, pool};
  CalendarHandler<SSL> calendar_handler{db, config, loop, pool};
  TaskBoardHandler<SSL> task_board_handler{db, config, loop, pool};
  WikiHandler<SSL> wiki_handler{db, config, upload_manager, loop, pool};

  ToolRegistry tool_registry;
  register_all_tools(tool_registry);
  AiHandler<SSL> ai_handler{db, config, ws_handler, tool_registry, loop, pool};

  // CORS preflight. Origin is validated against the ALLOWED_ORIGINS allowlist;
  // wildcard "*" is no longer emitted. Requests with a disallowed/absent Origin
  // simply receive a 204 without CORS headers, which browsers will treat as a
  // preflight failure.
  app.options("/*", [](auto* res, auto* req) {
    std::string_view origin = req->getHeader("origin");
    cors::apply(res, origin);
    res->writeHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
      ->writeHeader("Access-Control-Allow-Headers", "Content-Type, Authorization")
      ->writeStatus("204")
      ->end();
  });

  // Register routes (search before channels so /messages/around matches first)
  auth_handler.register_routes(app);
  notification_handler.register_routes(app);
  search_handler.register_routes(app);
  task_board_handler.register_routes(app);
  wiki_handler.register_routes(app);
  calendar_handler.register_routes(app);
  space_file_handler.register_routes(app);
  space_handler.register_routes(app);
  channel_handler.register_routes(app);
  user_handler.register_routes(app);
  ai_handler.register_routes(app);
  admin_handler.register_routes(app);
  file_handler.register_routes(app);
  ws_handler.register_routes(app);

  // Public config (non-sensitive settings for the frontend)
  app.get("/api/config", [&config, &db, loop, &pool](auto* res, auto* req) {
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    std::string origin(req->getHeader("origin"));
    pool.submit([res, aborted, origin = std::move(origin), &config, &db, loop]() {
      json resp;
      resp["public_url"] = config.public_url;

      resp["auth_methods"] = get_auth_methods(db);

      auto server_name = db.get_setting("server_name");
      resp["server_name"] = server_name.value_or("EnclaveStation");

      auto server_icon = db.get_setting("server_icon_file_id");
      resp["server_icon_file_id"] = server_icon.value_or("");

      auto server_icon_dark = db.get_setting("server_icon_dark_file_id");
      resp["server_icon_dark_file_id"] = server_icon_dark.value_or("");

      auto reg_mode = db.get_setting("registration_mode");
      resp["registration_mode"] = reg_mode.value_or("invite");

      auto setup = db.get_setting("setup_completed");
      resp["setup_completed"] = (setup && *setup == "true");

      resp["has_users"] = (db.count_users() > 0);

      auto uploads = db.get_setting("file_uploads_enabled");
      resp["file_uploads_enabled"] = (!uploads || *uploads == "true");

      resp["server_archived"] = db.is_server_archived();
      resp["server_locked_down"] = db.is_server_locked_down();

      // MFA requirements
      auto mfa_pw = db.get_setting("mfa_required_password");
      resp["mfa_required_password"] = (mfa_pw && *mfa_pw == "true");
      auto mfa_pki = db.get_setting("mfa_required_pki");
      resp["mfa_required_pki"] = (mfa_pki && *mfa_pki == "true");
      auto mfa_pk = db.get_setting("mfa_required_passkey");
      resp["mfa_required_passkey"] = (mfa_pk && *mfa_pk == "true");

      // Password policy (public so registration form can validate client-side)
      auto auth_methods = resp["auth_methods"];
      bool password_enabled = false;
      for (const auto& m : auth_methods) {
        if (m.get<std::string>() == "password") {
          password_enabled = true;
          break;
        }
      }
      if (password_enabled) {
        auto get_or = [&db](const std::string& key, const std::string& def) -> std::string {
          auto v = db.get_setting(key);
          return v.value_or(def);
        };
        resp["password_policy"] = {
          {"min_length", handler_utils::safe_parse_int(get_or("password_min_length", "8"), 8)},
          {"require_uppercase", get_or("password_require_uppercase", "true") == "true"},
          {"require_lowercase", get_or("password_require_lowercase", "true") == "true"},
          {"require_number", get_or("password_require_number", "true") == "true"},
          {"require_special", get_or("password_require_special", "false") == "true"},
        };
      }

      // LLM / AI assistant
      auto llm_enabled = db.get_setting("llm_enabled");
      resp["llm_enabled"] = (llm_enabled && *llm_enabled == "true");

      auto body = resp.dump();
      loop->defer([res, aborted, origin = std::move(origin), body = std::move(body)]() {
        if (*aborted) return;
        res->writeHeader("Content-Type", "application/json");
        cors::apply(res, origin);
        res->end(body);
      });
    });
  });

  // Health check (instrumented as a proof-of-concept for the /metrics
  // endpoint; full handler-wide instrumentation is a follow-up — most
  // handlers don't share a uniform middleware layer right now).
  app.get("/api/health", [](auto* res, auto* req) {
    metrics::RequestTimer timer("GET", "/api/health");
    std::string_view origin = req->getHeader("origin");
    res->writeHeader("Content-Type", "application/json");
    cors::apply(res, origin);
    res->end(R"({"status":"ok"})");
    timer.observe(200);
  });

  // Prometheus-style metrics endpoint. nginx restricts access to internal
  // networks; the backend itself does not authenticate this route.
  app.get("/metrics", [&db](auto* res, auto* req) {
    (void)req;
    // Refresh DB pool gauges on scrape.
    metrics::db_pool_in_use().set(static_cast<int64_t>(db.pool_in_use()));
    std::string body = metrics::render();
    res->writeHeader("Content-Type", "text/plain; version=0.0.4")->end(body);
  });

  // Start listening
  global_loop = loop;
  global_close_connections = [&ws_handler]() { ws_handler.close_all(); };
  app
    .listen(
      config.server_port,
      [&config](auto* listen_socket) {
        if (listen_socket) {
          global_listen_socket = listen_socket;
          LOG_INFO_N(
            "server",
            nullptr,
            "Listening on port " + std::to_string(config.server_port) +
              (config.has_ssl() ? " (HTTPS)" : " (HTTP)"));
        } else {
          LOG_ERROR_N(
            "server", nullptr, "Failed to listen on port " + std::to_string(config.server_port));
          exit(1);
        }
      })
    .run();

  LOG_INFO_N("server", nullptr, "Shut down cleanly.");
}

int main() {
  // Initialize structured JSON logger before anything else. Reads LOG_LEVEL.
  logging::init();
  LOG_INFO_N("server", nullptr, "=== Chat Server ===");

  auto config = Config::from_env();
  LOG_INFO_N("config", nullptr, "Port: " + std::to_string(config.server_port));

  // Load CORS allowlist from env. Wildcard "*" is intentionally not supported.
  cors::init_from_env(std::getenv("ALLOWED_ORIGINS"));
  const auto& origins = cors::allowed_origins();
  if (origins.empty()) {
    LOG_WARN_N(
      "config",
      nullptr,
      "ALLOWED_ORIGINS is not set; cross-origin browser requests will be rejected. "
      "Set ALLOWED_ORIGINS to a comma-separated list of allowed origins "
      "(e.g. https://example.com,https://app.example.com).");
  } else {
    std::string list;
    for (size_t i = 0; i < origins.size(); ++i) {
      if (i) list += ", ";
      list += origins[i];
    }
    LOG_INFO_N("config", nullptr, "CORS allowlist: " + list);
  }

  // Create upload directory
  std::filesystem::create_directories(config.upload_dir);
  LOG_INFO_N("config", nullptr, "Upload dir: " + config.upload_dir);

  // Connect to database
  Database db(config.pg_connection_string(), config.db_pool_size);
  if (config.enable_sqitch_only) {
    LOG_INFO_N(
      "db", nullptr, "ENABLE_SQITCH_ONLY=1; skipping run_migrations() (sqitch is authoritative)");
    // Fail fast if the operator set ENABLE_SQITCH_ONLY=1 without applying a
    // sqitch deploy first. Better to refuse to start than to begin serving
    // requests against an empty schema.
    if (!db.schema_initialized()) {
      LOG_ERROR_N(
        "db",
        nullptr,
        "FATAL: ENABLE_SQITCH_ONLY=1 but the schema is not initialized "
        "(no 'users' table found). Apply sqitch first: 'docker compose run --rm sqitch deploy'.");
      return 1;
    }
  } else {
    db.run_migrations();
  }
  db.set_all_users_offline();

  // Initialize Prometheus metrics state and seed the static DB pool size.
  metrics::init();
  metrics::db_pool_size().set(static_cast<int64_t>(db.pool_size()));

  // Upload manager for chunked uploads
  UploadManager upload_manager(config.upload_dir);

  // Thread pool for async DB operations
  DbThreadPool pool(config.db_thread_pool_size);

  std::signal(SIGTERM, shutdown_handler);
  std::signal(SIGINT, shutdown_handler);

  if (config.has_ssl()) {
    LOG_INFO_N(
      "config",
      nullptr,
      "SSL enabled (cert: " + config.ssl_cert_path + ", key: " + config.ssl_key_path + ")");
    run_server(
      uWS::SSLApp(
        {.key_file_name = config.ssl_key_path.c_str(),
         .cert_file_name = config.ssl_cert_path.c_str()}),
      config,
      db,
      upload_manager,
      pool);
  } else {
    run_server(uWS::App(), config, db, upload_manager, pool);
  }

  return 0;
}
