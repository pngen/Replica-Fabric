#pragma once

// Replica Fabric - the replica model. A replica is one live runtime instance
// derived from an artifact, hosted on a worker, under one authority generation.
// Identity is never silently reused across authority generations; every restart
// yields a fresh WorkerBootId and replica generation as appropriate.

#include <replicafabric/core/identity_kinds.hpp>
#include <replicafabric/core/enums.hpp>
#include <replicafabric/core/capability.hpp>
#include <replicafabric/core/compatibility.hpp>
#include <replicafabric/core/time.hpp>
#include <replicafabric/model/health.hpp>
#include <replicafabric/model/readiness.hpp>
#include <replicafabric/model/warming.hpp>

#include <cstdint>
#include <string>

namespace replicafabric {

struct ReplicaState {
  ReplicaId id;
  ReplicaSetId set_id;
  ReplicaGeneration generation;      // replica generation (fresh per incarnation)

  NodeId node_id;
  WorkerId worker_id;
  WorkerBootId boot_id;              // fresh per process lifetime; never inherited

  ArtifactId artifact_id;
  ArtifactGeneration artifact_generation;

  BackendKind backend = BackendKind::NONE;
  std::string backend_identity;      // backend/runtime identity
  std::string device_identity;       // e.g. "cuda:0", "cpu:0"
  AcceleratorKind accelerator = AcceleratorKind::NONE;
  ComputeCapability compute_capability;

  MemoryResidencyState memory_residency = MemoryResidencyState::UNKNOWN;
  WarmthState warmth = WarmthState::COLD;
  ReadinessState readiness = ReadinessState::UNKNOWN;
  HealthState health = HealthState::UNKNOWN;
  ReplicaLifecycle lifecycle = ReplicaLifecycle::DECLARED;

  PlacementId placement_id;
  PlacementState placement = PlacementState::PENDING;
  PromotionState promotion = PromotionState::NOT_PROMOTED;
  ReplicaRole role = ReplicaRole::NONE;

  bool serving_eligible = false;

  std::uint64_t active_requests = 0;
  std::uint64_t reserved_capacity = 0;
  std::uint64_t capacity_total = 0;

  MonotonicNs start_time_mono = 0;
  WallNs start_time_wall = 0;

  HealthRecord health_record;
  ReadinessRecord readiness_record;
  WarmingRecord warming_record;

  // Authority metadata under which this incarnation lives. All must be current
  // for the replica to be eligible to serve.
  CoordinatorEpoch coordinator_epoch;
  ReplicaSetGeneration set_generation;
  HealthGeneration health_generation;
  PolicyGeneration policy_generation;
  std::string policy_fingerprint;
  ReplicaCompatibility compatibility;

  friend bool operator==(const ReplicaState& a, const ReplicaState& b) noexcept {
    return a.id == b.id && a.set_id == b.set_id && a.generation == b.generation &&
           a.node_id == b.node_id && a.worker_id == b.worker_id && a.boot_id == b.boot_id &&
           a.artifact_id == b.artifact_id && a.artifact_generation == b.artifact_generation &&
           a.backend == b.backend && a.backend_identity == b.backend_identity &&
           a.device_identity == b.device_identity && a.accelerator == b.accelerator &&
           a.compute_capability == b.compute_capability &&
           a.memory_residency == b.memory_residency && a.warmth == b.warmth &&
           a.readiness == b.readiness && a.health == b.health && a.lifecycle == b.lifecycle &&
           a.placement_id == b.placement_id && a.placement == b.placement &&
           a.promotion == b.promotion && a.role == b.role &&
           a.serving_eligible == b.serving_eligible &&
           a.active_requests == b.active_requests &&
           a.reserved_capacity == b.reserved_capacity && a.capacity_total == b.capacity_total &&
           a.start_time_mono == b.start_time_mono && a.start_time_wall == b.start_time_wall &&
           a.health_record == b.health_record && a.readiness_record == b.readiness_record &&
           a.warming_record == b.warming_record &&
           a.coordinator_epoch == b.coordinator_epoch &&
           a.set_generation == b.set_generation && a.health_generation == b.health_generation &&
           a.policy_generation == b.policy_generation &&
           a.policy_fingerprint == b.policy_fingerprint && a.compatibility == b.compatibility;
  }
};

}  // namespace replicafabric
