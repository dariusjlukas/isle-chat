#include <gtest/gtest.h>

#include "redis/redis_envelope.h"
#include "redis/redis_publisher.h"
#include "redis/redis_pubsub.h"
#include "redis/redis_subscriber.h"

using enclave_redis::Envelope;
using enclave_redis::is_self_echo;
using enclave_redis::RedisPublisher;
using enclave_redis::RedisPubSub;
using enclave_redis::RedisSubscriber;

// --- Publisher: empty URL -> no-op ----------------------------------------

TEST(RedisPublisher, EmptyUrlDisablesPublisher) {
  RedisPublisher p("", "inst-1");
  EXPECT_FALSE(p.is_enabled());
  // publish() must not crash and must return false.
  EXPECT_FALSE(p.publish("channel:abc", "hello"));
}

// --- Subscriber: empty URL -> no-op ---------------------------------------

TEST(RedisSubscriber, EmptyUrlDisablesSubscriber) {
  bool called = false;
  RedisSubscriber s(
    "",
    "inst-1",
    [&](std::string, std::shared_ptr<std::string>) { called = true; });
  EXPECT_FALSE(s.is_enabled());
  // start() and stop() must be safe even when disabled (no thread spawned).
  s.start();
  s.stop();
  EXPECT_FALSE(called);
}

// --- PubSub orchestrator: empty URL ---------------------------------------

TEST(RedisPubSub, EmptyUrlDisablesAll) {
  RedisPubSub ps(
    "",
    "inst-1",
    [](std::string, std::shared_ptr<std::string>) {});
  EXPECT_FALSE(ps.is_enabled());
  EXPECT_FALSE(ps.publish("topic", "payload"));
  // start() must be a no-op (no background thread spun up).
  ps.start();
}

// --- Self-echo filter -----------------------------------------------------
// Covers the inline helper used by the subscriber loop. A live Redis would
// be needed to exercise the real receive path; the helper itself is the
// behaviorally important piece and is directly testable.

TEST(RedisSelfEchoFilter, MatchingInstanceIdIsSelf) {
  Envelope env;
  env.instance_id = "instance-A";
  env.topic = "channel:abc";
  env.payload = "{}";
  EXPECT_TRUE(is_self_echo(env, "instance-A"));
}

TEST(RedisSelfEchoFilter, DifferentInstanceIdIsNotSelf) {
  Envelope env;
  env.instance_id = "instance-A";
  EXPECT_FALSE(is_self_echo(env, "instance-B"));
}

TEST(RedisSelfEchoFilter, EmptyOurIdNeverMatchesNonemptyEnvelope) {
  Envelope env;
  env.instance_id = "instance-A";
  EXPECT_FALSE(is_self_echo(env, ""));
}

// --- Malformed URL: publisher reports failure but does not crash ----------
// We exercise the path with a syntactically-invalid URL that bypasses the
// "empty == disabled" early return; ensure_connected_locked() should return
// false and the metric increments without crashing.

TEST(RedisPublisher, MalformedUrlReturnsFalseWithoutCrash) {
  RedisPublisher p("not-a-url", "inst-1");
  EXPECT_TRUE(p.is_enabled());  // Non-empty url -> "enabled" by construction.
  // First publish triggers parse, which fails. Should return false.
  EXPECT_FALSE(p.publish("channel:abc", "hello"));
}
