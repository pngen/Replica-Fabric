#include <replicafabric/core/replica_set_controller.hpp>

#include <replicafabric/authority/transition.hpp>
#include <replicafabric/core/compatibility.hpp>

#include <algorithm>
#include <cstdint>
#include <set>
#include <utility>

namespace replicafabric {

ReplicaSetController::ReplicaSetController(std::uint64_t seed) : rng_(seed) {}

bool ReplicaSetController::empty() const {
  std::lock_guard<std::mutex> lk(mu_);
  return sets_.empty() && replicas_.empty();
}

ReplicaGeneration ReplicaSetController::fresh_generation() { return random_id<ReplicaGeneration>(); }
ReplicaSetGeneration ReplicaSetController::fresh_set_generation() {
  return random_id<ReplicaSetGeneration>();
}
PlacementId ReplicaSetController::fresh_placement() { return random_id<PlacementId>(); }

// ---------------------------------------------------------------------------
// Workers
// ---------------------------------------------------------------------------
OpResult ReplicaSetController::register_worker(const WorkerRegistration& wr, MonotonicNs,
                                               WallNs) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = workers_.find(wr.worker_id);
  const bool restart = (it != workers_.end() && it->second.boot_id != wr.boot_id);
  if (restart) {
    // The worker restarted with a fresh boot id. Any replica it previously
    // hosted that held serving authority is now stale and must be failed.
    for (auto& [rid, r] : replicas_) {
      if (r.worker_id == wr.worker_id && r.lifecycle == ReplicaLifecycle::SERVING) {
        r.serving_eligible = false;
        r.health = HealthState::FAILED;
        r.health_record.state = HealthState::FAILED;
        r.health_record.reason = "worker restarted under a new boot identity";
        r.lifecycle = ReplicaLifecycle::FAILED;
        r.promotion = PromotionState::NOT_PROMOTED;
        r.role = ReplicaRole::NONE;
        r.active_requests = 0;
        (void)rid;
      }
    }
  }
  workers_[wr.worker_id] = wr;
  return OpResult::ok("worker registered (boot " + wr.boot_id.str() + ")");
}

OpResult ReplicaSetController::unregister_worker(WorkerId worker_id) {
  std::lock_guard<std::mutex> lk(mu_);
  if (workers_.find(worker_id) == workers_.end()) {
    return OpResult::rejected("unknown worker");
  }
  for (auto& [rid, r] : replicas_) {
    if (r.worker_id == worker_id) {
      const bool had_authority =
          (r.serving_eligible || r.lifecycle == ReplicaLifecycle::SERVING ||
           r.lifecycle == ReplicaLifecycle::READY || r.lifecycle == ReplicaLifecycle::DRAINING);
      if (had_authority) {
        r.serving_eligible = false;
        r.health = HealthState::FAILED;
        r.health_record.state = HealthState::FAILED;
        r.health_record.reason = "worker disconnected";
        r.lifecycle = ReplicaLifecycle::FAILED;
        r.promotion = PromotionState::NOT_PROMOTED;
        r.role = ReplicaRole::NONE;
      }
      (void)rid;
    }
  }
  workers_.erase(worker_id);
  return OpResult::ok("worker unregistered");
}

const WorkerRegistration* ReplicaSetController::find_worker(WorkerId worker_id) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = workers_.find(worker_id);
  return it == workers_.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------------------
// Replica sets
// ---------------------------------------------------------------------------
OpResult ReplicaSetController::create_replica_set(const ReplicaSetState& set, CoordinatorEpoch epoch) {
  std::lock_guard<std::mutex> lk(mu_);
  if (sets_.find(set.id) != sets_.end()) {
    return OpResult::rejected("replica set already exists");
  }
  if (set.max_replicas == 0 || set.desired_count == 0 || set.min_healthy == 0 ||
      set.min_healthy > set.max_replicas || set.desired_count > set.max_replicas) {
    return OpResult::invalid(
        "replica set counts invalid (desired/min_healthy/max must satisfy 0 < min <= desired <= max)");
  }
  sets_[set.id] = set;
  epoch_ = epoch;
  return OpResult::ok("replica set created");
}

