#include <App.h>
#include <iostream>
#include <filesystem>
#include "config.h"
#include "db/database.h"
#include "handlers/auth_handler.h"
#include "handlers/channel_handler.h"
#include "handlers/user_handler.h"
#include "handlers/admin_handler.h"
#include "handlers/file_handler.h"
#include "ws/ws_handler.h"

template <bool SSL>
void run_server(uWS::TemplatedApp<SSL>&& app, Config& config, Database& db) {
    WsHandler<SSL> ws_handler(db);
    AuthHandler<SSL> auth_handler{db, config};
    ChannelHandler<SSL> channel_handler{db, ws_handler};
    UserHandler<SSL> user_handler{db};
    AdminHandler<SSL> admin_handler{db, config};
    FileHandler<SSL> file_handler{db, config};

    // CORS preflight
    app.options("/*", [](auto* res, auto* req) {
        res->writeHeader("Access-Control-Allow-Origin", "*")
            ->writeHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
            ->writeHeader("Access-Control-Allow-Headers", "Content-Type, Authorization")
            ->writeStatus("204")->end();
    });

    // Register routes
    auth_handler.register_routes(app);
    channel_handler.register_routes(app);
    user_handler.register_routes(app);
    admin_handler.register_routes(app);
    file_handler.register_routes(app);
    ws_handler.register_routes(app);

    // Public config (non-sensitive settings for the frontend)
    app.get("/api/config", [&config](auto* res, auto* req) {
        json resp;
        resp["public_url"] = config.public_url;
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
    app.listen(config.server_port, [&config](auto* listen_socket) {
        if (listen_socket) {
            std::cout << "[Server] Listening on port " << config.server_port
                      << (config.has_ssl() ? " (HTTPS)" : " (HTTP)") << std::endl;
        } else {
            std::cerr << "[Server] Failed to listen on port " << config.server_port << std::endl;
            exit(1);
        }
    }).run();
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
