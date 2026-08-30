#pragma once

// Replica Fabric - persistence.
//
// Authoritative replica-set and replica state is persisted using a versioned,
// checksummed binary encoding. Recovery validates strictly and rejects:
//   * malformed lengths       * truncation          * checksum corruption
//   * duplicate IDs           * duplicate fields    * invalid enum values
//   * NaN/Inf                 * overflow            * trailing garbage
//   * incompatible versions   * impossible lifecycle transitions
//   * invalid generation relationships
//
// Recovery must NOT resurrect stale serving authority: on load, every replica's
// serving eligibility is cleared and its worker boot identity is forgotten, so
// live workers must re-establish their current boot identity (and the
// coordinator a fresh epoch) before any replica may serve again.

#include <replicafabric/core/identity_kinds.hpp>
#include <replicafabric/core/error.hpp>
#include <replicafabric/model/replica.hpp>
#include <replicafabric/model/replica_set.hpp>
#include <replicafabric/core/replica_set_controller.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace replicafabric {

// Decoded, validated state extracted from a checkpoint. Not yet installed.
struct RecoveredState {
  std::map<ReplicaSetId, ReplicaSetState> sets;
  std::map<ReplicaId, ReplicaState> replicas;
  std::vector<WorkerRegistration> workers;
  std::vector<PromotionRecord> promotions;
  std::vector<DrainRecord> drains;
  std::vector<FailoverRecord> failovers;
};

struct Snapshot {
  static constexpr std::uint32_t kFormatVersion = 1;
  static constexpr std::uint32_t kSchemaVersion = 1;
  static constexpr std::uint32_t kMagic = 0x52534653;  // "RFS" tag
};

// Serialize the controller's full authoritative state into a byte buffer.
std::vector<std::uint8_t> make_snapshot(const ReplicaSetController& ctrl);

// Strictly decode + validate a snapshot into a RecoveredState. On failure,
// returns nullopt and sets *error (if provided). Never returns a partially
// valid state.
std::optional<RecoveredState> decode_snapshot(const std::vector<std::uint8_t>& bytes,
                                              std::string* error = nullptr);

// Validate a RecoveredState before installation: duplicate IDs, impossible
// transitions, invalid generation relationships, and enum/NaN validity.
// Returns an error string on failure, or nullopt when valid.
std::optional<std::string> validate_recovered_state(const RecoveredState& state);

// Install a validated RecoveredState into a controller. This DOES NOT restore
// serving authority: serving_eligible is cleared and boot identities are
// forgotten for every replica.
void install_recovered_state(const RecoveredState& state, ReplicaSetController& ctrl);

// One-shot convenience wrappers.
std::vector<std::uint8_t> capture_and_clear_authority(const ReplicaSetController& ctrl);
bool recover_into(const std::vector<std::uint8_t>& bytes, ReplicaSetController& ctrl,
                  std::string* error = nullptr);

}  // namespace replicafabric
