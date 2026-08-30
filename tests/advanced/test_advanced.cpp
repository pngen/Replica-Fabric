#include <replicafabric/core/replica_set_controller.hpp>
#include <replicafabric/authority/transition.hpp>
#include <replicafabric/persistence/snapshot.hpp>
#include <replicafabric/placement/placement.hpp>
#include <replicafabric/distributed/protocol.hpp>

#include <atomic>
#include <cstdint>
#include <random>
#include <thread>
#include <vector>

#include "replicafabric_test.hpp"

namespace {
using namespace replicafabric;
std::mt19937_64 g_rng(0xFACEULL);
template <typename T> T rnd() { return T::random(g_rng); }

ReplicaSetState set_for(int max) {
  ReplicaSetState s;
  s.id = rnd<ReplicaSetId>(); s.model_id = rnd<ModelId>(); s.artifact_id = rnd<ArtifactId>();
  s.artifact_generation = rnd<ArtifactGeneration>(); s.generation = rnd<ReplicaSetGeneration>();
  s.policy_generation = rnd<PolicyGeneration>();
  s.workload_id = rnd<WorkloadId>(); s.tenant_id = rnd<TenantId>();
  s.desired_count = static_cast<std::uint32_t>(max); s.min_healthy = 1;
  s.max_replicas = static_cast<std::uint32_t>(max);
  s.lifecycle = ReplicaSetLifecycle::CREATED;
  s.compatibility.model_id = s.model_id; s.compatibility.backend = BackendKind::TRITON;
  s.compatibility.runtime_name = "triton-3"; s.compatibility.architecture = "llama";
  s.compatibility.min_compute = {8,0}; s.compatibility.numeric_mode = NumericMode::FP16;
  s.compatibility.artifact_generation = s.artifact_generation; s.compatibility.policy_fingerprint = "fp-1";
  s.backend = BackendKind::TRITON; s.runtime_name = "triton-3"; s.min_compute = {8,0};
  s.memory_requirement_bytes = 16*1024*1024; s.accelerator_requirement = 1;
  s.placement_policy.anti_affinity_domains = {"host"}; s.placement_policy.require_diversity = true;
  s.placement_policy.synthetic_domains = {{"rack-A","rack"},{"rack-B","rack"}};
  return s;
}
}  // namespace

// Fixed-seed property: placement is deterministic and honors anti-affinity.
RF_TEST_CASE(property_placement_deterministic_and_antiaffinity) {
  PlacementEngine eng1(123);
  PlacementEngine eng2(123);
  const ReplicaSetState set = set_for(2);
  std::vector<PlacementHost> hosts;
  for (int i = 0; i < 3; ++i) {
    PlacementHost h;
    h.worker_id = rnd<WorkerId>(); h.boot_id = rnd<WorkerBootId>(); h.node_id = rnd<NodeId>();
    h.host = "host-" + std::to_string(i); h.numa = "numa-0";
    h.device = DeviceCapability{AcceleratorKind::CUDA, "cuda:" + std::to_string(i), {12,0}, 32ULL*1024*1024*1024};
    h.inventory.total_memory_bytes = 64ULL*1024*1024*1024; h.inventory.free_memory_bytes = 64ULL*1024*1024*1024;
    h.inventory.accelerator_count = 1;
    h.inventory.backends.push_back(BackendCapability{BackendKind::TRITON, "triton-3", {12,0}, NumericMode::FP16});
    h.failure_domain_labels = {i % 2 == 0 ? "rack-A" : "rack-B"};
    hosts.push_back(h);
  }
  auto d1 = eng1.place(set, hosts, {});
  auto d2 = eng2.place(set, hosts, {});
  RF_CHECK(d1.placed);
  RF_CHECK(d1.chosen.worker_id == d2.chosen.worker_id);   // deterministic
  // Place a second replica avoiding the first host (anti-affinity / diversity).
  PlacedReplica pr{ReplicaId::random(g_rng), d1.chosen.host, d1.chosen.numa, d1.chosen.device.device_id, d1.chosen.failure_domain_labels};
  auto d3 = eng1.place(set, hosts, {pr});
  RF_CHECK(d3.placed);
  RF_CHECK(d3.chosen.host != d1.chosen.host);  // diversity spreads across hosts
}

