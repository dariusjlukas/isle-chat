#pragma once

#include "stats.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

using json = nlohmann::json;
using Headers = std::unordered_map<std::string, std::string>;

struct Response {
  int status = 0;
  std::string body;
  double latency_ms = 0;

  json json_body() const {
    auto j = json::parse(body, nullptr, false);
    return j.is_discarded() ? json::object() : j;
  }
  bool ok() const { return status >= 200 && status < 300; }
};

class HttpClient {
 public:
  HttpClient(const std::string& base_url, StatsCollector& stats);
  ~HttpClient();

  HttpClient(const HttpClient&) = delete;
  HttpClient& operator=(const HttpClient&) = delete;

  void set_auth_token(const std::string& token);
  const std::string& auth_token() const { return auth_token_; }

  Response get(const std::string& path, const Headers& headers = {},
               const std::string& stat_name = "");

  Response post_json(const std::string& path, const json& body, const Headers& headers = {},
                     const std::string& stat_name = "");

  Response post_raw(const std::string& path, const std::string& body,
                    const std::string& content_type, const Headers& headers = {},
                    const std::string& stat_name = "");

  Response put_json(const std::string& path, const json& body, const Headers& headers = {},
                    const std::string& stat_name = "");

  Response del(const std::string& path, const Headers& headers = {},
               const std::string& stat_name = "");

  const std::string& base_url() const { return base_url_; }

 private:
  Response perform(const std::string& method, const std::string& path, const std::string& body,
                   const std::string& content_type, const Headers& headers,
                   const std::string& stat_name);

  static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);

  std::string base_url_;
  std::string auth_token_;
  StatsCollector& stats_;
  CURL* curl_;
};
