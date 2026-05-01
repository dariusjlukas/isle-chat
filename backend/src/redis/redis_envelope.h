#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace enclave_redis {

// Wire envelope: instance_id identifies the publisher (used for self-echo
// filtering); topic is the local-fan-out target ("channel:abc", etc.);
// payload is the original message body (already JSON-encoded; opaque to us).
struct Envelope {
  std::string instance_id;
  std::string topic;
  std::string payload;

  // Encode to a JSON string suitable for PUBLISH.
  std::string encode() const;

  // Decode from PUBLISH payload. Returns std::nullopt on parse failure
  // (missing fields, bad types, malformed JSON).
  static std::optional<Envelope> decode(std::string_view raw);
};

// Returns the prefix of `topic` up to (but not including) the first ':'.
// If there's no ':' the whole topic is returned. Used as a Prometheus label
// to bound metric cardinality (e.g. "channel" instead of "channel:abc").
inline std::string_view topic_kind_from(std::string_view topic) {
  auto colon = topic.find(':');
  if (colon == std::string_view::npos) return topic;
  return topic.substr(0, colon);
}

// Self-echo filter helper, factored out so it's directly unit-testable
// without a live Redis. Returns true if the envelope was published by this
// instance and should be dropped.
inline bool is_self_echo(const Envelope& env, std::string_view our_instance_id) {
  return env.instance_id == our_instance_id;
}

// The single fan-in topic both publishers and subscribers use.
inline constexpr const char* kBroadcastChannel = "enclave:broadcast";

}  // namespace enclave_redis
