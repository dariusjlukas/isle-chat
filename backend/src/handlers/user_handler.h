#pragma once
#include <App.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <random>
#include <sstream>
#include <iomanip>
#include "db/database.h"
#include "ws/ws_handler.h"
#include "auth/webauthn.h"
#include "auth/totp.h"
#include "config.h"
#include "handlers/handler_utils.h"

using json = nlohmann::json;

template <bool SSL>
struct UserHandler {
    Database& db;
    WsHandler<SSL>& ws;
    const Config& config;

    void register_routes(uWS::TemplatedApp<SSL>& app);

private:
    std::string get_user_id(uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req);
};