OpResult ReplicaSetController::transition_replica_set(ReplicaSetId set_id, ReplicaSetLifecycle to) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = sets_.find(set_id);
  if (it == sets_.end()) return OpResult::rejected("unknown replica set");
  auto& s = it->second;
  if (!can_transition(s.lifecycle, to)) {
    return OpResult::invalid("replica-set lifecycle transition " +
                             std::string(to_string(s.lifecycle)) + " -> " +
                             std::string(to_string(to)) + " is not allowed");
  }
  s.lifecycle = to;
  return OpResult::ok("replica-set lifecycle -> " + std::string(to_string(to)));
}

const ReplicaSetState* ReplicaSetController::find_set(ReplicaSetId set_id) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = sets_.find(set_id);
  return it == sets_.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------------------
// Authority validation helpers
// ---------------------------------------------------------------------------
namespace {
bool worker_boot_matches(const ReplicaState& r, const WorkerId& worker, WorkerBootId boot) {
  return r.worker_id == worker && r.boot_id == boot;
}
}  // namespace

// ---------------------------------------------------------------------------
// Replicas
// ---------------------------------------------------------------------------
OpResult ReplicaSetController::provision_replica(ReplicaId replica_id, ReplicaSetId set_id,
                                                 WorkerId worker_id, const ReplicaCompatibility& compat,
                                                 const DeviceCapability& device, std::uint64_t capacity,
                                                 MonotonicNs mono, WallNs wall) {
  std::lock_guard<std::mutex> lk(mu_);
  auto sit = sets_.find(set_id);
  if (sit == sets_.end()) return OpResult::rejected("unknown replica set");
  auto& set = sit->second;
  if (set.lifecycle == ReplicaSetLifecycle::RETIRED || set.lifecycle == ReplicaSetLifecycle::FAILED) {
    return OpResult::rejected("replica set is not provisionable");
  }
  auto wit = workers_.find(worker_id);
  if (wit == workers_.end()) return OpResult::rejected("unknown worker");
  const auto& reg = wit->second;

  std::uint32_t count = 0;
  for (const auto& [rid, r] : replicas_) {
    if (r.set_id == set_id) ++count;
    (void)rid;
  }
  if (count >= set.max_replicas) {
    return OpResult::rejected("replica set max_replicas reached");
  }

  if (set.lifecycle == ReplicaSetLifecycle::CREATED) {
    set.lifecycle = ReplicaSetLifecycle::PROVISIONING;
  }

  ReplicaState r;
  r.id = replica_id;
  r.set_id = set_id;
  r.generation = fresh_generation();
  r.node_id = reg.node_id;
  r.worker_id = worker_id;
  r.boot_id = reg.boot_id;
  r.artifact_id = set.artifact_id;
  r.artifact_generation = set.artifact_generation;
  r.backend = compat.backend;
  r.backend_identity = compat.runtime_name;
  r.device_identity = device.device_id;
  r.accelerator = device.kind;
  r.compute_capability = device.compute;
  r.memory_residency = MemoryResidencyState::NOT_RESIDENT;
  r.warmth = WarmthState::COLD;
  r.readiness = ReadinessState::UNKNOWN;
  r.health = HealthState::UNKNOWN;
  r.lifecycle = ReplicaLifecycle::DECLARED;
  r.placement = PlacementState::PENDING;
  r.promotion = PromotionState::NOT_PROMOTED;
  r.role = ReplicaRole::NONE;
  r.serving_eligible = false;
  r.active_requests = 0;
  r.reserved_capacity = 0;
  r.capacity_total = capacity;
  r.start_time_mono = mono;
  r.start_time_wall = wall;
  r.coordinator_epoch = epoch_;
  r.set_generation = set.generation;
  r.health_generation = random_id<HealthGeneration>();
  r.policy_generation = set.policy_generation;
  r.policy_fingerprint = set.compatibility.policy_fingerprint;
  r.compatibility = compat;

  replicas_[replica_id] = std::move(r);
  return OpResult::ok("replica provisioned");
}

