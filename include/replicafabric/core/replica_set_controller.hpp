#pragma once

// Replica Fabric - authoritative replica-set controller.
//
// This is the single source of truth for authoritative replica-set and replica
// state: lifecycle transitions, health/readiness/warming evidence, promotion,
// draining, failover, persistent authority metadata, and stale-message
// rejection. The coordinator process owns one controller; workers send
// evidence/registration over the transport and never decide authority.
//
// Concurrency contract: the controller serializes all state mutation behind a
// single mutex and never performs blocking network, persistence, backend, or
// CUDA work while holding it. Operations are pure state changes; the caller is
// responsible for I/O outside the call (the distributed layer and the CLI do
// exactly this).

#include <replicafabric/core/enums.hpp>
#include <replicafabric/core/identity_kinds.hpp>
#include <replicafabric/core/capability.hpp>
#include <replicafabric/core/policies.hpp>
#include <replicafabric/model/replica.hpp>
#include <replicafabric/model/replica_set.hpp>
#include <replicafabric/authority/serving_authority.hpp>

#include <cstdint>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <vector>

namespace replicafabric {

// Outcome of a controller operation. REJECTED means stale authority/policy
// prevented the change (the message must be discarded, not applied). INVALID
// is a programming/state-machine violation. OK means the change was applied.
enum class OpStatus { OK, REJECTED, INVALID };

struct OpResult {
  OpStatus status = OpStatus::INVALID;
  std::string message;

  static OpResult ok(std::string msg) { return OpResult{OpStatus::OK, std::move(msg)}; }
  static OpResult rejected(std::string msg) { return OpResult{OpStatus::REJECTED, std::move(msg)}; }
  static OpResult invalid(std::string msg) { return OpResult{OpStatus::INVALID, std::move(msg)}; }
  bool ok() const noexcept { return status == OpStatus::OK; }
  bool rejected() const noexcept { return status == OpStatus::REJECTED; }
};

// A worker's registration as known by the coordinator.
struct WorkerRegistration {
  WorkerId worker_id;
  WorkerBootId boot_id;
  NodeId node_id;
  ResourceInventory inventory;
  ReplicaSetGeneration hosted_set_generation;  // (informational) set generation the worker believes
  std::uint32_t protocol_version = 0;

  friend bool operator==(const WorkerRegistration& a, const WorkerRegistration& b) noexcept {
    return a.worker_id == b.worker_id && a.boot_id == b.boot_id && a.node_id == b.node_id &&
           a.inventory == b.inventory && a.hosted_set_generation == b.hosted_set_generation &&
           a.protocol_version == b.protocol_version;
  }
};

// Stable, auditable records.
struct PromotionRecord {
  PromotionId id;
  ReplicaId replica_id;
  ReplicaSetId set_id;
  ReplicaGeneration replica_generation;
  ReplicaSetGeneration set_generation;
  CoordinatorEpoch epoch;
  PromotionState from = PromotionState::NOT_PROMOTED;
  PromotionState to = PromotionState::PRIMARY;
  MonotonicNs when_mono = 0;
  WallNs when_wall = 0;
  std::string reason;

  friend bool operator==(const PromotionRecord& a, const PromotionRecord& b) noexcept {
    return a.id == b.id && a.replica_id == b.replica_id && a.set_id == b.set_id &&
           a.replica_generation == b.replica_generation &&
           a.set_generation == b.set_generation && a.epoch == b.epoch && a.from == b.from &&
           a.to == b.to && a.when_mono == b.when_mono && a.when_wall == b.when_wall &&
           a.reason == b.reason;
  }
};

struct DrainRecord {
  DrainId id;
  ReplicaId replica_id;
  ReplicaSetId set_id;
  DrainState state = DrainState::NOT_DRAINING;
  MonotonicNs started_mono = 0;
  MonotonicNs completed_mono = 0;
  bool force_cancelled = false;
  std::uint64_t quiesced_requests = 0;
  std::string reason;

  friend bool operator==(const DrainRecord& a, const DrainRecord& b) noexcept {
    return a.id == b.id && a.replica_id == b.replica_id && a.set_id == b.set_id &&
           a.state == b.state && a.started_mono == b.started_mono &&
           a.completed_mono == b.completed_mono && a.force_cancelled == b.force_cancelled &&
           a.quiesced_requests == b.quiesced_requests && a.reason == b.reason;
  }
};

struct FailoverRecord {
  FailoverId id;
  ReplicaSetId set_id;
  ReplicaId failed_replica;
  ReplicaId replacement;
  FailoverState state = FailoverState::TRIGGERED;
  CoordinatorEpoch epoch;
  MonotonicNs started_mono = 0;
  MonotonicNs completed_mono = 0;
  bool generation_preserved = true;
  std::vector<WorkOutcome> ambiguous_outcomes;
  std::string reason;

