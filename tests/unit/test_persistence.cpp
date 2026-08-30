#include <replicafabric/core/replica_set_controller.hpp>
#include <replicafabric/persistence/snapshot.hpp>

#include <cstdint>
#include <random>
#include <vector>

#include "replicafabric_test.hpp"

namespace {
using namespace replicafabric;
std::mt19937_64 g_rng(0xABCDEFULL);

ReplicaSetState mk_set() {
  ReplicaSetState s;
  s.id = ReplicaSetId::random(g_rng);
  s.model_id = ModelId::random(g_rng);
  s.workload_id = WorkloadId::random(g_rng);
  s.tenant_id = TenantId::random(g_rng);
  s.desired_count = 2; s.min_healthy = 1; s.max_replicas = 2;
  s.artifact_id = ArtifactId::random(g_rng);
  s.artifact_generation = ArtifactGeneration::random(g_rng);
  s.generation = ReplicaSetGeneration::random(g_rng);
  s.policy_generation = PolicyGeneration::random(g_rng);
  s.lifecycle = ReplicaSetLifecycle::CREATED;
  s.compatibility.model_id = s.model_id;
  s.compatibility.backend = BackendKind::TRITON;
  s.compatibility.runtime_name = "triton-3";
  s.compatibility.architecture = "llama";
  s.compatibility.min_compute = {8,0};
  s.compatibility.numeric_mode = NumericMode::FP16;
  s.compatibility.artifact_generation = s.artifact_generation;
  s.compatibility.policy_fingerprint = "fp-1";
  s.backend = BackendKind::TRITON;
  s.runtime_name = "triton-3";
  s.min_compute = {8,0};
  return s;
}

// Build a small controller with one set, one serving primary, and one worker.
void build_controller(ReplicaSetController& ctrl) {
  auto set = mk_set();
  CoordinatorEpoch ep = CoordinatorEpoch::random(g_rng);
  ctrl.create_replica_set(set, ep);

  WorkerRegistration wr;
  wr.worker_id = WorkerId::random(g_rng);
  wr.boot_id = WorkerBootId::random(g_rng);
  wr.node_id = NodeId::random(g_rng);
  wr.protocol_version = 1;
  wr.inventory.total_memory_bytes = 128ULL*1024*1024*1024;
  wr.inventory.free_memory_bytes = 128ULL*1024*1024*1024;
  wr.inventory.accelerator_count = 1;
  wr.inventory.devices.push_back(DeviceCapability{AcceleratorKind::CUDA, "cuda:0", {12,0}, 32ULL*1024*1024*1024});
  wr.inventory.backends.push_back(BackendCapability{BackendKind::TRITON, "triton-3", {12,0}, NumericMode::FP16});
  ctrl.register_worker(wr, 0, 0);

  ReplicaCompatibility compat;
  compat.model_id = set.compatibility.model_id;
  compat.backend = BackendKind::TRITON;
  compat.runtime_name = "triton-3";
  compat.architecture = "llama";
  compat.compute = {12,0};
  compat.numeric_mode = NumericMode::FP16;
  compat.artifact_generation = set.artifact_generation;
  compat.policy_fingerprint = "fp-1";

  ReplicaId id = ReplicaId::random(g_rng);
  ctrl.provision_replica(id, set.id, wr.worker_id, compat,
                         DeviceCapability{AcceleratorKind::CUDA, "cuda:0", {12,0}, 32ULL*1024*1024*1024},
                         16ULL*1024*1024, 0, 0);
  ctrl.set_allocating(id, PlacementId::random(g_rng), 0);
  ctrl.set_starting(id, 0, 0);
  WarmingRecord warm;
  warm.state = WarmthState::WARM; warm.artifact_loading_done=true; warm.weights_resident=true;
  warm.adapters_active=true; warm.kernel_init_done=true; warm.graph_init_done=true;
  warm.allocator_init_done=true; warm.device_context_done=true; warm.warmup_execution_done=true;
  warm.endpoint_registered=true;
  ctrl.warm_replica(id, warm, wr.worker_id, wr.boot_id, 0);
  const ReplicaState* r = ctrl.find_replica(id);
  HealthEvidence he; he.state=HealthState::HEALTHY; he.kind=HealthEvidenceKind::REPORTED;
  he.source="probe"; he.generation=r->health_generation; he.observed_at_mono=0;
  ctrl.report_health(id, he, wr.worker_id, wr.boot_id, 0);
  ReadinessRecord rr;
  rr.factors.model_loaded=true; rr.factors.artifact_validated=true; rr.factors.adapters_present=true;
  rr.factors.kernel_prepared=true; rr.factors.graph_prepared=true; rr.factors.memory_available=true;
  rr.factors.device_context_initialized=true; rr.factors.warmup_complete=true;
  rr.factors.dependencies_ready=true; rr.factors.endpoint_registered=true; rr.factors.policy_current=true;
  rr.state = ReadinessState::READY;
  ctrl.report_readiness(id, rr, wr.worker_id, wr.boot_id, 0);
  ctrl.promote_replica(id, PromotionState::PRIMARY, wr.worker_id, wr.boot_id, 0, 0);
}

}  // namespace

