#pragma once
#include <App.h>
#include <nlohmann/json.hpp>
#include "db/database.h"

using json = nlohmann::json;

template <bool SSL>
struct UserHandler {
    Database& db;

    void register_routes(uWS::TemplatedApp<SSL>& app) {
        app.get("/api/users", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            auto users = db.list_users();
            json arr = json::array();
            for (const auto& u : users) {
                arr.push_back({{"id", u.id}, {"username", u.username},
                               {"display_name", u.display_name}, {"role", u.role},
                               {"is_online", u.is_online}, {"bio", u.bio},
                               {"status", u.status}});
            }
            res->writeHeader("Content-Type", "application/json")->end(arr.dump());
        });

        app.get("/api/users/me", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            auto user = db.find_user_by_id(user_id);
            if (!user) {
                res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"User not found"})");
                return;
            }

            json resp = {{"id", user->id}, {"username", user->username},
                         {"display_name", user->display_name}, {"role", user->role},
                         {"is_online", user->is_online}, {"bio", user->bio},
                         {"status", user->status}};
            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        });

        app.put("/api/users/me", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            res->onData([this, res, user_id, body = std::string()](std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;

                try {
                    auto j = json::parse(body);
                    auto current = db.find_user_by_id(user_id);
                    if (!current) {
                        res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                            ->end(R"({"error":"User not found"})");
                        return;
                    }

                    std::string display_name = j.value("display_name", current->display_name);
                    std::string bio = j.value("bio", current->bio);
                    std::string status = j.value("status", current->status);

                    auto updated = db.update_user_profile(user_id, display_name, bio, status);

                    json resp = {{"id", updated.id}, {"username", updated.username},
                                 {"display_name", updated.display_name}, {"role", updated.role},
                                 {"is_online", updated.is_online}, {"bio", updated.bio},
                                 {"status", updated.status}};
                    res->writeHeader("Content-Type", "application/json")->end(resp.dump());
                } catch (const std::exception& e) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->end(json({{"error", e.what()}}).dump());
                }
            });
            res->onAborted([]() {});
        });

        app.del("/api/users/me", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            try {
                db.delete_user(user_id);
                res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
            } catch (const std::exception& e) {
                res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                    ->end(json({{"error", e.what()}}).dump());
            }
        });

        app.post("/api/users/me/device-tokens", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            auto token = db.create_device_token(user_id, 15);
            json resp = {{"token", token}, {"expires_in_minutes", 15}};
            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        });

        app.get("/api/users/me/devices", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            auto keys = db.list_user_keys(user_id);
            json arr = json::array();
            for (const auto& k : keys) {
                arr.push_back({{"id", k.id}, {"device_name", k.device_name},
                               {"created_at", k.created_at}});
            }
            res->writeHeader("Content-Type", "application/json")->end(arr.dump());
        });

        app.del("/api/users/me/devices/:id", [this](auto* res, auto* req) {
            std::string user_id = get_user_id(res, req);
            if (user_id.empty()) return;

            std::string key_id(req->getParameter(0));
            try {
                db.remove_user_key(key_id, user_id);
                res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
            } catch (const std::exception& e) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->end(json({{"error", e.what()}}).dump());
            }
        });
    }

private:
    std::string get_user_id(uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req) {
        std::string token(req->getHeader("authorization"));
        if (token.rfind("Bearer ", 0) == 0) token = token.substr(7);
        auto user_id = db.validate_session(token);
        if (!user_id) {
            res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Unauthorized"})");
            return "";
        }
        return *user_id;
    }
};
