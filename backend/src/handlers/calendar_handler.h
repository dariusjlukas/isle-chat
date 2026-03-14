#pragma once
#include <App.h>
#include <nlohmann/json.hpp>
#include "db/database.h"
#include "config.h"
#include "handlers/handler_utils.h"

using json = nlohmann::json;

template <bool SSL>
struct CalendarHandler {
    Database& db;
    const Config& config;

    void register_routes(uWS::TemplatedApp<SSL>& app);

private:
    std::string get_user_id(uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req);
    bool check_space_access(uWS::HttpResponse<SSL>* res, const std::string& space_id,
                            const std::string& user_id);
    // Returns effective permission: "owner", "edit", "view"
    std::string get_access_level(const std::string& space_id, const std::string& user_id);
    // Returns false (and writes 403) if user doesn't have required_level or above
    bool require_permission(uWS::HttpResponse<SSL>* res, const std::string& space_id,
                            const std::string& user_id, const std::string& required_level);
    static int perm_rank(const std::string& p);
};