OpResult ReplicaSetController::set_allocating(ReplicaId replica_id, PlacementId placement_id,
                                              MonotonicNs) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = replicas_.find(replica_id);
  if (it == replicas_.end()) return OpResult::rejected("unknown replica");
  auto& r = it->second;
  if (!can_transition(r.lifecycle, ReplicaLifecycle::ALLOCATING)) {
    return OpResult::invalid("replica cannot move to ALLOCATING");
  }
  const bool placed = !placement_id.is_null();
  r.lifecycle = ReplicaLifecycle::ALLOCATING;
  r.placement_id = placement_id;
  r.placement = placed ? PlacementState::ASSIGNED : PlacementState::PENDING;
  r.memory_residency = MemoryResidencyState::NOT_RESIDENT;
  return OpResult::ok("replica -> ALLOCATING");
}

OpResult ReplicaSetController::set_starting(ReplicaId replica_id, MonotonicNs mono, WallNs wall) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = replicas_.find(replica_id);
  if (it == replicas_.end()) return OpResult::rejected("unknown replica");
  auto& r = it->second;
  if (!can_transition(r.lifecycle, ReplicaLifecycle::STARTING)) {
    return OpResult::invalid("replica cannot move to STARTING");
  }
  r.lifecycle = ReplicaLifecycle::STARTING;
  r.health = HealthState::STARTING;
  r.health_record.state = HealthState::STARTING;
  r.health_record.updated_at_mono = mono;
  r.health_record.updated_at_wall = wall;
  return OpResult::ok("replica -> STARTING");
}

OpResult ReplicaSetController::warm_replica(ReplicaId replica_id, const WarmingRecord& warm,
                                            const WorkerId& worker, WorkerBootId boot, MonotonicNs mono) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = replicas_.find(replica_id);
  if (it == replicas_.end()) return OpResult::rejected("unknown replica");
  auto& r = it->second;
  if (!worker_boot_matches(r, worker, boot)) {
    return OpResult::rejected("stale worker boot identity");
  }
  if (!can_transition(r.lifecycle, ReplicaLifecycle::WARMING) &&
      r.lifecycle != ReplicaLifecycle::WARMING) {
    return OpResult::invalid("replica is not in a state that accepts warming");
  }
  r.lifecycle = ReplicaLifecycle::WARMING;
  r.warming_record = warm;
  if (!warm.warmup_execution_done && warm.steps_required > 0 && warm.steps_completed > 0) {
    r.warmth = WarmthState::WARMING;
  } else {
    // Full warm set under policy => WARM.
    r.warmth = WarmthState::WARM;
  }
  if (!r.warming_record.started_warming_mono) r.warming_record.started_warming_mono = mono;
  return OpResult::ok("replica warming progressed");
}
OpResult ReplicaSetController::report_health(ReplicaId replica_id, const HealthEvidence& ev,
                                             const WorkerId& worker, WorkerBootId boot, MonotonicNs mono) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = replicas_.find(replica_id);
  if (it == replicas_.end()) return OpResult::rejected("unknown replica");
  auto& r = it->second;
  if (!worker_boot_matches(r, worker, boot)) {
    return OpResult::rejected("stale worker boot identity");
  }
  if (r.coordinator_epoch != epoch_) {
    return OpResult::rejected("stale coordinator epoch");
  }
  // The evidence must belong to the current health generation.
  if (ev.generation != r.health_generation) {
    return OpResult::rejected("stale health generation");
  }
  // A terminated replica must not accept health evidence that could restore it.
  if (r.lifecycle == ReplicaLifecycle::FAILED || r.lifecycle == ReplicaLifecycle::RETIRED ||
      r.lifecycle == ReplicaLifecycle::QUIESCED) {
    return OpResult::rejected("replica is terminated; health evidence rejected");
  }
  r.health = ev.state;
  r.health_record.state = ev.state;
  r.health_record.kind = ev.kind;
  r.health_record.source = ev.source;
  r.health_record.updated_at_mono = mono;
  r.health_record.updated_at_wall = ev.observed_at_wall;
  r.health_record.confidence = ev.confidence;
  r.health_record.generation = ev.generation;
  r.health_record.reason = ev.message;
  if (ev.state == HealthState::FAILED || ev.state == HealthState::UNHEALTHY) {
    ++r.health_record.consecutive_failures;
  } else {
    r.health_record.consecutive_failures = 0;
  }
  // Serving eligibility only ever survives under a fresh authority round.
  // If the replica became unhealthy/failed while serving, strip authority.
  if (r.serving_eligible &&
      (ev.state == HealthState::FAILED || ev.state == HealthState::UNHEALTHY)) {
    r.serving_eligible = false;
    r.role = ReplicaRole::SECONDARY;
    r.promotion = PromotionState::STANDBY;
  }
  return OpResult::ok("health updated to " + std::string(to_string(ev.state)));
}

