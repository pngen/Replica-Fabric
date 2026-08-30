#include <replicafabric/core/identity.hpp>
#include <replicafabric/core/identity_kinds.hpp>
#include <replicafabric/core/enums.hpp>
#include <replicafabric/core/error.hpp>
#include <replicafabric/core/time.hpp>
#include <replicafabric/core/capability.hpp>
#include <replicafabric/core/compatibility.hpp>
#include <replicafabric/core/policies.hpp>

#include <replicafabric/model/health.hpp>
#include <replicafabric/model/readiness.hpp>
#include <replicafabric/model/warming.hpp>
#include <replicafabric/model/placement_policy.hpp>
#include <replicafabric/model/replica.hpp>
#include <replicafabric/model/replica_set.hpp>

#include <replicafabric/authority/serving_authority.hpp>
#include <replicafabric/authority/transition.hpp>

#include <string>

namespace replicafabric {

// Returns the compile-time library version string.
std::string version_string() { return "1.0.0"; }

// Force instantiation of the serving authority path for the library.
bool rf_serving_authority_smoke(const ReplicaState& r, const ReplicaSetState& set,
                                const ServingAuthorityContext& ctx) {
  return evaluate_serving_authority(r, set, ctx).eligible;
}

}  // namespace replicafabric
