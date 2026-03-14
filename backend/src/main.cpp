#include <App.h>
#include <iostream>
#include <filesystem>
#include <csignal>
#include <functional>
#include "config.h"
#include "db/database.h"
#include "handlers/auth_handler.h"
#include "handlers/channel_handler.h"
#include "handlers/user_handler.h"
#include "handlers/admin_handler.h"
#include "handlers/file_handler.h"
#include "handlers/space_handler.h"
#include "handlers/search_handler.h"
#include "handlers/notification_handler.h"
#include "handlers/space_file_handler.h"
#include "handlers/calendar_handler.h"
#include "upload_manager.h"
#include "ws/ws_handler.h"

us_listen_socket_t* global_listen_socket = nullptr;
uWS::Loop* global_loop = nullptr;
std::function<void()> global_close_connections;

void shutdown_handler(int signum) {
    std::cout << "\n[Server] Received signal " << signum << ", shutting down..." << std::endl;
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
void run_server(uWS::TemplatedApp<SSL>&& app, Config& config, Database& db,
                UploadManager& upload_manager) {
    WsHandler<SSL> ws_handler(db, config);
    AuthHandler<SSL> auth_handler{db, config, ws_handler};
    ChannelHandler<SSL> channel_handler{db, ws_handler};
    SpaceHandler<SSL> space_handler{db, ws_handler, config};
    UserHandler<SSL> user_handler{db, ws_handler, config};
    AdminHandler<SSL> admin_handler{db, config, ws_handler};
    FileHandler<SSL> file_handler{db, config, upload_manager};
    SearchHandler<SSL> search_handler{db};
    NotificationHandler<SSL> notification_handler{db};
    SpaceFileHandler<SSL> space_file_handler{db, config, upload_manager};
    CalendarHandler<SSL> calendar_handler{db, config};

    // CORS preflight
    app.options("/*", [](auto* res, auto* req) {
        res->writeHeader("Access-Control-Allow-Origin", "*")
            ->writeHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
            ->writeHeader("Access-Control-Allow-Headers", "Content-Type, Authorization")
            ->writeStatus("204")->end();
    });

    // Register routes (search before channels so /messages/around matches first)
    auth_handler.register_routes(app);
    notification_handler.register_routes(app);
    search_handler.register_routes(app);
    calendar_handler.register_routes(app);
    space_file_handler.register_routes(app);
    space_handler.register_routes(app);
    channel_handler.register_routes(app);
    user_handler.register_routes(app);
    admin_handler.register_routes(app);
    file_handler.register_routes(app);
    ws_handler.register_routes(app);

    // Public config (non-sensitive settings for the frontend)
    app.get("/api/config", [&config, &db](auto* res, auto* req) {
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
            if (m.get<std::string>() == "password") { password_enabled = true; break; }
        }
        if (password_enabled) {
            auto get_or = [&db](const std::string& key, const std::string& def) -> std::string {
                auto v = db.get_setting(key);
                return v.value_or(def);
            };
            resp["password_policy"] = {
                {"min_length", std::stoi(get_or("password_min_length", "8"))},
                {"require_uppercase", get_or("password_require_uppercase", "true") == "true"},
                {"require_lowercase", get_or("password_require_lowercase", "true") == "true"},
                {"require_number", get_or("password_require_number", "true") == "true"},
                {"require_special", get_or("password_require_special", "false") == "true"},
            };
        }

        res->writeHeader("Content-Type", "application/json")
            ->writeHeader("Access-Control-Allow-Origin", "*")
            ->end(resp.dump());
    });

    // Health check
    app.get("/api/health", [](auto* res, auto* req) {
        res->writeHeader("Content-Type", "application/json")
            ->writeHeader("Access-Control-Allow-Origin", "*")
            ->end(R"({"status":"ok"})");
    });

    // Start listening
    global_loop = uWS::Loop::get();
    global_close_connections = [&ws_handler]() { ws_handler.close_all(); };
    app.listen(config.server_port, [&config](auto* listen_socket) {
        if (listen_socket) {
            global_listen_socket = listen_socket;
            std::cout << "[Server] Listening on port " << config.server_port
                      << (config.has_ssl() ? " (HTTPS)" : " (HTTP)") << std::endl;
        } else {
            std::cerr << "[Server] Failed to listen on port " << config.server_port << std::endl;
            exit(1);
        }
    }).run();

    std::cout << "[Server] Shut down cleanly." << std::endl;
}

int main() {
    std::cout << "=== Chat Server ===" << std::endl;

    auto config = Config::from_env();
    std::cout << "[Config] Port: " << config.server_port << std::endl;

    // Create upload directory
    std::filesystem::create_directories(config.upload_dir);
    std::cout << "[Config] Upload dir: " << config.upload_dir << std::endl;

    // Connect to database
    Database db(config.pg_connection_string());
    db.run_migrations();
    db.set_all_users_offline();

    // Upload manager for chunked uploads
    UploadManager upload_manager(config.upload_dir);

    std::signal(SIGTERM, shutdown_handler);
    std::signal(SIGINT, shutdown_handler);

    if (config.has_ssl()) {
        std::cout << "[Config] SSL enabled (cert: " << config.ssl_cert_path
                  << ", key: " << config.ssl_key_path << ")" << std::endl;
        run_server(uWS::SSLApp({
            .key_file_name = config.ssl_key_path.c_str(),
            .cert_file_name = config.ssl_cert_path.c_str()
        }), config, db, upload_manager);
    } else {
        run_server(uWS::App(), config, db, upload_manager);
    }

    return 0;
}