OpResult ReplicaSetController::report_readiness(ReplicaId replica_id, const ReadinessRecord& rr,
                                                const WorkerId& worker, WorkerBootId boot,
                                                MonotonicNs) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = replicas_.find(replica_id);
  if (it == replicas_.end()) return OpResult::rejected("unknown replica");
  auto& r = it->second;
  if (!worker_boot_matches(r, worker, boot)) {
    return OpResult::rejected("stale worker boot identity");
  }
  if (r.coordinator_epoch != epoch_) {
    return OpResult::rejected("stale coordinator epoch");
  }
  if (r.lifecycle == ReplicaLifecycle::FAILED || r.lifecycle == ReplicaLifecycle::RETIRED ||
      r.lifecycle == ReplicaLifecycle::QUIESCED) {
    return OpResult::rejected("replica is terminated; readiness report rejected");
  }
  r.readiness_record = rr;
  r.readiness = rr.state;
  // If all readiness factors are satisfied and we are still warming, the
  // replica becomes READY (a guarded transition).
  if (rr.factors.all_set()) {
    const bool can_ready = (r.lifecycle == ReplicaLifecycle::WARMING ||
                            r.lifecycle == ReplicaLifecycle::STARTING);
    if (can_ready && can_transition(r.lifecycle, ReplicaLifecycle::READY)) {
      r.lifecycle = ReplicaLifecycle::READY;
    }
    r.readiness = ReadinessState::READY;
  } else {
    r.readiness = ReadinessState::NOT_READY;
  }
  return OpResult::ok("readiness updated to " + std::string(to_string(r.readiness)));
}

namespace {
bool ready_to_promote(const ReplicaState& r) {
  return r.lifecycle == ReplicaLifecycle::READY || r.lifecycle == ReplicaLifecycle::SERVING;
}
}  // namespace

