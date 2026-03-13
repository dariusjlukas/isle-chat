#include "handlers/space_file_handler.h"
#include "handlers/file_access_utils.h"
#include "handlers/format_utils.h"
#include <fstream>
#include <filesystem>
#include <pqxx/pqxx>

using json = nlohmann::json;

static json space_file_to_json(const SpaceFile& f, const std::string& username = "") {
    return {
        {"id", f.id}, {"space_id", f.space_id},
        {"parent_id", f.parent_id.empty() ? json(nullptr) : json(f.parent_id)},
        {"name", f.name}, {"is_folder", f.is_folder},
        {"file_size", f.file_size}, {"mime_type", f.mime_type},
        {"created_by", f.created_by},
        {"created_by_username", username.empty() ? f.created_by_username : username},
        {"created_at", f.created_at}, {"updated_at", f.updated_at}
    };
}

static json space_file_version_to_json(const SpaceFileVersion& v) {
    return {
        {"id", v.id}, {"file_id", v.file_id},
        {"version_number", v.version_number}, {"file_size", v.file_size},
        {"mime_type", v.mime_type}, {"uploaded_by", v.uploaded_by},
        {"uploaded_by_username", v.uploaded_by_username},
        {"created_at", v.created_at}
    };
}

template <bool SSL>
void SpaceFileHandler<SSL>::register_routes(uWS::TemplatedApp<SSL>& app) {

    // List files in a folder (root if no parent_id)
    app.get("/api/spaces/:id/files", [this](auto* res, auto* req) {
        std::string user_id = get_user_id(res, req);
        if (user_id.empty()) return;
        std::string space_id(req->getParameter("id"));

        if (!check_space_access(res, space_id, user_id)) return;

        std::string parent_id(req->getQuery("parent_id"));
        auto files = db.list_space_files(space_id, parent_id);

        json arr = json::array();
        for (const auto& f : files) {
            arr.push_back(space_file_to_json(f));
        }

        // Include breadcrumb path if inside a subfolder
        json path_arr = json::array();
        if (!parent_id.empty()) {
            auto path = db.get_space_file_path(parent_id);
            for (const auto& p : path) {
                path_arr.push_back({{"id", p.id}, {"name", p.name}});
            }
        }

        // Include caller's effective permission on this folder
        std::string my_perm = get_access_level(space_id, parent_id, user_id);

        json resp = {{"files", arr}, {"path", path_arr}, {"my_permission", my_perm}};
        res->writeHeader("Content-Type", "application/json")
            ->writeHeader("Access-Control-Allow-Origin", "*")
            ->end(resp.dump());
    });

    // Create folder
    app.post("/api/spaces/:id/files/folder", [this](auto* res, auto* req) {
        auto user_id_copy = get_user_id(res, req);
        std::string space_id(req->getParameter("id"));
        std::string body;
        res->onData([this, res, user_id = std::move(user_id_copy),
                     space_id = std::move(space_id), body = std::move(body)](
            std::string_view data, bool last) mutable {
            body.append(data);
            if (!last) return;
            if (user_id.empty()) return;

            if (!check_space_access(res, space_id, user_id)) return;

            try {
                auto j = json::parse(body);
                std::string name = j.at("name");
                std::string parent_id = j.value("parent_id", "");

                if (name.empty() || name.length() > 255) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(R"({"error":"Invalid folder name"})");
                    return;
                }

                // Permission check: need "edit" on parent
                if (!require_permission(res, space_id, parent_id, user_id, "edit")) return;

                if (db.space_file_name_exists(space_id, parent_id, name)) {
                    res->writeStatus("409")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(R"({"error":"A file or folder with this name already exists"})");
                    return;
                }

                if (!parent_id.empty()) {
                    auto parent = db.find_space_file(parent_id);
                    if (!parent || parent->space_id != space_id || !parent->is_folder || parent->is_deleted) {
                        res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                            ->writeHeader("Access-Control-Allow-Origin", "*")
                            ->end(R"({"error":"Invalid parent folder"})");
                        return;
                    }
                }

                auto folder = db.create_space_folder(space_id, parent_id, name, user_id);
                auto creator = db.find_user_by_id(user_id);
                json resp = space_file_to_json(folder, creator ? creator->username : "");
                res->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(resp.dump());
            } catch (const pqxx::unique_violation&) {
                res->writeStatus("409")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(R"({"error":"A file or folder with this name already exists"})");
            } catch (const std::exception& e) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(json({{"error", e.what()}}).dump());
            }
        });
        res->onAborted([]() {});
    });

    // Upload file
    app.post("/api/spaces/:id/files/upload", [this](auto* res, auto* req) {
        std::string user_id = get_user_id(res, req);
        if (user_id.empty()) return;
        std::string space_id(req->getParameter("id"));
        std::string parent_id(req->getQuery("parent_id"));
        std::string filename(req->getQuery("filename"));
        std::string content_type(req->getQuery("content_type"));

        if (filename.empty()) filename = "upload";
        if (content_type.empty()) content_type = "application/octet-stream";

        int64_t max_size = file_access_utils::parse_max_file_size(
            db.get_setting("max_file_size"), config.max_file_size);

        auto body = std::make_shared<std::string>();

        res->onData([this, res, body, max_size, space_id, parent_id, user_id,
                     filename, content_type](std::string_view data, bool last) mutable {
            body->append(data);

            if (static_cast<int64_t>(body->size()) > max_size) {
                    std::string msg = file_access_utils::file_too_large_message(max_size);
                res->writeStatus("413")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(json{{"error", msg}}.dump());
                return;
            }

            if (!last) return;

            if (!check_space_access(res, space_id, user_id)) return;

            // Permission check: need "edit" on parent
            if (!require_permission(res, space_id, parent_id, user_id, "edit")) return;

            if (!parent_id.empty()) {
                auto parent = db.find_space_file(parent_id);
                if (!parent || parent->space_id != space_id || !parent->is_folder || parent->is_deleted) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(R"({"error":"Invalid parent folder"})");
                    return;
                }
            }

            if (db.space_file_name_exists(space_id, parent_id, filename)) {
                res->writeStatus("409")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(R"({"error":"A file or folder with this name already exists"})");
                return;
            }

            try {
                // Check server storage limit
                int64_t max_storage = file_access_utils::parse_max_storage_size(
                    db.get_setting("max_storage_size"));
                if (max_storage > 0 && file_access_utils::exceeds_storage_limit(
                        max_storage, db.get_total_file_size(), static_cast<int64_t>(body->size()))) {
                    res->writeStatus("413")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(R"({"error":"Server storage limit reached"})");
                    return;
                }

                // Check space storage limit
                int64_t space_limit = file_access_utils::parse_space_storage_limit(
                    db.get_setting("space_storage_limit_" + space_id));
                if (space_limit > 0 && file_access_utils::exceeds_storage_limit(
                        space_limit, db.get_space_storage_used(space_id),
                        static_cast<int64_t>(body->size()))) {
                    res->writeStatus("413")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(R"({"error":"Space storage limit reached"})");
                    return;
                }

                std::string disk_file_id = format_utils::random_hex(32);
                std::string path = config.upload_dir + "/" + disk_file_id;
                std::ofstream out(path, std::ios::binary);
                if (!out) {
                    res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(R"({"error":"Failed to save file"})");
                    return;
                }
                out.write(body->data(), body->size());
                out.close();

                int64_t file_size = static_cast<int64_t>(body->size());

                auto file = db.create_space_file(space_id, parent_id, filename,
                                                  disk_file_id, file_size, content_type, user_id);
                auto creator = db.find_user_by_id(user_id);
                json resp = space_file_to_json(file, creator ? creator->username : "");
                res->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(resp.dump());
            } catch (const pqxx::unique_violation&) {
                res->writeStatus("409")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(R"({"error":"A file or folder with this name already exists"})");
            } catch (const std::exception& e) {
                res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(json({{"error", e.what()}}).dump());
            }
        });
        res->onAborted([]() {});
    });

    // Get file/folder details
    app.get("/api/spaces/:spaceId/files/:fileId", [this](auto* res, auto* req) {
        std::string user_id = get_user_id(res, req);
        if (user_id.empty()) return;
        std::string space_id(req->getParameter(0));
        std::string file_id(req->getParameter(1));

        if (!check_space_access(res, space_id, user_id)) return;

        auto file = db.find_space_file(file_id);
        if (!file || file->space_id != space_id || file->is_deleted) {
            res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                ->writeHeader("Access-Control-Allow-Origin", "*")
                ->end(R"({"error":"File not found"})");
            return;
        }

        std::string my_perm = get_access_level(space_id, file_id, user_id);
        json resp = space_file_to_json(*file);
        resp["my_permission"] = my_perm;

        auto path = db.get_space_file_path(file_id);
        json path_arr = json::array();
        for (const auto& p : path) {
            path_arr.push_back({{"id", p.id}, {"name", p.name}});
        }
        resp["path"] = path_arr;

        res->writeHeader("Content-Type", "application/json")
            ->writeHeader("Access-Control-Allow-Origin", "*")
            ->end(resp.dump());
    });

    // Download file
    app.get("/api/spaces/:spaceId/files/:fileId/download", [this](auto* res, auto* req) {
        std::string user_id = get_user_id(res, req);
        if (user_id.empty()) return;
        std::string space_id(req->getParameter(0));
        std::string file_id(req->getParameter(1));

        if (!check_space_access(res, space_id, user_id)) return;

        auto file = db.find_space_file(file_id);
        if (!file || file->space_id != space_id || file->is_deleted || file->is_folder) {
            res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                ->writeHeader("Access-Control-Allow-Origin", "*")
                ->end(R"({"error":"File not found"})");
            return;
        }

        std::string path = config.upload_dir + "/" + file->disk_file_id;
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                ->writeHeader("Access-Control-Allow-Origin", "*")
                ->end(R"({"error":"File data not found on disk"})");
            return;
        }

        std::string data((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());

        std::string content_type = file->mime_type.empty() ? "application/octet-stream" : file->mime_type;
        std::string disposition = file_access_utils::attachment_disposition(file->name);

        res->writeHeader("Content-Type", content_type)
            ->writeHeader("Content-Disposition", disposition)
            ->writeHeader("Access-Control-Allow-Origin", "*")
            ->end(data);
    });

    // Update file/folder (rename, move)
    app.put("/api/spaces/:spaceId/files/:fileId", [this](auto* res, auto* req) {
        auto user_id_copy = get_user_id(res, req);
        std::string space_id(req->getParameter(0));
        std::string file_id(req->getParameter(1));
        std::string body;
        res->onData([this, res, user_id = std::move(user_id_copy),
                     space_id = std::move(space_id), file_id = std::move(file_id),
                     body = std::move(body)](
            std::string_view data, bool last) mutable {
            body.append(data);
            if (!last) return;
            if (user_id.empty()) return;

            if (!check_space_access(res, space_id, user_id)) return;

            // Permission check: need "edit" on the file
            if (!require_permission(res, space_id, file_id, user_id, "edit")) return;

            auto file = db.find_space_file(file_id);
            if (!file || file->space_id != space_id || file->is_deleted) {
                res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(R"({"error":"File not found"})");
                return;
            }

            try {
                auto j = json::parse(body);

                if (j.contains("name")) {
                    std::string new_name = j["name"];
                    if (new_name.empty() || new_name.length() > 255) {
                        res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                            ->writeHeader("Access-Control-Allow-Origin", "*")
                            ->end(R"({"error":"Invalid name"})");
                        return;
                    }
                    if (db.space_file_name_exists(space_id, file->parent_id, new_name, file_id)) {
                        res->writeStatus("409")->writeHeader("Content-Type", "application/json")
                            ->writeHeader("Access-Control-Allow-Origin", "*")
                            ->end(R"({"error":"A file or folder with this name already exists"})");
                        return;
                    }
                    db.rename_space_file(file_id, new_name);
                }

                if (j.contains("parent_id")) {
                    std::string new_parent = j["parent_id"].is_null() ? "" : j["parent_id"].get<std::string>();
                    if (!new_parent.empty()) {
                        if (new_parent == file_id) {
                            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                                ->writeHeader("Access-Control-Allow-Origin", "*")
                                ->end(R"({"error":"Cannot move a folder into itself"})");
                            return;
                        }
                        auto parent = db.find_space_file(new_parent);
                        if (!parent || parent->space_id != space_id || !parent->is_folder || parent->is_deleted) {
                            res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                                ->writeHeader("Access-Control-Allow-Origin", "*")
                                ->end(R"({"error":"Invalid parent folder"})");
                            return;
                        }
                        if (file->is_folder) {
                            auto path = db.get_space_file_path(new_parent);
                            for (const auto& p : path) {
                                if (p.id == file_id) {
                                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                                        ->writeHeader("Access-Control-Allow-Origin", "*")
                                        ->end(R"({"error":"Cannot move a folder into its own descendant"})");
                                    return;
                                }
                            }
                        }
                    }
                    std::string current_name = j.value("name", file->name);
                    if (db.space_file_name_exists(space_id, new_parent, current_name, file_id)) {
                        res->writeStatus("409")->writeHeader("Content-Type", "application/json")
                            ->writeHeader("Access-Control-Allow-Origin", "*")
                            ->end(R"({"error":"A file or folder with this name already exists in the destination"})");
                        return;
                    }
                    db.move_space_file(file_id, new_parent);
                }

                auto updated = db.find_space_file(file_id);
                json resp = space_file_to_json(*updated);
                res->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(resp.dump());
            } catch (const pqxx::unique_violation&) {
                res->writeStatus("409")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(R"({"error":"A file or folder with this name already exists"})");
            } catch (const std::exception& e) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(json({{"error", e.what()}}).dump());
            }
        });
        res->onAborted([]() {});
    });

    // Delete file/folder (soft delete) — requires "owner" permission
    app.del("/api/spaces/:spaceId/files/:fileId", [this](auto* res, auto* req) {
        std::string user_id = get_user_id(res, req);
        if (user_id.empty()) return;
        std::string space_id(req->getParameter(0));
        std::string file_id(req->getParameter(1));

        if (!check_space_access(res, space_id, user_id)) return;
        if (!require_permission(res, space_id, file_id, user_id, "owner")) return;

        auto file = db.find_space_file(file_id);
        if (!file || file->space_id != space_id || file->is_deleted) {
            res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                ->writeHeader("Access-Control-Allow-Origin", "*")
                ->end(R"({"error":"File not found"})");
            return;
        }

        db.soft_delete_space_file(file_id);

        res->writeHeader("Content-Type", "application/json")
            ->writeHeader("Access-Control-Allow-Origin", "*")
            ->end(R"({"ok":true})");
    });

    // Get space storage usage
    app.get("/api/spaces/:id/storage", [this](auto* res, auto* req) {
        std::string user_id = get_user_id(res, req);
        if (user_id.empty()) return;
        std::string space_id(req->getParameter("id"));

        if (!check_space_access(res, space_id, user_id)) return;

        int64_t used = db.get_space_storage_used(space_id);

        json resp = {{"used", used}};
        res->writeHeader("Content-Type", "application/json")
            ->writeHeader("Access-Control-Allow-Origin", "*")
            ->end(resp.dump());
    });

    // --- File Permissions endpoints ---

    // Get permissions for a file/folder
    app.get("/api/spaces/:spaceId/files/:fileId/permissions", [this](auto* res, auto* req) {
        std::string user_id = get_user_id(res, req);
        if (user_id.empty()) return;
        std::string space_id(req->getParameter(0));
        std::string file_id(req->getParameter(1));

        if (!check_space_access(res, space_id, user_id)) return;

        auto file = db.find_space_file(file_id);
        if (!file || file->space_id != space_id || file->is_deleted) {
            res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                ->writeHeader("Access-Control-Allow-Origin", "*")
                ->end(R"({"error":"File not found"})");
            return;
        }

        auto perms = db.get_file_permissions(file_id);
        json arr = json::array();
        for (const auto& p : perms) {
            arr.push_back({
                {"id", p.id}, {"file_id", p.file_id},
                {"user_id", p.user_id}, {"username", p.username},
                {"display_name", p.display_name}, {"permission", p.permission},
                {"granted_by", p.granted_by}, {"granted_by_username", p.granted_by_username},
                {"created_at", p.created_at}
            });
        }

        std::string my_perm = get_access_level(space_id, file_id, user_id);

        json resp = {{"permissions", arr}, {"my_permission", my_perm}};
        res->writeHeader("Content-Type", "application/json")
            ->writeHeader("Access-Control-Allow-Origin", "*")
            ->end(resp.dump());
    });

    // Set permission for a user on a file/folder — requires "owner" on the file
    app.put("/api/spaces/:spaceId/files/:fileId/permissions", [this](auto* res, auto* req) {
        auto user_id_copy = get_user_id(res, req);
        std::string space_id(req->getParameter(0));
        std::string file_id(req->getParameter(1));
        std::string body;
        res->onData([this, res, user_id = std::move(user_id_copy),
                     space_id = std::move(space_id), file_id = std::move(file_id),
                     body = std::move(body)](
            std::string_view data, bool last) mutable {
            body.append(data);
            if (!last) return;
            if (user_id.empty()) return;

            if (!check_space_access(res, space_id, user_id)) return;
            if (!require_permission(res, space_id, file_id, user_id, "owner")) return;

            try {
                auto j = json::parse(body);
                std::string target_user_id = j.at("user_id");
                std::string permission = j.at("permission");

                if (permission != "owner" && permission != "edit" && permission != "view") {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(R"json({"error":"Invalid permission level (must be owner, edit, or view)"})json");
                    return;
                }

                // Verify target user exists and is a space member
                auto target = db.find_user_by_id(target_user_id);
                if (!target) {
                    res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(R"({"error":"User not found"})");
                    return;
                }

                // Check monotonic escalation: new permission must be >= parent's effective permission
                auto file = db.find_space_file(file_id);
                if (file && !file->parent_id.empty()) {
                    std::string parent_perm = db.get_effective_file_permission(file->parent_id, target_user_id);
                    if (perm_rank(permission) < perm_rank(parent_perm)) {
                        res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                            ->writeHeader("Access-Control-Allow-Origin", "*")
                            ->end(json({{"error", "Cannot set permission below inherited level (" + parent_perm + ")"}}).dump());
                        return;
                    }
                }

                db.set_file_permission(file_id, target_user_id, permission, user_id);

                res->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(R"({"ok":true})");
            } catch (const std::exception& e) {
                res->writeStatus("400")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(json({{"error", e.what()}}).dump());
            }
        });
        res->onAborted([]() {});
    });

    // Remove a specific user's permission on a file
    app.del("/api/spaces/:spaceId/files/:fileId/permissions/:userId", [this](auto* res, auto* req) {
        std::string user_id = get_user_id(res, req);
        if (user_id.empty()) return;
        std::string space_id(req->getParameter(0));
        std::string file_id(req->getParameter(1));
        std::string target_user_id(req->getParameter(2));

        if (!check_space_access(res, space_id, user_id)) return;
        if (!require_permission(res, space_id, file_id, user_id, "owner")) return;

        db.remove_file_permission(file_id, target_user_id);

        res->writeHeader("Content-Type", "application/json")
            ->writeHeader("Access-Control-Allow-Origin", "*")
            ->end(R"({"ok":true})");
    });

    // --- Version endpoints ---

    // List versions for a file
    app.get("/api/spaces/:spaceId/files/:fileId/versions", [this](auto* res, auto* req) {
        std::string user_id = get_user_id(res, req);
        if (user_id.empty()) return;
        std::string space_id(req->getParameter(0));
        std::string file_id(req->getParameter(1));

        if (!check_space_access(res, space_id, user_id)) return;

        auto file = db.find_space_file(file_id);
        if (!file || file->space_id != space_id || file->is_deleted || file->is_folder) {
            res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                ->writeHeader("Access-Control-Allow-Origin", "*")
                ->end(R"({"error":"File not found"})");
            return;
        }

        auto versions = db.list_file_versions(file_id);
        json arr = json::array();
        for (const auto& v : versions) {
            arr.push_back(space_file_version_to_json(v));
        }

        json resp = {{"versions", arr}};
        res->writeHeader("Content-Type", "application/json")
            ->writeHeader("Access-Control-Allow-Origin", "*")
            ->end(resp.dump());
    });

    // Upload new version of a file — requires "edit" permission
    app.post("/api/spaces/:spaceId/files/:fileId/versions", [this](auto* res, auto* req) {
        std::string user_id = get_user_id(res, req);
        if (user_id.empty()) return;
        std::string space_id(req->getParameter(0));
        std::string file_id(req->getParameter(1));

        auto max_file_setting = db.get_setting("max_file_size");
        int64_t max_size = file_access_utils::parse_max_file_size(
            max_file_setting, config.max_file_size);

        auto body = std::make_shared<std::string>();

        res->onData([this, res, body, max_size, space_id, file_id, user_id](
            std::string_view data, bool last) mutable {
            body->append(data);

            if (static_cast<int64_t>(body->size()) > max_size) {
                    std::string msg = file_access_utils::file_too_large_message(max_size);
                res->writeStatus("413")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(json{{"error", msg}}.dump());
                return;
            }

            if (!last) return;

            if (!check_space_access(res, space_id, user_id)) return;
            if (!require_permission(res, space_id, file_id, user_id, "edit")) return;

            auto file = db.find_space_file(file_id);
            if (!file || file->space_id != space_id || file->is_deleted || file->is_folder) {
                res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(R"({"error":"File not found"})");
                return;
            }

            try {
                // Check server storage limit
                int64_t max_storage = file_access_utils::parse_max_storage_size(
                    db.get_setting("max_storage_size"));
                if (max_storage > 0 && file_access_utils::exceeds_storage_limit(
                        max_storage, db.get_total_file_size(), static_cast<int64_t>(body->size()))) {
                    res->writeStatus("413")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(R"({"error":"Server storage limit reached"})");
                    return;
                }

                // Check space storage limit
                int64_t space_limit = file_access_utils::parse_space_storage_limit(
                    db.get_setting("space_storage_limit_" + space_id));
                if (space_limit > 0 && file_access_utils::exceeds_storage_limit(
                        space_limit, db.get_space_storage_used(space_id),
                        static_cast<int64_t>(body->size()))) {
                    res->writeStatus("413")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(R"({"error":"Space storage limit reached"})");
                    return;
                }

                std::string disk_file_id = format_utils::random_hex(32);
                std::string path = config.upload_dir + "/" + disk_file_id;
                std::ofstream out(path, std::ios::binary);
                if (!out) {
                    res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                        ->writeHeader("Access-Control-Allow-Origin", "*")
                        ->end(R"({"error":"Failed to save file"})");
                    return;
                }
                out.write(body->data(), body->size());
                out.close();

                int64_t file_size = static_cast<int64_t>(body->size());
                std::string mime_type = file->mime_type;

                auto version = db.create_file_version(file_id, disk_file_id, file_size, mime_type, user_id);
                json resp = space_file_version_to_json(version);
                res->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(resp.dump());
            } catch (const std::exception& e) {
                res->writeStatus("500")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(json({{"error", e.what()}}).dump());
            }
        });
        res->onAborted([]() {});
    });

    // Download a specific version
    app.get("/api/spaces/:spaceId/files/:fileId/versions/:versionId/download", [this](auto* res, auto* req) {
        std::string user_id = get_user_id(res, req);
        if (user_id.empty()) return;
        std::string space_id(req->getParameter(0));
        std::string file_id(req->getParameter(1));
        std::string version_id(req->getParameter(2));

        if (!check_space_access(res, space_id, user_id)) return;

        auto file = db.find_space_file(file_id);
        if (!file || file->space_id != space_id || file->is_deleted || file->is_folder) {
            res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                ->writeHeader("Access-Control-Allow-Origin", "*")
                ->end(R"({"error":"File not found"})");
            return;
        }

        auto version = db.get_file_version(version_id);
        if (!version || version->file_id != file_id) {
            res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                ->writeHeader("Access-Control-Allow-Origin", "*")
                ->end(R"({"error":"Version not found"})");
            return;
        }

        std::string path = config.upload_dir + "/" + version->disk_file_id;
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                ->writeHeader("Access-Control-Allow-Origin", "*")
                ->end(R"({"error":"Version data not found on disk"})");
            return;
        }

        std::string data((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());

        std::string content_type = version->mime_type.empty() ? "application/octet-stream" : version->mime_type;
        std::string disposition = file_access_utils::versioned_attachment_disposition(
            version->version_number, file->name);

        res->writeHeader("Content-Type", content_type)
            ->writeHeader("Content-Disposition", disposition)
            ->writeHeader("Access-Control-Allow-Origin", "*")
            ->end(data);
    });

    // Revert to a specific version — requires "edit" permission
    app.post("/api/spaces/:spaceId/files/:fileId/versions/:versionId/revert", [this](auto* res, auto* req) {
        auto user_id_copy = get_user_id(res, req);
        std::string space_id(req->getParameter(0));
        std::string file_id(req->getParameter(1));
        std::string version_id(req->getParameter(2));

        res->onData([this, res, user_id = std::move(user_id_copy),
                     space_id = std::move(space_id), file_id = std::move(file_id),
                     version_id = std::move(version_id)](
            std::string_view, bool last) mutable {
            if (!last) return;
            if (user_id.empty()) return;

            if (!check_space_access(res, space_id, user_id)) return;
            if (!require_permission(res, space_id, file_id, user_id, "edit")) return;

            auto file = db.find_space_file(file_id);
            if (!file || file->space_id != space_id || file->is_deleted || file->is_folder) {
                res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(R"({"error":"File not found"})");
                return;
            }

            auto version = db.get_file_version(version_id);
            if (!version || version->file_id != file_id) {
                res->writeStatus("404")->writeHeader("Content-Type", "application/json")
                    ->writeHeader("Access-Control-Allow-Origin", "*")
                    ->end(R"({"error":"Version not found"})");
                return;
            }

            // Create a new version that copies the old version's data
            auto new_version = db.create_file_version(file_id, version->disk_file_id,
                                                        version->file_size, version->mime_type, user_id);
            json resp = space_file_version_to_json(new_version);

            res->writeHeader("Content-Type", "application/json")
                ->writeHeader("Access-Control-Allow-Origin", "*")
                ->end(resp.dump());
        });
        res->onAborted([]() {});
    });

    // --- Admin storage endpoint ---

    app.get("/api/admin/storage", [this](auto* res, auto* req) {
        std::string user_id = validate_admin_or_403(res, req, db);
        if (user_id.empty()) return;

        auto spaces = db.get_all_space_storage();
        int64_t total_used = db.get_total_file_size();

        json arr = json::array();
        for (const auto& s : spaces) {
            arr.push_back({
                {"space_id", s.space_id}, {"space_name", s.space_name},
                {"storage_used", s.storage_used}, {"storage_limit", s.storage_limit},
                {"file_count", s.file_count}
            });
        }

        json resp = {{"spaces", arr}, {"total_used", total_used}};
        res->writeHeader("Content-Type", "application/json")
            ->writeHeader("Access-Control-Allow-Origin", "*")
            ->end(resp.dump());
    });
}

