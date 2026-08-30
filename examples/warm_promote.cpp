// Example 3: warming + readiness + promotion to PRIMARY.
#include <replicafabric/core/replica_set_controller.hpp>
#include <iostream>
#include "example_util.hpp"

int main() {
  using namespace replicafabric;
  ReplicaSetController ctrl(3);
  auto set = rf_example::make_set(2);
  ctrl.create_replica_set(set, CoordinatorEpoch::random(rf_example::rng()));

  auto h = rf_example::provision_ready(ctrl, set.id);
  const ReplicaState* r = ctrl.find_replica(h.id);
  std::cout << "after warm/ready: lifecycle=" << (r ? std::string(to_string(r->lifecycle)) : "?")
            << " readiness=" << (r ? std::string(to_string(r->readiness)) : "?")
            << " health=" << (r ? std::string(to_string(r->health)) : "?" ) << "\n";

  auto pr = ctrl.promote_replica(h.id, PromotionState::PRIMARY, h.worker, h.boot, 0, 0);
  std::cout << "promote: " << (pr.ok() ? "ok" : pr.message) << "\n";

  const ReplicaState* fin = ctrl.find_replica(h.id);
  const bool serving = fin && fin->serving_eligible && ctrl.servable(h.id).eligible;
  std::cout << "serving as PRIMARY=" << (serving ? "yes" : "no")
            << " lifecycle=" << (fin ? std::string(to_string(fin->lifecycle)) : "?")
            << " promotion=" << (fin ? std::string(to_string(fin->promotion)) : "?") << "\n";
  std::cout << "warm/promote example: " << (serving ? "PASSED" : "FAILED") << "\n";
  return serving ? 0 : 1;
}
