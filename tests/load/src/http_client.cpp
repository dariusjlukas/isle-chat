#include "http_client.h"

#include <chrono>
#include <stdexcept>

HttpClient::HttpClient(const std::string& base_url, StatsCollector& stats)
    : base_url_(base_url), stats_(stats) {
  curl_ = curl_easy_init();
  if (!curl_) throw std::runtime_error("Failed to init CURL handle");

  // Enable connection reuse (keep-alive)
  curl_easy_setopt(curl_, CURLOPT_TCP_KEEPALIVE, 1L);
  curl_easy_setopt(curl_, CURLOPT_TCP_KEEPIDLE, 60L);
  curl_easy_setopt(curl_, CURLOPT_TCP_KEEPINTVL, 30L);

  // Follow redirects
  curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);

  // Timeouts
  curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 10L);

  // Disable signal-based timeout handling (important for multi-threaded)
  curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L);
}

HttpClient::~HttpClient() {
  if (curl_) curl_easy_cleanup(curl_);
}

void HttpClient::set_auth_token(const std::string& token) { auth_token_ = token; }

size_t HttpClient::write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* body = static_cast<std::string*>(userdata);
  body->append(ptr, size * nmemb);
  return size * nmemb;
}

Response HttpClient::get(const std::string& path, const Headers& headers,
                         const std::string& stat_name) {
  return perform("GET", path, "", "", headers, stat_name);
}

Response HttpClient::post_json(const std::string& path, const json& body, const Headers& headers,
                               const std::string& stat_name) {
  return perform("POST", path, body.dump(), "application/json", headers, stat_name);
}

Response HttpClient::post_raw(const std::string& path, const std::string& body,
                              const std::string& content_type, const Headers& headers,
                              const std::string& stat_name) {
  return perform("POST", path, body, content_type, headers, stat_name);
}

Response HttpClient::put_json(const std::string& path, const json& body, const Headers& headers,
                              const std::string& stat_name) {
  return perform("PUT", path, body.dump(), "application/json", headers, stat_name);
}

Response HttpClient::del(const std::string& path, const Headers& headers,
                         const std::string& stat_name) {
  return perform("DELETE", path, "", "", headers, stat_name);
}

Response HttpClient::perform(const std::string& method, const std::string& path,
                             const std::string& body, const std::string& content_type,
                             const Headers& headers, const std::string& stat_name) {
  std::string url = base_url_ + path;
  std::string response_body;

  curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response_body);

  // Set method
  if (method == "GET") {
    curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);
  } else if (method == "POST") {
    curl_easy_setopt(curl_, CURLOPT_POST, 1L);
  } else if (method == "PUT") {
    curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, "PUT");
  } else if (method == "DELETE") {
    curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, "DELETE");
  }

  // Set body
  if (!body.empty()) {
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, (long)body.size());
  } else if (method == "POST") {
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, "");
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, 0L);
  }

  // Build header list
  struct curl_slist* slist = nullptr;
  if (!content_type.empty()) {
    slist = curl_slist_append(slist, ("Content-Type: " + content_type).c_str());
  }
  if (!auth_token_.empty()) {
    slist = curl_slist_append(slist, ("Authorization: Bearer " + auth_token_).c_str());
  }
  for (auto& [k, v] : headers) {
    slist = curl_slist_append(slist, (k + ": " + v).c_str());
  }
  curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, slist);

  // Perform request with timing
  auto start = std::chrono::steady_clock::now();
  CURLcode res = curl_easy_perform(curl_);
  auto end = std::chrono::steady_clock::now();
  double latency_ms =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;

  Response resp;
  resp.latency_ms = latency_ms;
  resp.body = std::move(response_body);

  if (res == CURLE_OK) {
    long http_code = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);
    resp.status = static_cast<int>(http_code);
  }

  // Record stats
  std::string name = stat_name.empty() ? path : stat_name;
  // Strip query parameters from name if no explicit stat_name
  if (stat_name.empty()) {
    auto qpos = name.find('?');
    if (qpos != std::string::npos) name = name.substr(0, qpos);
  }
  stats_.record(method, name, latency_ms, resp.body.size(), res == CURLE_OK && resp.ok());

  // Cleanup
  if (slist) curl_slist_free_all(slist);

  // Reset custom request method for next call
  curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, nullptr);

  return resp;
}