OpResult ReplicaSetController::promote_replica(ReplicaId replica_id, PromotionState target,
                                               const WorkerId& worker, WorkerBootId boot,
                                               MonotonicNs mono, WallNs wall) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = replicas_.find(replica_id);
  if (it == replicas_.end()) return OpResult::rejected("unknown replica");
  auto& r = it->second;
  if (!worker_boot_matches(r, worker, boot)) {
    return OpResult::rejected("stale worker boot identity");
  }
  auto sit = sets_.find(r.set_id);
  if (sit == sets_.end()) return OpResult::rejected("unknown replica set");
  auto& set = sit->second;

  if (r.coordinator_epoch != epoch_) return OpResult::rejected("stale coordinator epoch");
  if (r.set_generation != set.generation) return OpResult::rejected("stale replica-set generation");
  if (r.policy_generation != set.policy_generation) {
    return OpResult::rejected("stale policy generation");
  }
  if (r.artifact_generation != set.artifact_generation) {
    return OpResult::rejected("stale artifact generation");
  }

  const PromotionPolicy& pol = set.policies.promotion;
  if (pol.require_ready && !ready_to_promote(r)) {
    return OpResult::rejected("replica is not in a ready state");
  }
  if (r.readiness != ReadinessState::READY) {
    return OpResult::rejected("replica is not ready");
  }
  if (pol.require_healthy && (r.health == HealthState::UNKNOWN || r.health == HealthState::STARTING)) {
    return OpResult::rejected("replica has no validated health");
  }
  if (pol.require_healthy && r.health == HealthState::UNHEALTHY) {
    return OpResult::rejected("replica is unhealthy");
  }
  if (pol.require_compatible) {
    const auto cr = check_compatibility(set.compatibility, r.compatibility);
    if (!cr.compatible) {
      std::string why;
      for (const auto& iss : cr.issues) {
        why += std::string(compatibility_issue_str(iss)) + " ";
      }
      return OpResult::rejected("replica incompatible: " + why);
    }
  }
  if (pol.require_placement_valid && r.placement != PlacementState::ASSIGNED) {
    return OpResult::rejected("replica placement is not assigned");
  }
  if (pol.require_resources && r.capacity_total == 0) {
    return OpResult::rejected("replica has no reserved capacity");
  }
  // Authority currency (epoch, set generation, policy generation, artifact
  // generation) was already validated above; require_authority_current is
  // honored by that gate.
  (void)pol.require_authority_current;

  const PromotionState from = r.promotion;
  if (target == PromotionState::PRIMARY) {
    if (!ready_to_promote(r)) return OpResult::rejected("cannot promote a non-ready replica to PRIMARY");
    if (r.lifecycle != ReplicaLifecycle::SERVING) {
      if (!can_transition(r.lifecycle, ReplicaLifecycle::SERVING)) {
        return OpResult::invalid("cannot move replica to SERVING");
      }
      r.lifecycle = ReplicaLifecycle::SERVING;
    }
    r.promotion = PromotionState::PRIMARY;
    r.role = ReplicaRole::PRIMARY;
    r.serving_eligible = true;
    r.health_generation = random_id<HealthGeneration>();  // new authority round
    r.health_record.generation = r.health_generation;
    if (r.health == HealthState::UNKNOWN) r.health = HealthState::HEALTHY;
  } else {
    // Standby / canary / replacement / recovered keep serving_eligible false.
    r.promotion = target;
    r.role = (target == PromotionState::RECOVERED) ? ReplicaRole::SECONDARY : ReplicaRole::STANDBY;
    r.serving_eligible = false;
  }

  PromotionRecord rec;
  rec.id = random_id<PromotionId>();
  rec.replica_id = r.id;
  rec.set_id = r.set_id;
  rec.replica_generation = r.generation;
  rec.set_generation = r.set_generation;
  rec.epoch = epoch_;
  rec.from = from;
  rec.to = target;
  rec.when_mono = mono;
  rec.when_wall = wall;
  rec.reason = "promoted to " + std::string(to_string(target));
  promotions_.push_back(std::move(rec));

  // Derive replica-set lifecycle from healthy count.
  std::uint32_t healthy = 0;
  for (const auto& [rid2, r2] : replicas_) {
    if (r2.set_id == set.id && r2.health == HealthState::HEALTHY) ++healthy;
    (void)rid2;
  }
  if (healthy < set.min_healthy && can_transition(set.lifecycle, ReplicaSetLifecycle::DEGRADED)) {
    set.lifecycle = ReplicaSetLifecycle::DEGRADED;
  } else if (healthy >= set.min_healthy &&
             can_transition(set.lifecycle, ReplicaSetLifecycle::AVAILABLE)) {
    set.lifecycle = ReplicaSetLifecycle::AVAILABLE;
  }

  return OpResult::ok("promoted to " + std::string(to_string(target)));
}
OpResult ReplicaSetController::drain_replica(ReplicaId replica_id, const WorkerId& worker,
                                             WorkerBootId boot, MonotonicNs mono) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = replicas_.find(replica_id);
  if (it == replicas_.end()) return OpResult::rejected("unknown replica");
  auto& r = it->second;
  if (!worker_boot_matches(r, worker, boot)) return OpResult::rejected("stale worker boot identity");
  if (r.coordinator_epoch != epoch_) return OpResult::rejected("stale coordinator epoch");
  if (r.lifecycle != ReplicaLifecycle::READY && r.lifecycle != ReplicaLifecycle::SERVING) {
    return OpResult::rejected("only READY or SERVING replicas can be drained");
  }
  // Reject new work: serving authority is dropped immediately.
  r.serving_eligible = false;
  r.role = ReplicaRole::SECONDARY;
  r.promotion = PromotionState::STANDBY;
  if (!can_transition(r.lifecycle, ReplicaLifecycle::DRAINING)) {
    return OpResult::invalid("cannot move replica to DRAINING");
  }
  r.lifecycle = ReplicaLifecycle::DRAINING;
  DrainRecord dr;
  dr.id = random_id<DrainId>();
  dr.replica_id = r.id;
  dr.set_id = r.set_id;
  dr.state = DrainState::DRAINING;
  dr.started_mono = mono;
  dr.reason = "drain requested: reject new work, allow existing to finish";
  drains_.push_back(std::move(dr));
  return OpResult::ok("replica draining (reject new work)");
}

