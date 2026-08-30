#pragma once

// Shared example scaffolding: build a replica set and bring a replica to
// READY + HEALTHY (provision -> allocate -> start -> warm -> health ->
// readiness). Used by the warm/promote and drain examples to avoid duplication.

#include <replicafabric/core/replica_set_controller.hpp>

#include <random>

namespace rf_example {

using namespace replicafabric;

inline std::mt19937_64& rng() {
  static std::mt19937_64 r(0xABCDEFULL);
  return r;
}

inline ReplicaSetState make_set(int max) {
  ReplicaSetState s;
  s.id = ReplicaSetId::random(rng());
  s.model_id = ModelId::random(rng());
  s.artifact_id = ArtifactId::random(rng());
  s.artifact_generation = ArtifactGeneration::random(rng());
  s.generation = ReplicaSetGeneration::random(rng());
  s.policy_generation = PolicyGeneration::random(rng());
  s.workload_id = WorkloadId::random(rng());
  s.tenant_id = TenantId::random(rng());
  s.desired_count = static_cast<std::uint32_t>(max);
  s.min_healthy = 1;
  s.max_replicas = static_cast<std::uint32_t>(max);
  s.lifecycle = ReplicaSetLifecycle::CREATED;
  s.compatibility.model_id = s.model_id;
  s.compatibility.backend = BackendKind::TRITON;
  s.compatibility.runtime_name = "triton-3";
  s.compatibility.architecture = "llama";
  s.compatibility.min_compute = {8, 0};
  s.compatibility.numeric_mode = NumericMode::FP16;
  s.compatibility.artifact_generation = s.artifact_generation;
  s.compatibility.policy_fingerprint = "fp-1";
  s.backend = BackendKind::TRITON;
  s.runtime_name = "triton-3";
  s.min_compute = {8, 0};
  s.memory_requirement_bytes = 16 * 1024 * 1024;
  s.accelerator_requirement = 1;
  s.placement_policy.anti_affinity_domains = {"host"};
  s.placement_policy.require_diversity = true;
  s.placement_policy.synthetic_domains = {{"rack-A", "rack"}, {"rack-B", "rack"}};
  return s;
}

struct ReplicaHandle {
  ReplicaId id;
  WorkerId worker;
  WorkerBootId boot;
  NodeId node;
};

inline ReplicaHandle provision_ready(ReplicaSetController& ctrl, const ReplicaSetId& set) {
  ReplicaHandle h;
  h.id = ReplicaId::random(rng());
  h.worker = WorkerId::random(rng());
  h.boot = WorkerBootId::random(rng());
  h.node = NodeId::random(rng());

  WorkerRegistration wr;
  wr.worker_id = h.worker;
  wr.boot_id = h.boot;
  wr.node_id = h.node;
  wr.protocol_version = 1;
  wr.inventory.total_memory_bytes = 64ULL * 1024 * 1024 * 1024;
  wr.inventory.free_memory_bytes = wr.inventory.total_memory_bytes;
  wr.inventory.accelerator_count = 1;
  wr.inventory.devices.push_back(DeviceCapability{AcceleratorKind::CUDA, "cuda:0", {12, 0}, 32ULL * 1024 * 1024 * 1024});
  wr.inventory.backends.push_back(BackendCapability{BackendKind::TRITON, "triton-3", {12, 0}, NumericMode::FP16});
  ctrl.register_worker(wr, 0, 0);

  const ReplicaSetState* sp = ctrl.find_set(set);
  ReplicaCompatibility compat;
  if (sp) {
    compat.model_id = sp->compatibility.model_id;
    compat.backend = sp->compatibility.backend;
    compat.runtime_name = sp->compatibility.runtime_name;
    compat.architecture = sp->compatibility.architecture;
    compat.compute = {12, 0};
    compat.numeric_mode = sp->compatibility.numeric_mode;
    compat.artifact_generation = sp->compatibility.artifact_generation;
    compat.policy_fingerprint = sp->compatibility.policy_fingerprint;
  }
  ctrl.provision_replica(h.id, set, h.worker, compat, wr.inventory.devices[0], 8ULL * 1024 * 1024, 0, 0);
  ctrl.set_allocating(h.id, PlacementId::random(rng()), 0);
  ctrl.set_starting(h.id, 0, 0);

  WarmingRecord warm;
  warm.state = WarmthState::WARM;
  warm.artifact_loading_done = true; warm.weights_resident = true; warm.adapters_active = true;
  warm.kernel_init_done = true; warm.graph_init_done = true; warm.allocator_init_done = true;
  warm.device_context_done = true; warm.warmup_execution_done = true; warm.endpoint_registered = true;
  warm.steps_required = 4; warm.steps_completed = 4;
  ctrl.warm_replica(h.id, warm, h.worker, h.boot, 0);

  const ReplicaState* rs = ctrl.find_replica(h.id);
  HealthEvidence he;
  he.state = HealthState::HEALTHY;
  he.kind = HealthEvidenceKind::REPORTED;
  he.source = "example";
  if (rs) he.generation = rs->health_generation;
  he.observed_at_mono = 0;
  ctrl.report_health(h.id, he, h.worker, h.boot, 0);

  ReadinessRecord rr;
  rr.factors.model_loaded = true; rr.factors.artifact_validated = true; rr.factors.adapters_present = true;
  rr.factors.kernel_prepared = true; rr.factors.graph_prepared = true; rr.factors.memory_available = true;
  rr.factors.device_context_initialized = true; rr.factors.warmup_complete = true; rr.factors.dependencies_ready = true;
  rr.factors.endpoint_registered = true; rr.factors.policy_current = true;
  rr.state = ReadinessState::READY;
  ctrl.report_readiness(h.id, rr, h.worker, h.boot, 0);
  return h;
}

}  // namespace rf_example
