#pragma once
#include <App.h>
#include <nlohmann/json.hpp>
#include "config.h"
#include "db/database.h"
#include "db/db_thread_pool.h"
#include "handlers/handler_utils.h"
#include "upload_manager.h"

using json = nlohmann::json;

template <bool SSL>
struct SpaceFileHandler {
  Database& db;
  const Config& config;
  UploadManager& uploads;
  uWS::Loop* loop_;
  DbThreadPool& pool_;

  void register_routes(uWS::TemplatedApp<SSL>& app);

private:
  std::string get_user_id(uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req);
  bool check_space_access(
    uWS::HttpResponse<SSL>* res,
    const std::string& space_id,
    const std::string& user_id,
    const std::string& origin);
  // Sync version: returns true/false without writing to res (for use in pool threads)
  bool check_space_access_sync(const std::string& space_id, const std::string& user_id);
  // Returns effective permission: "owner", "edit", "view"
  std::string get_access_level(
    const std::string& space_id, const std::string& file_id, const std::string& user_id);
  // Returns false (and writes 403) if user doesn't have required_level or above
  bool require_permission(
    uWS::HttpResponse<SSL>* res,
    const std::string& space_id,
    const std::string& file_id,
    const std::string& user_id,
    const std::string& required_level,
    const std::string& origin);
  // Sync version: returns true/false without writing to res (for use in pool threads)
  bool require_permission_sync(
    const std::string& space_id,
    const std::string& file_id,
    const std::string& user_id,
    const std::string& required_level);
  static int perm_rank(const std::string& p);
  bool check_personal_total_limit(
    uWS::HttpResponse<SSL>* res,
    const std::string& space_id,
    int64_t upload_size,
    const std::string& origin);
  // Sync version: returns empty string if OK, or error JSON if limit exceeded
  std::string check_personal_total_limit_sync(const std::string& space_id, int64_t upload_size);
};
