// Example 4: draining a serving primary, quiescing, and retiring it.
#include <replicafabric/core/replica_set_controller.hpp>
#include <iostream>
#include "example_util.hpp"

int main() {
  using namespace replicafabric;
  ReplicaSetController ctrl(4);
  auto set = rf_example::make_set(1);
  ctrl.create_replica_set(set, CoordinatorEpoch::random(rf_example::rng()));

  auto h = rf_example::provision_ready(ctrl, set.id);
  ctrl.promote_replica(h.id, PromotionState::PRIMARY, h.worker, h.boot, 0, 0);
  const ReplicaState* serving = ctrl.find_replica(h.id);
  std::cout << "serving before drain=" << (serving && serving->serving_eligible ? "yes" : "no") << "\n";

  auto dr = ctrl.drain_replica(h.id, h.worker, h.boot, 0);
  const ReplicaState* draining = ctrl.find_replica(h.id);
  std::cout << "drain: " << (dr.ok() ? "ok" : dr.message)
            << " serving_eligible=" << (draining ? (draining->serving_eligible ? "1" : "0") : "?")
            << " lifecycle=" << (draining ? std::string(to_string(draining->lifecycle)) : "?") << "\n";

  auto cr = ctrl.complete_drain(h.id, 0, false, 0);
  const ReplicaState* quiesced = ctrl.find_replica(h.id);
  std::cout << "complete_drain(quiesce): " << (cr.ok() ? "ok" : cr.message)
            << " lifecycle=" << (quiesced ? std::string(to_string(quiesced->lifecycle)) : "?") << "\n";

  auto rt = ctrl.retire_replica(h.id, 0);
  const ReplicaState* retired = ctrl.find_replica(h.id);
  std::cout << "retire: " << (rt.ok() ? "ok" : rt.message)
            << " lifecycle=" << (retired ? std::string(to_string(retired->lifecycle)) : "?")
            << " serving_eligible=" << (retired ? (retired->serving_eligible ? "1" : "0") : "?") << "\n";

  const bool ok = retired && retired->lifecycle == ReplicaLifecycle::RETIRED && !retired->serving_eligible;
  std::cout << "drain example: " << (ok ? "PASSED" : "FAILED") << "\n";
  return ok ? 0 : 1;
}
