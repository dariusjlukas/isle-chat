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
void run_server(uWS::TemplatedApp<SSL>&& app, Config& config, Database& db) {
    WsHandler<SSL> ws_handler(db);
    AuthHandler<SSL> auth_handler{db, config};
    ChannelHandler<SSL> channel_handler{db, ws_handler};
    SpaceHandler<SSL> space_handler{db, ws_handler};
    UserHandler<SSL> user_handler{db, ws_handler, config};
    AdminHandler<SSL> admin_handler{db, config};
    FileHandler<SSL> file_handler{db, config};
    SearchHandler<SSL> search_handler{db};

    // CORS preflight
    app.options("/*", [](auto* res, auto* req) {
        res->writeHeader("Access-Control-Allow-Origin", "*")
            ->writeHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
            ->writeHeader("Access-Control-Allow-Headers", "Content-Type, Authorization")
            ->writeStatus("204")->end();
    });

    // Register routes (search before channels so /messages/around matches first)
    auth_handler.register_routes(app);
    search_handler.register_routes(app);
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

        // Auth methods
        json auth_methods = json::array({"passkey", "pki"});
        auto am = db.get_setting("auth_methods");
        if (am) {
            try { auth_methods = json::parse(*am); } catch (...) {}
        }
        resp["auth_methods"] = auth_methods;

        auto server_name = db.get_setting("server_name");
        resp["server_name"] = server_name.value_or("Isle Chat");

        auto reg_mode = db.get_setting("registration_mode");
        resp["registration_mode"] = reg_mode.value_or("invite");

        auto setup = db.get_setting("setup_completed");
        resp["setup_completed"] = (setup && *setup == "true");

        auto uploads = db.get_setting("file_uploads_enabled");
        resp["file_uploads_enabled"] = (!uploads || *uploads == "true");

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

    std::signal(SIGTERM, shutdown_handler);
    std::signal(SIGINT, shutdown_handler);

    if (config.has_ssl()) {
        std::cout << "[Config] SSL enabled (cert: " << config.ssl_cert_path
                  << ", key: " << config.ssl_key_path << ")" << std::endl;
        run_server(uWS::SSLApp({
            .key_file_name = config.ssl_key_path.c_str(),
            .cert_file_name = config.ssl_cert_path.c_str()
        }), config, db);
    } else {
        run_server(uWS::App(), config, db);
    }

    return 0;
}
