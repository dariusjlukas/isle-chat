#include "redis/redis_pubsub.h"

namespace enclave_redis {

RedisPubSub::RedisPubSub(
  const std::string& url, const std::string& instance_id, DispatchFn dispatch)
  : publisher_(url, instance_id), subscriber_(url, instance_id, std::move(dispatch)) {}

RedisPubSub::~RedisPubSub() = default;

void RedisPubSub::start() {
  subscriber_.start();
}

bool RedisPubSub::publish(const std::string& topic, const std::string& payload) {
  return publisher_.publish(topic, payload);
}

}  // namespace enclave_redis