OpResult ReplicaSetController::complete_drain(ReplicaId replica_id, std::uint64_t quiesced,
                                              bool force_cancelled, MonotonicNs mono) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = replicas_.find(replica_id);
  if (it == replicas_.end()) return OpResult::rejected("unknown replica");
  auto& r = it->second;
  if (r.lifecycle != ReplicaLifecycle::DRAINING) {
    return OpResult::rejected("replica is not draining");
  }
  if (!force_cancelled && r.active_requests != 0) {
    return OpResult::rejected("replica still has active requests; drain not complete");
  }
  if (!can_transition(r.lifecycle, ReplicaLifecycle::QUIESCED)) {
    return OpResult::invalid("cannot move replica to QUIESCED");
  }
  r.lifecycle = ReplicaLifecycle::QUIESCED;
  r.active_requests = 0;
  r.serving_eligible = false;
  for (auto& dr : drains_) {
    if (dr.replica_id == r.id && dr.state == DrainState::DRAINING) {
      dr.state = DrainState::QUIESCED;
      dr.completed_mono = mono;
      dr.quiesced_requests = quiesced;
      dr.force_cancelled = force_cancelled;
      break;
    }
  }
  return OpResult::ok("replica quiesced");
}

OpResult ReplicaSetController::fail_replica(ReplicaId replica_id, const WorkerId& worker,
                                            WorkerBootId boot, const std::string& reason,
                                            MonotonicNs mono) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = replicas_.find(replica_id);
  if (it == replicas_.end()) return OpResult::rejected("unknown replica");
  auto& r = it->second;
  // A stale worker (old boot) must not be able to fail a fresh replica.
  if (!worker_boot_matches(r, worker, boot)) return OpResult::rejected("stale worker boot identity");
  r.serving_eligible = false;
  r.health = HealthState::FAILED;
  r.health_record.state = HealthState::FAILED;
  r.health_record.updated_at_mono = mono;
  r.health_record.reason = reason;
  r.promotion = PromotionState::NOT_PROMOTED;
  r.role = ReplicaRole::NONE;
  r.reserved_capacity = 0;
  if (!can_transition(r.lifecycle, ReplicaLifecycle::FAILED)) {
    return OpResult::rejected("replica is already in a terminal/older state");
  }
  r.lifecycle = ReplicaLifecycle::FAILED;
  return OpResult::ok("replica failed and its authority is obsolete");
}

OpResult ReplicaSetController::retire_replica(ReplicaId replica_id, MonotonicNs) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = replicas_.find(replica_id);
  if (it == replicas_.end()) return OpResult::rejected("unknown replica");
  auto& r = it->second;
  if (!can_transition(r.lifecycle, ReplicaLifecycle::RETIRED)) {
    return OpResult::rejected("replica cannot be retired from " + std::string(to_string(r.lifecycle)));
  }
  r.lifecycle = ReplicaLifecycle::RETIRED;
  r.serving_eligible = false;
  r.promotion = PromotionState::NOT_PROMOTED;
  r.role = ReplicaRole::NONE;
  r.reserved_capacity = 0;
  r.active_requests = 0;
  r.memory_residency = MemoryResidencyState::EVICTED;
  r.warmth = WarmthState::INVALIDATED;
  return OpResult::ok("replica retired");
}