  friend bool operator==(const FailoverRecord& a, const FailoverRecord& b) noexcept {
    return a.id == b.id && a.set_id == b.set_id && a.failed_replica == b.failed_replica &&
           a.replacement == b.replacement && a.state == b.state && a.epoch == b.epoch &&
           a.started_mono == b.started_mono && a.completed_mono == b.completed_mono &&
           a.generation_preserved == b.generation_preserved &&
           a.ambiguous_outcomes == b.ambiguous_outcomes && a.reason == b.reason;
  }
};

// Placeholder for placement candidate view (defined in placement module).
struct PlacementCandidateSummary {
  std::string worker_id;
  std::string device_id;
  std::string host;
  std::string numa;
  std::string failure_domain;
};

class ReplicaSetController {
public:
  explicit ReplicaSetController(std::uint64_t seed = 0x9e3779b97f4a7c15ULL);

  // --- workers ------------------------------------------------------------
  // Register a worker (or re-register with a fresh boot id after restart).
  OpResult register_worker(const WorkerRegistration& wr, MonotonicNs mono, WallNs wall);
  OpResult unregister_worker(WorkerId worker_id);
  const WorkerRegistration* find_worker(WorkerId worker_id) const;

  // --- replica sets ---------------------------------------------------------
  OpResult create_replica_set(const ReplicaSetState& set, CoordinatorEpoch epoch);
  OpResult transition_replica_set(ReplicaSetId set_id, ReplicaSetLifecycle to);
  const ReplicaSetState* find_set(ReplicaSetId set_id) const;

  // --- replicas -------------------------------------------------------------
  // Declare/provision a new replica incarnation on a worker. A fresh
  // ReplicaGeneration and PlacementId are minted here.
  OpResult provision_replica(ReplicaId replica_id, ReplicaSetId set_id, WorkerId worker_id,
                             const ReplicaCompatibility& compat, const DeviceCapability& device,
                             std::uint64_t capacity, MonotonicNs mono, WallNs wall);
  OpResult set_allocating(ReplicaId replica_id, PlacementId placement_id, MonotonicNs mono);
  OpResult set_starting(ReplicaId replica_id, MonotonicNs mono, WallNs wall);
  OpResult warm_replica(ReplicaId replica_id, const WarmingRecord& warm, const WorkerId& worker,
                        WorkerBootId boot, MonotonicNs mono);
  OpResult report_health(ReplicaId replica_id, const HealthEvidence& ev, const WorkerId& worker,
                         WorkerBootId boot, MonotonicNs mono);
  OpResult report_readiness(ReplicaId replica_id, const ReadinessRecord& rr, const WorkerId& worker,
                            WorkerBootId boot, MonotonicNs mono);
  OpResult promote_replica(ReplicaId replica_id, PromotionState target, const WorkerId& worker,
                           WorkerBootId boot, MonotonicNs mono, WallNs wall);
  OpResult drain_replica(ReplicaId replica_id, const WorkerId& worker, WorkerBootId boot,
                         MonotonicNs mono);
  OpResult complete_drain(ReplicaId replica_id, std::uint64_t quiesced,
                          bool force_cancelled, MonotonicNs mono);
  OpResult fail_replica(ReplicaId replica_id, const WorkerId& worker, WorkerBootId boot,
                        const std::string& reason, MonotonicNs mono);
  OpResult retire_replica(ReplicaId replica_id, MonotonicNs mono);
  OpResult trigger_failover(ReplicaSetId set_id, ReplicaId failed_replica, const std::string& reason,
                            MonotonicNs mono, WallNs wall);

  // --- queries -------------------------------------------------------------
  const ReplicaState* find_replica(ReplicaId replica_id) const;
  std::vector<ReplicaId> list_replicas(ReplicaSetId set_id) const;
  std::vector<ReplicaSetId> list_sets() const;
  ServingAuthorityDecision servable(ReplicaId replica_id) const;
  const std::vector<PromotionRecord>& promotions() const { return promotions_; }
  const std::vector<DrainRecord>& drains() const { return drains_; }
  const std::vector<FailoverRecord>& failovers() const { return failovers_; }
  CoordinatorEpoch epoch() const { return epoch_; }
  bool empty() const;

  // --- generation/identity helpers (used by persistence & tests) -----------
  template <typename T>
  T random_id() {
    T t = T::random(rng_);
    return t;
  }

  // For persistence: access the mutable working state (single-threaded use).
  std::map<ReplicaSetId, ReplicaSetState>& sets() { return sets_; }
  std::map<ReplicaId, ReplicaState>& replicas() { return replicas_; }

  // Resolve the current worker boot for a replica's worker (for gate context).
  WorkerBootId current_boot_for(const ReplicaId& replica_id) const;
  void set_epoch(CoordinatorEpoch epoch) { epoch_ = epoch; }

private:
  ReplicaGeneration fresh_generation();
  ReplicaSetGeneration fresh_set_generation();
  PlacementId fresh_placement();

  mutable std::mutex mu_;
  std::map<ReplicaSetId, ReplicaSetState> sets_;
  std::map<ReplicaId, ReplicaState> replicas_;
  std::map<WorkerId, WorkerRegistration> workers_;
  std::vector<PromotionRecord> promotions_;
  std::vector<DrainRecord> drains_;
  std::vector<FailoverRecord> failovers_;
  CoordinatorEpoch epoch_;
  std::mt19937_64 rng_;
};

}  // namespace replicafabric