RF_TEST_CASE(snapshot_round_trip) {
  ReplicaSetController ctrl(3);
  build_controller(ctrl);
  auto bytes = make_snapshot(ctrl);
  RF_CHECK(!bytes.empty());
  ReplicaSetController c2(999);
  std::string err;
  RF_CHECK(recover_into(bytes, c2, &err));
  RF_CHECK(c2.list_sets().size() == 1u);
  RF_CHECK(c2.list_replicas(c2.list_sets()[0]).size() == 1u);
  // Recovery must NOT resurrect serving authority.
  for (const auto sid : c2.list_sets()) {
    for (const auto rid : c2.list_replicas(sid)) {
      const ReplicaState* r = c2.find_replica(rid);
      RF_REQUIRE(r != nullptr);
      RF_CHECK(!r->serving_eligible);
      RF_CHECK(r->boot_id.is_null());
      RF_CHECK(r->lifecycle == ReplicaLifecycle::DECLARED);
      RF_CHECK(!c2.servable(rid).eligible);
    }
  }
}

RF_TEST_CASE(snapshot_corruption_rejected) {
  ReplicaSetController ctrl(3);
  build_controller(ctrl);
  auto bytes = make_snapshot(ctrl);
  // Flip bytes at several positions; decode must reject (or not crash).
  for (std::size_t i = 8; i < bytes.size(); i += (bytes.size() / 16) + 1) {
    auto b = bytes;
    b[i] = static_cast<std::uint8_t>(b[i] ^ 0xFF);
    std::string err;
    if (decode_snapshot(b, &err).has_value()) {
      RF_CHECK(false);  // should never accept a corrupted checksum
    }
  }
  RF_CHECK(true);
}

RF_TEST_CASE(snapshot_truncation_rejected) {
  ReplicaSetController ctrl(3);
  build_controller(ctrl);
  auto bytes = make_snapshot(ctrl);
  for (std::size_t cut = 0; cut < bytes.size(); cut += (bytes.size()/8)+1) {
    auto b = std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut));
    RF_CHECK(!decode_snapshot(b).has_value());
    if (cut == 0) break;
  }
  RF_CHECK(true);
}

RF_TEST_CASE(snapshot_trailing_garbage_rejected) {
  ReplicaSetController ctrl(3);
  build_controller(ctrl);
  auto bytes = make_snapshot(ctrl);
  auto b = bytes;
  b.push_back(0xAA); b.push_back(0xBB);
  std::string err;
  RF_CHECK(!decode_snapshot(b, &err).has_value());
  RF_CHECK(err.find("trailing") != std::string::npos || err.find("mismatch") != std::string::npos);
}

RF_TEST_CASE(snapshot_bad_magic_rejected) {
  ReplicaSetController ctrl(3);
  build_controller(ctrl);
  auto bytes = make_snapshot(ctrl);
  auto b = bytes;
  b[0] ^= 0xFF;
  std::string err;
  RF_CHECK(!decode_snapshot(b, &err).has_value());
  RF_CHECK(err == "bad magic");
}

RF_TEST_CASE(snapshot_incompatible_version_rejected) {
  ReplicaSetController ctrl(3);
  build_controller(ctrl);
  auto bytes = make_snapshot(ctrl);
  auto b = bytes;
  b[8] = 0xFF; b[9] = 0xFF; b[10] = 0xFF; b[11] = 0xFF;  // format version = huge
  std::string err;
  RF_CHECK(!decode_snapshot(b, &err).has_value());
  RF_CHECK(err.find("incompatible") != std::string::npos);
}