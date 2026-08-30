#include <replicafabric/core/identity_kinds.hpp>
#include <replicafabric/core/replica_set_controller.hpp>
#include <iostream>

int main() {
  using namespace replicafabric;
  ReplicaSetController ctrl(1);
  std::cout << "consumer: Replica Fabric linked; ctrl empty=" << ctrl.empty() << "\n";
  std::cout << "consumer: ReplicaId example=" << ReplicaId().str() << "\n";
  return 0;
}
