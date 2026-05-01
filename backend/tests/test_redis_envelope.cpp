#include <gtest/gtest.h>

#include "redis/redis_envelope.h"

using enclave_redis::Envelope;
using enclave_redis::is_self_echo;
using enclave_redis::topic_kind_from;

TEST(RedisEnvelope, EncodeDecodeRoundtrip) {
  Envelope src;
  src.instance_id = "11111111-2222-4333-8444-555555555555";
  src.topic = "channel:abc";
  src.payload = R"({"type":"new_message","id":42})";

  std::string wire = src.encode();
  auto decoded = Envelope::decode(wire);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->instance_id, src.instance_id);
  EXPECT_EQ(decoded->topic, src.topic);
  EXPECT_EQ(decoded->payload, src.payload);
}

TEST(RedisEnvelope, DecodeFailsOnMalformedJson) {
  EXPECT_FALSE(Envelope::decode("{not json").has_value());
  EXPECT_FALSE(Envelope::decode("").has_value());
  EXPECT_FALSE(Envelope::decode("[]").has_value());
}

TEST(RedisEnvelope, DecodeFailsOnMissingFields) {
  EXPECT_FALSE(Envelope::decode(R"({"topic":"t","payload":"p"})").has_value());
  EXPECT_FALSE(Envelope::decode(R"({"instance_id":"i","payload":"p"})").has_value());
  EXPECT_FALSE(Envelope::decode(R"({"instance_id":"i","topic":"t"})").has_value());
}

TEST(RedisEnvelope, DecodeFailsOnWrongFieldTypes) {
  // payload must be a string (the wire format requires the inner message
  // be JSON-encoded; the publisher stringifies before wrapping).
  EXPECT_FALSE(
    Envelope::decode(R"({"instance_id":"i","topic":"t","payload":42})").has_value());
  EXPECT_FALSE(
    Envelope::decode(R"({"instance_id":1,"topic":"t","payload":"p"})").has_value());
}

TEST(RedisEnvelope, EncodeProducesParseableJson) {
  Envelope src;
  src.instance_id = "id";
  src.topic = "channel:x";
  // Payload contains characters that require JSON escaping (quote, newline).
  src.payload = "{\"k\":\"v\\nlines\"}";
  std::string wire = src.encode();
  auto decoded = Envelope::decode(wire);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->payload, src.payload);
}

TEST(RedisEnvelope, TopicKindFrom) {
  EXPECT_EQ(topic_kind_from("channel:abc"), "channel");
  EXPECT_EQ(topic_kind_from("space:42:foo"), "space");
  EXPECT_EQ(topic_kind_from("nocolon"), "nocolon");
  EXPECT_EQ(topic_kind_from(""), "");
}

TEST(RedisEnvelope, IsSelfEcho) {
  Envelope env;
  env.instance_id = "alpha";
  EXPECT_TRUE(is_self_echo(env, "alpha"));
  EXPECT_FALSE(is_self_echo(env, "beta"));
  EXPECT_FALSE(is_self_echo(env, ""));
}
