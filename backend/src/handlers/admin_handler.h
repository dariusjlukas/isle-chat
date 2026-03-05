#pragma once
#include <App.h>
#include <nlohmann/json.hpp>
#include "db/database.h"
#include "config.h"

using json = nlohmann::json;

template <bool SSL>
struct AdminHandler {
    Database& db;
    const Config& config;

    void register_routes(uWS::TemplatedApp<SSL>& app) {
        app.post("/api/admin/invites", [this](auto* res, auto* req) {
            auto user_id_copy = get_admin_id(res, req);
            std::string body;
            res->onData([this, res, user_id = std::move(user_id_copy), body = std::move(body)](
                std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                if (user_id.empty()) return;

                int expiry = 24;
                try {
                    auto j = json::parse(body);
                    expiry = j.value("expiry_hours", 24);
                } catch (...) {}

                auto token = db.create_invite(user_id, expiry);
                json resp = {{"token", token}};
                res->writeHeader("Content-Type", "application/json")->end(resp.dump());
            });
            res->onAborted([]() {});
        });

        app.get("/api/admin/invites", [this](auto* res, auto* req) {
            auto user_id = get_admin_id(res, req);
            if (user_id.empty()) return;

            auto invites = db.list_invites();
            json arr = json::array();
            for (const auto& inv : invites) {
                arr.push_back({{"id", inv.id}, {"token", inv.token},
                               {"created_by", inv.created_by_username},
                               {"used", inv.used},
                               {"expires_at", inv.expires_at},
                               {"created_at", inv.created_at}});
            }
            res->writeHeader("Content-Type", "application/json")->end(arr.dump());
        });

        app.get("/api/admin/requests", [this](auto* res, auto* req) {
            auto user_id = get_admin_id(res, req);
            if (user_id.empty()) return;

            auto requests = db.list_pending_requests();
            json arr = json::array();
            for (const auto& r : requests) {
                arr.push_back({{"id", r.id}, {"username", r.username},
                               {"display_name", r.display_name},
                               {"status", r.status}, {"created_at", r.created_at}});
            }
            res->writeHeader("Content-Type", "application/json")->end(arr.dump());
        });

        app.post("/api/admin/requests/:id/approve", [this](auto* res, auto* req) {
            auto admin_id = get_admin_id(res, req);
            if (admin_id.empty()) return;

            std::string request_id(req->getParameter("id"));
            handle_approve(res, request_id, admin_id);
        });

        app.post("/api/admin/requests/:id/deny", [this](auto* res, auto* req) {
            auto admin_id = get_admin_id(res, req);
            if (admin_id.empty()) return;

            std::string request_id(req->getParameter("id"));
            db.update_join_request(request_id, "denied", admin_id);
            res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
        });

        app.get("/api/admin/settings", [this](auto* res, auto* req) {
            auto user_id = get_admin_id(res, req);
            if (user_id.empty()) return;

            auto max_file = db.get_setting("max_file_size");
            auto max_storage = db.get_setting("max_storage_size");
            int64_t storage_used = db.get_total_file_size();

            json resp = {
                {"max_file_size", max_file ? std::stoll(*max_file) : config.max_file_size},
                {"max_storage_size", max_storage ? std::stoll(*max_storage) : 0},
                {"storage_used", storage_used}
            };
            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        });

        app.put("/api/admin/settings", [this](auto* res, auto* req) {
            auto admin_id_copy = get_admin_id(res, req);
            std::string body;
            res->onData([this, res, admin_id = std::move(admin_id_copy), body = std::move(body)](
                std::string_view data, bool last) mutable {
                body.append(data);
                if (!last) return;
                if (admin_id.empty()) return;

                try {
                    auto j = json::parse(body);
                    if (j.contains("max_file_size")) {
                        db.set_setting("max_file_size", std::to_string(j["max_file_size"].get<int64_t>()));
                    }
                    if (j.contains("max_storage_size")) {
                        db.set_setting("max_storage_size", std::to_string(j["max_storage_size"].get<int64_t>()));
                    }
                    res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
                } catch (const std::exception& e) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->end(json{{"error", e.what()}}.dump());
                }
            });
            res->onAborted([]() {});
        });
    }

private:
    std::string get_admin_id(uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req) {
        std::string token(req->getHeader("authorization"));
        if (token.rfind("Bearer ", 0) == 0) token = token.substr(7);
        auto user_id = db.validate_session(token);
        if (!user_id) {
            res->writeStatus("401")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Unauthorized"})");
            return "";
        }
        auto user = db.find_user_by_id(*user_id);
        if (!user || user->role != "admin") {
            res->writeStatus("403")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Admin access required"})");
            return "";
        }
        return *user_id;
    }

    void handle_approve(uWS::HttpResponse<SSL>* res, const std::string& request_id,
                         const std::string& admin_id) {
        auto request = db.get_join_request(request_id);
        if (!request) {
            res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Request not found"})");
            return;
        }

        if (request->status != "pending") {
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Request already processed"})");
            return;
        }

        try {
            // Create the user
            auto user = db.create_user(request->username, request->display_name,
                                       request->public_key, "user");
            db.update_join_request(request_id, "approved", admin_id);

            // Add new user to general channel
            auto general = db.find_general_channel();
            if (general) {
                db.add_channel_member(general->id, user.id, general->default_role);
            }

            json resp = {{"ok", true}, {"user_id", user.id}};
            res->writeHeader("Content-Type", "application/json")->end(resp.dump());
        } catch (const pqxx::unique_violation&) {
            res->writeStatus("409")->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Username already taken"})");
        } catch (const std::exception& e) {
            res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                ->end(json({{"error", e.what()}}).dump());
        }
    }
};
