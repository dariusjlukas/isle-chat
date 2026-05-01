#pragma once
#include <App.h>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include "logging/logger.h"
#include "metrics/metrics.h"

namespace handler_utils {

// Bundles a request-id (for logs), a metrics RequestTimer (for /metrics),
// and the basic request context. One instance is created at the top of
// each route handler and captured via std::shared_ptr into any async
// continuations. Call observe(status) before the final response write.
struct RequestScope {
  logging::RequestCtx ctx;
  metrics::RequestTimer timer;
  std::string method;
  std::string route;

  RequestScope(std::string method_in, std::string route_in)
    : ctx{logging::make_request_id(), "", method_in, route_in},
      timer(method_in, route_in),
      method(std::move(method_in)),
      route(std::move(route_in)) {}

  void observe(int status) {
    timer.observe(status);
  }
  void set_user(const std::string& user_id) {
    ctx.user_id = user_id;
  }
};

// Helper to set X-Request-Id header on a response. Must be called before
// any res->end(...) / res->writeStatus() that finalizes the response.
template <bool SSL>
inline void set_request_id_header(uWS::HttpResponse<SSL>* res, const RequestScope& scope) {
  res->writeHeader("X-Request-Id", scope.ctx.request_id);
}

}  // namespace handler_utils
