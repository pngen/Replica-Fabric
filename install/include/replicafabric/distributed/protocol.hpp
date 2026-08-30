#pragma once

// Replica Fabric - framed protocol messages.
//
// Every message carries a fixed binary authority header (message kind,
// sequence, coordinator epoch, worker identity + boot, replica/set identity,
// and attempt identity). The coordinator validates every inbound message
// against its current authority before acting, in strict order:
// CoordinatorEpoch -> WorkerBootId -> ReplicaSetGeneration -> ReplicaGeneration
// -> ArtifactGeneration -> HealthGeneration -> AttemptId. Stale messages are
// rejected and never mutate authoritative state.

#include <replicafabric/core/identity_kinds.hpp>
#include <replicafabric/core/time.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace replicafabric {

enum class MessageKind : std::uint8_t {
  HELLO = 1,
  REGISTER = 2,
  REGISTER_ACK = 3,
  CREATE_SET = 4,
  CREATE_SET_ACK = 5,
  PROVISION = 6,
  PROVISION_ACK = 7,
  WARM = 8,
  WARM_DONE = 9,
  HEALTH = 10,
  READINESS = 11,
  PROMOTE = 12,
  PROMOTE_ACK = 13,
  DRAIN = 14,
  DRAIN_ACK = 15,
  FAILOVER = 16,
  FAILOVER_ACK = 17,
  EXECUTE = 18,
  EXECUTE_RESULT = 19,
  SNAPSHOT = 20,
  MSG_ERROR = 21,
  PING = 22,
  PONG = 23,
  QUERY = 24,
};

struct Message {
  MessageKind kind = MessageKind::HELLO;
  std::uint64_t seq = 0;
  CoordinatorEpoch epoch;      // coordinator epoch the sender believes is current
  WorkerId worker;             // originating worker (null for control client)
  WorkerBootId boot;           // the worker's boot identity
  ReplicaId replica;           // target replica
  ReplicaSetId set;            // target replica set
  AttemptId attempt;           // work attempt identity (for completion messages)
  std::string payload;         // command/data payload (simple key=value lines)
};

// Strict binary encode/decode. decode returns false on malformed/truncated data.
std::vector<std::uint8_t> encode_message(const Message& m);
bool decode_message(const std::vector<std::uint8_t>& bytes, Message& out);

// Text payload helpers (single-line values).
void set_payload(std::string& s, const std::string& k, const std::string& v);
std::string get_payload(const std::string& s, const std::string& k, const std::string& def = "");

}  // namespace replicafabric