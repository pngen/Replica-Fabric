#pragma once

// Replica Fabric - the concrete identity kinds used throughout the system.
//
// Each tag struct is empty and only exists to give its corresponding Id a
// unique C++ type. Identity kinds are never reused across authority
// generations.

#include <replicafabric/core/identity.hpp>

namespace replicafabric {

struct ReplicaIdTag {};
struct ReplicaSetIdTag {};
struct ModelIdTag {};
struct ArtifactIdTag {};
struct TenantIdTag {};
struct WorkloadIdTag {};
struct NodeIdTag {};
struct WorkerIdTag {};
struct WorkerBootIdTag {};
struct PlacementIdTag {};
struct PromotionIdTag {};
struct DrainIdTag {};
struct FailoverIdTag {};
struct AttemptIdTag {};
struct CoordinatorEpochTag {};
struct ReplicaGenerationTag {};
struct ReplicaSetGenerationTag {};
struct ArtifactGenerationTag {};
struct HealthGenerationTag {};
struct PolicyGenerationTag {};

using ReplicaId = Id<ReplicaIdTag>;
using ReplicaSetId = Id<ReplicaSetIdTag>;
using ModelId = Id<ModelIdTag>;
using ArtifactId = Id<ArtifactIdTag>;
using TenantId = Id<TenantIdTag>;
using WorkloadId = Id<WorkloadIdTag>;
using NodeId = Id<NodeIdTag>;
using WorkerId = Id<WorkerIdTag>;
using WorkerBootId = Id<WorkerBootIdTag>;
using PlacementId = Id<PlacementIdTag>;
using PromotionId = Id<PromotionIdTag>;
using DrainId = Id<DrainIdTag>;
using FailoverId = Id<FailoverIdTag>;
using AttemptId = Id<AttemptIdTag>;
using CoordinatorEpoch = Id<CoordinatorEpochTag>;
using ReplicaGeneration = Id<ReplicaGenerationTag>;
using ReplicaSetGeneration = Id<ReplicaSetGenerationTag>;
using ArtifactGeneration = Id<ArtifactGenerationTag>;
using HealthGeneration = Id<HealthGenerationTag>;
using PolicyGeneration = Id<PolicyGenerationTag>;

}  // namespace replicafabric
