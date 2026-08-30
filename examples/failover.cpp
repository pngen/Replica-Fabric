// Example 2: two-replica set with failover after the primary's worker dies.
#include <replicafabric/core/replica_set_controller.hpp>
#include <iostream>
#include <random>

using namespace replicafabric;

static ReplicaSetState mk(std::mt19937_64& rng) {
  ReplicaSetState s; s.id = ReplicaSetId::random(rng); s.model_id = ModelId::random(rng);
  s.artifact_id = ArtifactId::random(rng); s.artifact_generation = ArtifactGeneration::random(rng);
  s.generation = ReplicaSetGeneration::random(rng); s.policy_generation = PolicyGeneration::random(rng);
  s.workload_id = WorkloadId::random(rng); s.tenant_id = TenantId::random(rng);
  s.desired_count = 2; s.min_healthy = 1; s.max_replicas = 2; s.lifecycle = ReplicaSetLifecycle::CREATED;
  s.compatibility.model_id = s.model_id; s.compatibility.backend = BackendKind::TRITON;
  s.compatibility.runtime_name = "triton-3"; s.compatibility.architecture = "llama";
  s.compatibility.min_compute = {8,0}; s.compatibility.numeric_mode = NumericMode::FP16;
  s.compatibility.artifact_generation = s.artifact_generation; s.compatibility.policy_fingerprint = "fp-1";
  s.backend = BackendKind::TRITON; s.min_compute = {8,0};
  s.placement_policy.anti_affinity_domains = {"host"}; s.placement_policy.require_diversity = true;
  return s;
}
static void full(ReplicaSetController& ctrl, ReplicaId r, const ReplicaSetId& set, WorkerId w, WorkerBootId b, NodeId n) {
  WorkerRegistration wr; wr.worker_id = w; wr.boot_id = b; wr.node_id = n; wr.protocol_version = 1;
  wr.inventory.total_memory_bytes = 64ULL*1024*1024*1024; wr.inventory.free_memory_bytes = wr.inventory.total_memory_bytes; wr.inventory.accelerator_count = 1;
  wr.inventory.devices.push_back(DeviceCapability{AcceleratorKind::CUDA, "cuda:0", {12,0}, 32ULL*1024*1024*1024});
  wr.inventory.backends.push_back(BackendCapability{BackendKind::TRITON, "triton-3", {12,0}, NumericMode::FP16});
  ctrl.register_worker(wr, 0, 0);
  const ReplicaSetState* sp = ctrl.find_set(set);
  ReplicaCompatibility compat; if (sp) { compat.model_id = sp->compatibility.model_id; compat.backend = sp->compatibility.backend;
    compat.runtime_name = sp->compatibility.runtime_name; compat.architecture = sp->compatibility.architecture;
    compat.compute = {12,0}; compat.numeric_mode = sp->compatibility.numeric_mode;
    compat.artifact_generation = sp->compatibility.artifact_generation; compat.policy_fingerprint = sp->compatibility.policy_fingerprint; }
  ctrl.provision_replica(r, set, w, compat, wr.inventory.devices[0], 8ULL*1024*1024, 0, 0);
  ctrl.set_allocating(r, PlacementId::random(*(new std::mt19937_64(9))), 0);
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
}
int main() {
  std::mt19937_64 rng(7);
  ReplicaSetController ctrl(7);
  auto set = mk(rng);
  ctrl.create_replica_set(set, CoordinatorEpoch::random(rng));
  WorkerId wa = WorkerId::random(rng); WorkerBootId ba = WorkerBootId::random(rng); NodeId na = NodeId::random(rng);
  WorkerId wb = WorkerId::random(rng); WorkerBootId bb = WorkerBootId::random(rng); NodeId nb = NodeId::random(rng);
  ReplicaId ra = ReplicaId::random(rng); ReplicaId rb = ReplicaId::random(rng);
  full(ctrl, ra, set.id, wa, ba, na);
  full(ctrl, rb, set.id, wb, bb, nb);
  ctrl.promote_replica(ra, PromotionState::PRIMARY, wa, ba, 0, 0);
  ctrl.promote_replica(rb, PromotionState::STANDBY, wb, bb, 0, 0);
  std::cout << "A serving=" << ctrl.servable(ra).eligible << " B serving=" << ctrl.servable(rb).eligible << "\n";
  ctrl.unregister_worker(wa);            // worker A dies
  ctrl.trigger_failover(set.id, ra, "worker A died", 0, 0);
  bool ok = ctrl.servable(rb).eligible;
  std::cout << "after failover, B serving=" << ok << " A serving=" << ctrl.servable(ra).eligible << "\n";
  std::cout << "two-replica failover example: " << (ok ? "PASSED" : "FAILED") << "\n";
  return ok ? 0 : 1;
}