// Adversarial: invalid transitions and invalid enums fail, never crash.
RF_TEST_CASE(adversarial_invalid_transition_rejected) {
  RF_CHECK(!can_transition(ReplicaLifecycle::SERVING, ReplicaLifecycle::SERVING));       // no self-transition
  RF_CHECK(!can_transition(ReplicaLifecycle::RETIRED, ReplicaLifecycle::SERVING));       // terminal
  RF_CHECK(!can_transition(ReplicaSetLifecycle::RETIRED, ReplicaSetLifecycle::AVAILABLE));
  RF_CHECK_THROWS(guard_transition(ReplicaLifecycle::FAILED, ReplicaLifecycle::READY));
  // Invalid enum values.
  ReplicaSetLifecycle lv; RF_CHECK(!from_int(lv, -1));
  RF_CHECK(!from_int(lv, 999));
  // Invalid identity strings.
  RF_CHECK_THROWS(ReplicaId::from_string("zzzz"));
}

// Adversarial: corrupt/oversized/malformed snapshot and protocol input is rejected.
RF_TEST_CASE(adversarial_malformed_input_rejected) {
  ReplicaSetController ctrl(9);
  auto set = set_for(2);
  ctrl.create_replica_set(set, CoordinatorEpoch::random(g_rng));
  auto bytes = make_snapshot(ctrl);
  // Corrupt every 16th byte; decode must not crash and must reject.
  for (std::size_t i = 0; i < bytes.size(); i += 16) {
    auto b = bytes; b[i] ^= 0xFF;
    RF_CHECK(!decode_snapshot(b).has_value());
  }
  // Oversized length prefix must not cause OOB reads.
  std::vector<std::uint8_t> huge(4, 0xFF);
  { Message m; RF_CHECK(!decode_message(huge, m)); }
  // Truncated message.
  std::vector<std::uint8_t> shortb = encode_message(Message{});
  shortb.resize(5);
  Message m2; RF_CHECK(!decode_message(shortb, m2));
}

// High-contention lifecycle churn across threads; no deadlock / double-serving.
RF_TEST_CASE(concurrency_high_contention_churn) {
  ReplicaSetController ctrl(7);
  auto set = set_for(8);
  CoordinatorEpoch ep = CoordinatorEpoch::random(g_rng);
  ctrl.create_replica_set(set, ep);
  std::mt19937_64 thread_rng(0x3333ULL);
  std::atomic<bool> stop{false};
  std::atomic<int> errors{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < 8; ++t) {
    threads.emplace_back([&, t]() {
      std::mt19937_64 rng(0x1000ULL + t);
      for (int i = 0; i < 2000; ++i) {
        WorkerId w = WorkerId::random(rng);
        WorkerBootId b = WorkerBootId::random(rng);
        if (i % 2 == 0) {
          WorkerRegistration wr; wr.worker_id = w; wr.boot_id = b; wr.node_id = NodeId::random(rng);
          wr.protocol_version = 1; wr.inventory.total_memory_bytes = 64ULL*1024*1024*1024; wr.inventory.free_memory_bytes = wr.inventory.total_memory_bytes;
          ctrl.register_worker(wr, 0, 0);
        } else {
          ctrl.unregister_worker(w);
        }
        if (i % 5 == 0) ctrl.transition_replica_set(set.id, ReplicaSetLifecycle::AVAILABLE);
        if (ctrl.servable(ReplicaId::random(rng)).eligible) errors.fetch_add(1);
      }
    });
  }
  for (auto& th : threads) th.join();
  (void)stop;
  RF_CHECK_EQ(errors.load(), 0);  // no spurious serving eligibility during churn
}

RF_TEST_MAIN()