#include <replicafabric/core/replica_set_controller.hpp>
#include <replicafabric/placement/placement.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

using namespace replicafabric;

static std::mt19937_64 g_rng(0x1234ULL);

static ReplicaSetState make_set(int max) {
  ReplicaSetState s;
  s.id = ReplicaSetId::random(g_rng);
  s.model_id = ModelId::random(g_rng);
  s.artifact_id = ArtifactId::random(g_rng);
  s.artifact_generation = ArtifactGeneration::random(g_rng);
  s.generation = ReplicaSetGeneration::random(g_rng);
  s.policy_generation = PolicyGeneration::random(g_rng);
  s.workload_id = WorkloadId::random(g_rng);
  s.tenant_id = TenantId::random(g_rng);
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

static inline double ms(const std::chrono::steady_clock::time_point& a,
                        const std::chrono::steady_clock::time_point& b) {
  return static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count());
}

static void run_lifecycle(int threads, int iters) {
  ReplicaSetController ctrl(1);
  auto set = make_set(8);
  ctrl.create_replica_set(set, CoordinatorEpoch::random(g_rng));
  ctrl.transition_replica_set(set.id, ReplicaSetLifecycle::PROVISIONING);
  ctrl.transition_replica_set(set.id, ReplicaSetLifecycle::AVAILABLE);
  std::atomic<long> ops{0};
  const auto t0 = std::chrono::steady_clock::now();
  std::vector<std::thread> ts;
  for (int t = 0; t < threads; ++t) {
    ts.emplace_back([&]() {
      for (int i = 0; i < iters; ++i) {
        ctrl.transition_replica_set(set.id, ReplicaSetLifecycle::DEGRADED);
        ctrl.transition_replica_set(set.id, ReplicaSetLifecycle::AVAILABLE);
        ops.fetch_add(2, std::memory_order_relaxed);
      }
    });
  }
  for (auto& th : ts) th.join();
  const double d = ms(t0, std::chrono::steady_clock::now());
  const long total = ops.load();
  std::cout << "  lifecycle " << threads << "-thread: " << total << " transitions in " << d
            << " ms (" << (d > 0 ? static_cast<long>(total / static_cast<double>(d)) : 0) << " ops/ms)\n";
}

static void run_placement(int threads, int iters) {
  PlacementEngine eng(5);
  auto set = make_set(2);
  std::vector<PlacementHost> hosts;
  for (int i = 0; i < 16; ++i) {
    PlacementHost h;
    h.worker_id = WorkerId::random(g_rng);
    h.boot_id = WorkerBootId::random(g_rng);
    h.node_id = NodeId::random(g_rng);
    h.host = "host-" + std::to_string(i);
    h.numa = "numa-0";
    h.device = DeviceCapability{AcceleratorKind::CUDA, "cuda:" + std::to_string(i), {12, 0}, 32ULL * 1024 * 1024 * 1024};
    h.inventory.total_memory_bytes = 64ULL * 1024 * 1024 * 1024;
    h.inventory.free_memory_bytes = h.inventory.total_memory_bytes;
    h.inventory.accelerator_count = 1;
    h.inventory.backends.push_back(BackendCapability{BackendKind::TRITON, "triton-3", {12, 0}, NumericMode::FP16});
    h.failure_domain_labels = {"rack-" + std::to_string(i % 2)};
    hosts.push_back(std::move(h));
  }
  std::atomic<long> ops{0};
  const auto t0 = std::chrono::steady_clock::now();
  std::vector<std::thread> ts;
  for (int t = 0; t < threads; ++t) {
    ts.emplace_back([&]() {
      for (int i = 0; i < iters; ++i) {
        if (eng.place(set, hosts, {}).placed) ops.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }
  for (auto& th : ts) th.join();
  const double d = ms(t0, std::chrono::steady_clock::now());
  const long total = ops.load();
  std::cout << "  placement " << threads << "-thread: " << total << " placements in " << d
            << " ms (" << (d > 0 ? static_cast<long>(total / static_cast<double>(d)) : 0) << " ops/ms)\n";
}

static void run_pool(int n) {
  ReplicaSetController ctrl(2);
  auto set = make_set(n);
  ctrl.create_replica_set(set, CoordinatorEpoch::random(g_rng));
  WorkerRegistration wr;
  wr.worker_id = WorkerId::random(g_rng);
  wr.boot_id = WorkerBootId::random(g_rng);
  wr.node_id = NodeId::random(g_rng);
  wr.protocol_version = 1;
  wr.inventory.total_memory_bytes = 1024ULL * 1024 * 1024 * 1024;
  wr.inventory.free_memory_bytes = wr.inventory.total_memory_bytes;
  wr.inventory.accelerator_count = 16;
  for (int i = 0; i < 16; ++i) wr.inventory.devices.push_back(DeviceCapability{AcceleratorKind::CUDA, "cuda:" + std::to_string(i), {12, 0}, 32ULL * 1024 * 1024 * 1024});
  wr.inventory.backends.push_back(BackendCapability{BackendKind::TRITON, "triton-3", {12, 0}, NumericMode::FP16});
  ctrl.register_worker(wr, 0, 0);
  const ReplicaSetState* sp = ctrl.find_set(set.id);
  ReplicaCompatibility compat;
  if (sp) { compat.model_id = sp->compatibility.model_id; compat.backend = sp->compatibility.backend;
    compat.runtime_name = sp->compatibility.runtime_name; compat.architecture = sp->compatibility.architecture;
    compat.compute = {12, 0}; compat.numeric_mode = sp->compatibility.numeric_mode;
    compat.artifact_generation = sp->compatibility.artifact_generation; compat.policy_fingerprint = sp->compatibility.policy_fingerprint; }
  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < n; ++i) {
    ctrl.provision_replica(ReplicaId::random(g_rng), set.id, wr.worker_id, compat,
                           wr.inventory.devices[i % 16], 8ULL * 1024 * 1024, 0, 0);
    ctrl.set_allocating(ctrl.list_replicas(set.id).back(), PlacementId::random(g_rng), 0);
    ctrl.set_starting(ctrl.list_replicas(set.id).back(), 0, 0);
  }
  const double d = ms(t0, std::chrono::steady_clock::now());
  std::cout << "  replica-pool: provisioned+allocated+started " << n << " replicas in " << d
            << " ms (" << (d > 0 ? static_cast<long>(n / static_cast<double>(d)) : 0) << " ops/ms)\n";
}

int main() {
  std::cout << "Replica Fabric benchmarks\n";
  for (int th : {1, 4, 8}) run_lifecycle(th, 5000);
  for (int th : {1, 4, 8}) run_placement(th, 5000);
  run_pool(2000);
  return 0;
}
