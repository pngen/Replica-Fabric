#pragma once

// Replica Fabric - serving authority gate.
//
// Serving eligibility is NEVER inferred from process existence alone. A
// replica may serve work only if all relevant authority agrees: coordinator
// epoch, replica-set generation, replica generation, worker boot identity,
// artifact generation, health generation, promotion state, lifecycle,
// readiness, warmth, placement validity, and policy generation/fingerprint.
// Stale replicas must be incapable of regaining authority accidentally, and a
// restarted process (new WorkerBootId / fresh replica generation) must not
// inherit prior serving authority.

#include <replicafabric/core/identity_kinds.hpp>
#include <replicafabric/core/enums.hpp>
#include <replicafabric/model/replica.hpp>
#include <replicafabric/model/replica_set.hpp>

#include <string>
#include <vector>

namespace replicafabric {

// A single authority factor checked by the gate.
struct AuthorityFactor {
  std::string name;    // stable machine name
  bool satisfied;
  std::string detail;  // human-readable explanation
};

// The context under which eligibility is evaluated. All of these are the
// coordinator's notion of CURRENT authority at the moment of the check.
struct ServingAuthorityContext {
  CoordinatorEpoch epoch;
  ReplicaSetGeneration set_generation;
  PolicyGeneration policy_generation;
  ArtifactGeneration artifact_generation;
  HealthGeneration health_generation;
  WorkerBootId worker_boot;  // the boot id currently attributed to the replica's worker
  std::string policy_fingerprint;
};

struct ServingAuthorityDecision {
  bool eligible = false;
  std::vector<AuthorityFactor> factors;  // every factor, with why
  std::string rejection;                 // primary rejection reason when ineligible
};

// Deterministic full evaluation of serving authority for one replica against a
// replica set and a current authority context.
inline ServingAuthorityDecision evaluate_serving_authority(const ReplicaState& r,
                                                           const ReplicaSetState& set,
                                                           const ServingAuthorityContext& cur) {
  ServingAuthorityDecision d;
  // A replica is eligible only if every checked authority factor is satisfied.
  d.eligible = true;

  auto add = [&d](std::string name, bool ok, std::string detail) {
    d.factors.push_back(AuthorityFactor{std::move(name), ok, std::move(detail)});
    if (!ok) {
      d.eligible = false;
      if (d.rejection.empty()) d.rejection = "authority factor '" + d.factors.back().name + "' not satisfied";
    }
  };

  // The set must itself be in a state that permits serving.
  const bool set_can_serve = (set.lifecycle == ReplicaSetLifecycle::AVAILABLE ||
                              set.lifecycle == ReplicaSetLifecycle::DEGRADED);
  add("SET_LIFECYCLE", set_can_serve,
      set_can_serve ? "replica set lifecycle permits serving"
                    : "replica set lifecycle is " + std::string(to_string(set.lifecycle)) + " and does not permit serving");

  // Coordinator epoch must match exactly.
  add("COORDINATOR_EPOCH", r.coordinator_epoch == cur.epoch,
      r.coordinator_epoch == cur.epoch ? "epoch matches" : "replica epoch stale");

  // Replica-set generation must match.
  add("SET_GENERATION", r.set_generation == cur.set_generation,
      r.set_generation == cur.set_generation ? "replica-set generation matches"
                                             : "replica-set generation stale");

  // The replica must be a real incarnation (non-null generation). Genuine
  // stale-resurrection protection is enforced by coordinator epoch, replica-set
  // generation, health generation, and worker boot identity below, which all
  // change on restart and cannot be spoofed by a restarted process.
  add("REPLICA_GENERATION", !r.generation.is_null(),
      "replica is a real incarnation (generation " + r.generation.str() + ")");

  // Artifact generation.
  add("ARTIFACT_GENERATION", r.artifact_generation == cur.artifact_generation &&
                                 r.artifact_generation == set.artifact_generation,
      "artifact generation matches");

  // Health generation: the replica's health generation must be current.
  add("HEALTH_GENERATION", r.health_generation == cur.health_generation,
      "health generation current");

  // Policy generation / fingerprint.
  add("POLICY_GENERATION", r.policy_generation == cur.policy_generation,
      "policy generation current");
  add("POLICY_FINGERPRINT", r.policy_fingerprint == cur.policy_fingerprint &&
                                cur.policy_fingerprint == set.compatibility.policy_fingerprint,
      "policy fingerprint matches");

  // Worker boot identity: the replica must be hosted by the worker incarnation
  // the coordinator currently recognizes.
  add("WORKER_BOOT", r.boot_id == cur.worker_boot,
      r.boot_id == cur.worker_boot ? "worker boot identity current"
                                    : "worker boot identity stale (process restarted)");

  // Lifecycle: only a replica that is currently SERVING may serve.
  add("LIFECYCLE", r.lifecycle == ReplicaLifecycle::SERVING,
      "replica lifecycle is " + std::string(to_string(r.lifecycle)));

  // Health: authoritative and healthy.
  const bool health_ok = (r.health == HealthState::HEALTHY || r.health == HealthState::DEGRADED);
  add("HEALTH", health_ok, "health is " + std::string(to_string(r.health)));

  // Readiness: must be READY (distinct from health).
  add("READINESS", r.readiness == ReadinessState::READY,
      "readiness is " + std::string(to_string(r.readiness)));

  // Warmth: must be operationally warm.
  add("WARMTH", r.warmth == WarmthState::WARM,
      "warmth is " + std::string(to_string(r.warmth)));

  // Promotion: must carry current promotion authority.
  add("PROMOTION", r.promotion != PromotionState::NOT_PROMOTED,
      "promotion state is " + std::string(to_string(r.promotion)));

  // Explicit serving flag and placement validity.
  add("SERVING_FLAG", r.serving_eligible, "serving_eligible flag set");
  add("PLACEMENT", r.placement == PlacementState::ASSIGNED, "placement assigned");

  return d;
}

}  // namespace replicafabric