OpResult ReplicaSetController::trigger_failover(ReplicaSetId set_id, ReplicaId failed_replica,
                                                const std::string& reason, MonotonicNs mono,
                                                WallNs wall) {
  std::lock_guard<std::mutex> lk(mu_);
  auto sit = sets_.find(set_id);
  if (sit == sets_.end()) return OpResult::rejected("unknown replica set");
  auto& set = sit->second;
  auto fit = replicas_.find(failed_replica);
  if (fit == replicas_.end()) return OpResult::rejected("unknown failed replica");
  auto& fr = fit->second;
  if (fr.set_id != set_id) return OpResult::rejected("failed replica not in this set");

  // Mark failed replica's authority obsolete.
  fr.serving_eligible = false;
  fr.health = HealthState::FAILED;
  fr.health_record.state = HealthState::FAILED;
  fr.health_record.reason = reason;
  fr.promotion = PromotionState::NOT_PROMOTED;
  fr.role = ReplicaRole::NONE;
  fr.reserved_capacity = 0;
  if (fr.lifecycle != ReplicaLifecycle::FAILED) {
    if (can_transition(fr.lifecycle, ReplicaLifecycle::FAILED)) {
      fr.lifecycle = ReplicaLifecycle::FAILED;
    }
  }

  const FailoverPolicy& pol = set.policies.failover;
  // Gather eligible candidates deterministically.
  std::vector<ReplicaId> candidates;
  for (const auto& [rid, r] : replicas_) {
    if (r.set_id != set_id || rid == failed_replica) continue;
    if (!(r.lifecycle == ReplicaLifecycle::READY || r.lifecycle == ReplicaLifecycle::SERVING)) continue;
    if (r.readiness != ReadinessState::READY) continue;
    if (r.health != HealthState::HEALTHY && r.health != HealthState::DEGRADED) continue;
    if (!check_compatibility(set.compatibility, r.compatibility).compatible) continue;  // incompatible
    if (r.placement != PlacementState::ASSIGNED) continue;
    if (r.set_generation != set.generation) continue;
    candidates.push_back(rid);
  }

  auto role_rank = [](PromotionState p) {
    switch (p) {
      case PromotionState::STANDBY: return 0;
      case PromotionState::CANARY: return 1;
      case PromotionState::REPLACEMENT: return 2;
      case PromotionState::RECOVERED: return 3;
      default: return 4;
    }
  };
  std::stable_sort(candidates.begin(), candidates.end(), [&](const ReplicaId& a, const ReplicaId& b) {
    const ReplicaState& ra = replicas_.at(a);
    const ReplicaState& rb = replicas_.at(b);
    if (pol.prefer_standby_first && role_rank(ra.promotion) != role_rank(rb.promotion)) {
      return role_rank(ra.promotion) < role_rank(rb.promotion);
    }
    const bool a_diff = (ra.worker_id != fr.worker_id);
    const bool b_diff = (rb.worker_id != fr.worker_id);
    if (pol.require_diversity && a_diff != b_diff) {
      return a_diff;  // different worker preferred
    }
    return a < b;  // deterministic tie-break by id
  });

  if (candidates.empty()) {
    FailoverRecord f;
    f.id = random_id<FailoverId>();
    f.set_id = set_id;
    f.failed_replica = failed_replica;
    f.state = FailoverState::FAILED;
    f.epoch = epoch_;
    f.started_mono = mono;
    f.completed_mono = mono;
    f.generation_preserved = pol.preserve_generation;
    if (fr.active_requests > 0) f.ambiguous_outcomes.push_back(WorkOutcome::AMBIGUOUS);
    f.reason = reason + "; no eligible replacement";
    failovers_.push_back(std::move(f));
    return OpResult::rejected("no eligible replacement for failover");
  }

  const ReplicaId chosen = candidates.front();
  auto& r = replicas_.at(chosen);
  if (r.lifecycle != ReplicaLifecycle::SERVING) {
    r.lifecycle = ReplicaLifecycle::SERVING;
  }
  r.promotion = PromotionState::PRIMARY;
  r.role = ReplicaRole::PRIMARY;
  r.serving_eligible = true;
  r.health_generation = random_id<HealthGeneration>();
  r.health_record.generation = r.health_generation;

  FailoverRecord f;
  f.id = random_id<FailoverId>();
  f.set_id = set_id;
  f.failed_replica = failed_replica;
  f.replacement = chosen;
  f.state = FailoverState::COMPLETE;
  f.epoch = epoch_;
  f.started_mono = mono;
  f.completed_mono = mono;
  f.generation_preserved = pol.preserve_generation;
  if (fr.active_requests > 0) f.ambiguous_outcomes.push_back(WorkOutcome::AMBIGUOUS);
  f.reason = reason + "; promoted " + chosen.str();
  failovers_.push_back(std::move(f));

  PromotionRecord pr;
  pr.id = random_id<PromotionId>();
  pr.replica_id = chosen;
  pr.set_id = set_id;
  pr.replica_generation = r.generation;
  pr.set_generation = r.set_generation;
  pr.epoch = epoch_;
  pr.from = PromotionState::STANDBY;
  pr.to = PromotionState::PRIMARY;
  pr.when_mono = mono;
  pr.when_wall = wall;
  pr.reason = "failover replacement";
  promotions_.push_back(std::move(pr));

  // Update set lifecycle if below minimum healthy.
  std::uint32_t healthy = 0;
  for (const auto& [rid, r2] : replicas_) {
    if (r2.set_id == set_id && (r2.health == HealthState::HEALTHY)) ++healthy;
    (void)rid;
  }
  if (healthy < set.min_healthy && can_transition(set.lifecycle, ReplicaSetLifecycle::DEGRADED)) {
    set.lifecycle = ReplicaSetLifecycle::DEGRADED;
  } else if (healthy >= set.min_healthy &&
             can_transition(set.lifecycle, ReplicaSetLifecycle::AVAILABLE)) {
    set.lifecycle = ReplicaSetLifecycle::AVAILABLE;
  }

  return OpResult::ok("failover completed; promoted " + chosen.str());
}

