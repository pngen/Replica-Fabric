#pragma once

// Replica Fabric - the replica set model. A replica set is the unit of a
// logical service/model: its identity, desired/healthy/max counts, artifact and
// generation, runtime/backend/accelerator/memory requirements, policies,
// placement policy, health/readiness/warming/draining/promotion/failover
// policy, and lifecycle.

#include <replicafabric/core/identity_kinds.hpp>
#include <replicafabric/core/enums.hpp>
#include <replicafabric/core/capability.hpp>
#include <replicafabric/core/compatibility.hpp>
#include <replicafabric/core/policies.hpp>
#include <replicafabric/model/placement_policy.hpp>

#include <cstdint>
#include <string>

namespace replicafabric {

struct ReplicaSetState {
  ReplicaSetId id;
  ModelId model_id;
  WorkloadId workload_id;
  TenantId tenant_id;

  std::uint32_t desired_count = 1;
  std::uint32_t min_healthy = 1;
  std::uint32_t max_replicas = 1;

  ArtifactId artifact_id;
  ArtifactGeneration artifact_generation;
  ReplicaSetGeneration generation;      // replica-set generation (bumps on authority changes)
  PolicyGeneration policy_generation;   // policy generation

  ReplicaSetLifecycle lifecycle = ReplicaSetLifecycle::CREATED;

  CompatibilityRequirements compatibility;
  PolicyBundle policies;
  PlacementPolicy placement_policy;

  // Runtime/backend/accelerator/memory requirements.
  BackendKind backend = BackendKind::NONE;
  std::string runtime_name;
  ComputeCapability min_compute;
  std::uint64_t memory_requirement_bytes = 0;
  std::uint32_t accelerator_requirement = 0;

  friend bool operator==(const ReplicaSetState& a, const ReplicaSetState& b) noexcept {
    return a.id == b.id && a.model_id == b.model_id && a.workload_id == b.workload_id &&
           a.tenant_id == b.tenant_id && a.desired_count == b.desired_count &&
           a.min_healthy == b.min_healthy && a.max_replicas == b.max_replicas &&
           a.artifact_id == b.artifact_id &&
           a.artifact_generation == b.artifact_generation && a.generation == b.generation &&
           a.policy_generation == b.policy_generation && a.lifecycle == b.lifecycle &&
           a.compatibility == b.compatibility && a.policies == b.policies &&
           a.placement_policy == b.placement_policy && a.backend == b.backend &&
           a.runtime_name == b.runtime_name && a.min_compute == b.min_compute &&
           a.memory_requirement_bytes == b.memory_requirement_bytes &&
           a.accelerator_requirement == b.accelerator_requirement;
  }
};

}  // namespace replicafabric
