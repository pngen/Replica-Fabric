// Example 1: basic single-replica lifecycle (declare -> allocate -> start ->
// warm -> ready -> serve). Demonstrates the guarded state machine.
#include <replicafabric/core/replica_set_controller.hpp>
#include <iostream>
#include <random>

using namespace replicafabric;

int main() {
  std::mt19937_64 rng(1);
  ReplicaSetController ctrl(1);
  ReplicaSetState set; set.id = ReplicaSetId::random(rng); set.model_id = ModelId::random(rng);
  set.artifact_id = ArtifactId::random(rng); set.artifact_generation = ArtifactGeneration::random(rng);
  set.generation = ReplicaSetGeneration::random(rng); set.policy_generation = PolicyGeneration::random(rng);
  set.desired_count = 1; set.min_healthy = 1; set.max_replicas = 1; set.lifecycle = ReplicaSetLifecycle::CREATED;
  set.compatibility.model_id = set.model_id; set.compatibility.backend = BackendKind::TRITON;
  set.compatibility.runtime_name = "triton-3"; set.compatibility.architecture = "llama";
  set.compatibility.min_compute = {8,0}; set.compatibility.numeric_mode = NumericMode::FP16;
  set.compatibility.artifact_generation = set.artifact_generation; set.compatibility.policy_fingerprint = "fp-1";
  set.backend = BackendKind::TRITON; set.min_compute = {8,0};
  ctrl.create_replica_set(set, CoordinatorEpoch::random(rng));

  WorkerId w = WorkerId::random(rng); WorkerBootId b = WorkerBootId::random(rng);
  WorkerRegistration wr; wr.worker_id = w; wr.boot_id = b; wr.node_id = NodeId::random(rng); wr.protocol_version = 1;
  wr.inventory.total_memory_bytes = 64ULL*1024*1024*1024; wr.inventory.free_memory_bytes = wr.inventory.total_memory_bytes; wr.inventory.accelerator_count = 1;
  wr.inventory.devices.push_back(DeviceCapability{AcceleratorKind::CUDA, "cuda:0", {12,0}, 32ULL*1024*1024*1024});
  wr.inventory.backends.push_back(BackendCapability{BackendKind::TRITON, "triton-3", {12,0}, NumericMode::FP16});
  ctrl.register_worker(wr, 0, 0);

  ReplicaCompatibility compat; compat.model_id = set.compatibility.model_id; compat.backend = BackendKind::TRITON;
  compat.runtime_name = "triton-3"; compat.architecture = "llama"; compat.compute = {12,0};
  compat.numeric_mode = NumericMode::FP16; compat.artifact_generation = set.artifact_generation; compat.policy_fingerprint = "fp-1";
  ReplicaId r = ReplicaId::random(rng);
  ctrl.provision_replica(r, set.id, w, compat, wr.inventory.devices[0], 8ULL*1024*1024, 0, 0);
  ctrl.set_allocating(r, PlacementId::random(rng), 0);
  ctrl.set_starting(r, 0, 0);
  WarmingRecord warm; warm.state = WarmthState::WARM; warm.artifact_loading_done=true; warm.weights_resident=true;
  warm.adapters_active=true; warm.kernel_init_done=true; warm.graph_init_done=true; warm.allocator_init_done=true;
  warm.device_context_done=true; warm.warmup_execution_done=true; warm.endpoint_registered=true;
  ctrl.warm_replica(r, warm, w, b, 0);
  const ReplicaState* rs = ctrl.find_replica(r);
  HealthEvidence he; he.state=HealthState::HEALTHY; he.kind=HealthEvidenceKind::REPORTED; he.source="probe";
  if (rs) he.generation = rs->health_generation; he.observed_at_mono = 0;
  ctrl.report_health(r, he, w, b, 0);
  ReadinessRecord rr; rr.factors.model_loaded=true; rr.factors.artifact_validated=true; rr.factors.adapters_present=true;
  rr.factors.kernel_prepared=true; rr.factors.graph_prepared=true; rr.factors.memory_available=true;
  rr.factors.device_context_initialized=true; rr.factors.warmup_complete=true; rr.factors.dependencies_ready=true;
  rr.factors.endpoint_registered=true; rr.factors.policy_current=true; rr.state=ReadinessState::READY;
  ctrl.report_readiness(r, rr, w, b, 0);
  ctrl.promote_replica(r, PromotionState::PRIMARY, w, b, 0, 0);

  const ReplicaState* final = ctrl.find_replica(r);
  std::cout << "replica lifecycle=" << (final ? std::string(to_string(final->lifecycle)) : "?")
            << " health=" << (final ? std::string(to_string(final->health)) : "?")
            << " serving=" << (final ? std::string(final->serving_eligible ? "yes" : "no") : "?") << "\n";
  std::cout << "basic lifecycle example: " << (final && final->serving_eligible ? "PASSED" : "FAILED") << "\n";
  return final && final->serving_eligible ? 0 : 1;
}