const ReplicaState* ReplicaSetController::find_replica(ReplicaId replica_id) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = replicas_.find(replica_id);
  return it == replicas_.end() ? nullptr : &it->second;
}

std::vector<ReplicaId> ReplicaSetController::list_replicas(ReplicaSetId set_id) const {
  std::lock_guard<std::mutex> lk(mu_);
  std::vector<ReplicaId> out;
  for (const auto& [rid, r] : replicas_) {
    if (r.set_id == set_id) out.push_back(rid);
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::vector<ReplicaSetId> ReplicaSetController::list_sets() const {
  std::lock_guard<std::mutex> lk(mu_);
  std::vector<ReplicaSetId> out;
  for (const auto& [sid, s] : sets_) out.push_back(sid);
  std::sort(out.begin(), out.end());
  return out;
}

WorkerBootId ReplicaSetController::current_boot_for(const ReplicaId& replica_id) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = replicas_.find(replica_id);
  if (it == replicas_.end()) return WorkerBootId();
  auto wit = workers_.find(it->second.worker_id);
  if (wit == workers_.end()) return WorkerBootId();
  return wit->second.boot_id;
}

ServingAuthorityDecision ReplicaSetController::servable(ReplicaId replica_id) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = replicas_.find(replica_id);
  if (it == replicas_.end()) {
    ServingAuthorityDecision d;
    d.eligible = false;
    d.rejection = "unknown replica";
    return d;
  }
  const ReplicaState& r = it->second;
  const ReplicaSetState* s = nullptr;
  const auto sit = sets_.find(r.set_id);
  if (sit != sets_.end()) s = &sit->second;
  if (s == nullptr) {
    ServingAuthorityDecision d;
    d.eligible = false;
    d.rejection = "unknown replica set";
    return d;
  }
  ServingAuthorityContext ctx;
  ctx.epoch = epoch_;
  ctx.set_generation = s->generation;
  ctx.policy_generation = s->policy_generation;
  ctx.artifact_generation = s->artifact_generation;
  ctx.health_generation = r.health_generation;
  ctx.policy_fingerprint = s->compatibility.policy_fingerprint;
  auto wit = workers_.find(r.worker_id);
  ctx.worker_boot = (wit == workers_.end()) ? WorkerBootId() : wit->second.boot_id;
  return evaluate_serving_authority(r, *s, ctx);
}

}  // namespace replicafabric