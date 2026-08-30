#pragma once

// Replica Fabric - placement policy and synthetic failure domains.
//
// Placement is not collapsed into one opaque score, and anti-affinity must be
// real. Where physical rack/topology is unavailable, infrastructure is modeled
// with clearly labeled synthetic failure domains. The placement engine exposes
// each component factor and a deterministic tie-break.

#include <cstdint>
#include <string>
#include <vector>

namespace replicafabric {

// A synthetic failure domain label (we do not require physical rack data).
struct SyntheticFailureDomain {
  std::string label;  // e.g. "rack-A", "zone-1", "fd-0"
  std::string kind;   // "rack", "zone", "region", "fd" (generic)

  friend bool operator==(const SyntheticFailureDomain& a, const SyntheticFailureDomain& b) noexcept {
    return a.label == b.label && a.kind == b.kind;
  }
};

// Resource/topology requirements a replica set places on placement.
struct PlacementPolicy {
  // Domains to spread replicas across for anti-affinity. Recognized kinds:
  // "host", "numa", "device", "rack", "zone", "region", or any synthetic
  // domain label from synthetic_domains.
  std::vector<std::string> anti_affinity_domains;

  // Synthetic failure domains advertised by the infrastructure.
  std::vector<SyntheticFailureDomain> synthetic_domains;

  bool require_diversity = true;

  // Optional preferred placement (empty = no preference). Preference never
  // overrides hard anti-affinity or capacity; it only breaks ties deterministically.
  std::string preferred_host;
  std::string preferred_numa;
  std::string preferred_device;
  std::string preferred_failure_domain;

  // Deterministic tie-break ordering: 0 = by id, 1 = by host then device, etc.
  std::uint32_t tie_break_strategy = 0;

  friend bool operator==(const PlacementPolicy& a, const PlacementPolicy& b) noexcept {
    return a.anti_affinity_domains == b.anti_affinity_domains &&
           a.synthetic_domains == b.synthetic_domains &&
           a.require_diversity == b.require_diversity && a.preferred_host == b.preferred_host &&
           a.preferred_numa == b.preferred_numa && a.preferred_device == b.preferred_device &&
           a.preferred_failure_domain == b.preferred_failure_domain &&
           a.tie_break_strategy == b.tie_break_strategy;
  }
};

}  // namespace replicafabric
