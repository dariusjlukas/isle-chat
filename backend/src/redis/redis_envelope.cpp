#include "redis/redis_envelope.h"

#include <nlohmann/json.hpp>

namespace enclave_redis {

std::string Envelope::encode() const {
  nlohmann::json j = {
    {"instance_id", instance_id},
    {"topic", topic},
    {"payload", payload},
  };
  return j.dump();
}

std::optional<Envelope> Envelope::decode(std::string_view raw) {
  try {
    auto j = nlohmann::json::parse(raw);
    if (!j.is_object()) return std::nullopt;
    if (!j.contains("instance_id") || !j["instance_id"].is_string()) return std::nullopt;
    if (!j.contains("topic") || !j["topic"].is_string()) return std::nullopt;
    if (!j.contains("payload") || !j["payload"].is_string()) return std::nullopt;
    Envelope env;
    env.instance_id = j["instance_id"].get<std::string>();
    env.topic = j["topic"].get<std::string>();
    env.payload = j["payload"].get<std::string>();
    return env;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

}  // namespace enclave_redis
