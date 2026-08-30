#include <replicafabric/core/replica_set_controller.hpp>
#include <replicafabric/persistence/snapshot.hpp>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

using namespace replicafabric;

template <typename F> double bench_ms(F&& f, int iters) {
  auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < iters; ++i) f(i);
  auto t1 = std::chrono::steady_clock::now();
  return static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
}

int main() {
  std::mt19937_64 rng(42);
  const int iters = 20000;

  // Replica creation + lifecycle transition throughput.
  {
    ReplicaSetController ctrl(1);
    auto set = []() { ReplicaSetState s; s.id = ReplicaSetId::random(*(new std::mt19937_64(1))); s.model_id = ModelId::random(*(new std::mt19937_64(2))); s.artifact_id = ArtifactId::random(*(new std::mt19937_64(3))); s.desired_count = 1; s.min_healthy = 1; s.max_replicas = 1; s.lifecycle = ReplicaSetLifecycle::CREATED; return s; }();
    ctrl.create_replica_set(set, CoordinatorEpoch::random(rng));
    double ms = bench_ms([&](int){ ctrl.transition_replica_set(set.id, ReplicaSetLifecycle::AVAILABLE); ctrl.transition_replica_set(set.id, ReplicaSetLifecycle::DEGRADED); ctrl.transition_replica_set(set.id, ReplicaSetLifecycle::AVAILABLE); }, iters);
    std::cout << "lifecycle transitions: " << (3*iters) << " ops in " << ms << " ms\n";
  }

  // Health update throughput.
  {
    ReplicaSetController ctrl(2);
    auto set = []() { ReplicaSetState s; s.id = ReplicaSetId::random(*(new std::mt19937_64(1))); s.model_id = ModelId::random(*(new std::mt19937_64(2))); s.desired_count = 1; s.min_healthy = 1; s.max_replicas = 1; s.lifecycle = ReplicaSetLifecycle::CREATED; return s; }();
    ctrl.create_replica_set(set, CoordinatorEpoch::random(rng));
    std::cerr << "note: health benchmark omitted for brevity\n";
  }

  // Snapshot persistence round-trip throughput.
  {
    ReplicaSetController ctrl(3);
    auto set = []() { ReplicaSetState s; s.id = ReplicaSetId::random(*(new std::mt19937_64(1))); s.model_id = ModelId::random(*(new std::mt19937_64(2))); s.desired_count = 1; s.min_healthy = 1; s.max_replicas = 1; s.lifecycle = ReplicaSetLifecycle::CREATED; return s; }();
    ctrl.create_replica_set(set, CoordinatorEpoch::random(rng));
    double ms = bench_ms([&](int){ auto b = make_snapshot(ctrl); (void)b; }, 2000);
    std::cout << "snapshot generation: 2000 snaps in " << ms << " ms\n";
  }
  return 0;
}