#pragma once
#include <App.h>
#include <memory>
#include <nlohmann/json.hpp>
#include "config.h"
#include "db/database.h"
#include "db/db_thread_pool.h"
#include "handlers/handler_utils.h"
#include "upload_manager.h"

using json = nlohmann::json;

template <bool SSL>
struct WikiHandler {
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
    std::shared_ptr<bool> aborted,
    const std::string& space_id,
    const std::string& user_id,
    const std::string& origin);
  std::string get_access_level(const std::string& space_id, const std::string& user_id);
  std::string get_page_access_level(
    const std::string& space_id, const std::string& page_id, const std::string& user_id);
  bool require_permission(
    uWS::HttpResponse<SSL>* res,
    std::shared_ptr<bool> aborted,
    const std::string& space_id,
    const std::string& user_id,
    const std::string& required_level,
    const std::string& origin);
  bool require_page_permission(
    uWS::HttpResponse<SSL>* res,
    std::shared_ptr<bool> aborted,
    const std::string& space_id,
    const std::string& page_id,
    const std::string& user_id,
    const std::string& required_level,
    const std::string& origin);
  static int perm_rank(const std::string& p);
};