template <bool SSL>
std::string SpaceFileHandler<SSL>::get_user_id(uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req) {
    return validate_session_or_401(res, req, db);
}

template <bool SSL>
bool SpaceFileHandler<SSL>::check_space_access(uWS::HttpResponse<SSL>* res,
                                                const std::string& space_id,
                                                const std::string& user_id) {
    if (db.is_space_member(space_id, user_id)) return true;
    auto user = db.find_user_by_id(user_id);
    if (user && (user->role == "admin" || user->role == "owner")) return true;

    res->writeStatus("403")->writeHeader("Content-Type", "application/json")
        ->writeHeader("Access-Control-Allow-Origin", "*")
        ->end(R"({"error":"Not a member of this space"})");
    return false;
}

template <bool SSL>
std::string SpaceFileHandler<SSL>::get_access_level(const std::string& space_id,
                                                      const std::string& file_id,
                                                      const std::string& user_id) {
    // Server admin/owner → full access
    auto user = db.find_user_by_id(user_id);
    if (user && (user->role == "admin" || user->role == "owner")) return "owner";

    // Space admin/owner → full access
    auto space_role = db.get_space_member_role(space_id, user_id);
    if (space_role == "admin" || space_role == "owner") return "owner";

    // Base from space role
    std::string base = (space_role == "write") ? "edit" : "view";

    if (file_id.empty()) return base;

    // Get file-level permission (may be higher due to explicit grants)
    auto file_perm = db.get_effective_file_permission(file_id, user_id);

    // Monotonic escalation: return the higher of base and file permission
    return (perm_rank(file_perm) >= perm_rank(base)) ? file_perm : base;
}

template <bool SSL>
bool SpaceFileHandler<SSL>::require_permission(uWS::HttpResponse<SSL>* res,
                                                const std::string& space_id,
                                                const std::string& file_id,
                                                const std::string& user_id,
                                                const std::string& required_level) {
    auto level = get_access_level(space_id, file_id, user_id);
    if (perm_rank(level) >= perm_rank(required_level)) return true;

    res->writeStatus("403")->writeHeader("Content-Type", "application/json")
        ->writeHeader("Access-Control-Allow-Origin", "*")
        ->end(json({{"error", "Requires " + required_level + " permission"}}).dump());
    return false;
}

template <bool SSL>
int SpaceFileHandler<SSL>::perm_rank(const std::string& p) {
    if (p == "owner") return 2;
    if (p == "edit") return 1;
    return 0;
}

template struct SpaceFileHandler<false>;
template struct SpaceFileHandler<true>;
