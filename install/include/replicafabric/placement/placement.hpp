#pragma once

// Replica Fabric - placement engine.
//
// Placement never collapses into one opaque score. Every candidate is evaluated
// against typed component factors (accelerator capability, free memory, NUMA
// locality, topology, artifact/model locality, warm state, transfer cost,
// current load, failure-domain diversity, capacity, tenant constraints), each
// with an explicit weight, score, and human-readable detail. Anti-affinity is
// real: replicas are spread across host, NUMA domain, accelerator, and
// rack/failure-domain (or clearly labeled synthetic failure domains). Ties are
// broken deterministically.

#include <replicafabric/core/capability.hpp>
#include <replicafabric/core/identity_kinds.hpp>
#include <replicafabric/model/replica_set.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace replicafabric {

// A host the placement engine can place a replica onto.
struct PlacementHost {
  WorkerId worker_id;
  WorkerBootId boot_id;
  NodeId node_id;
  std::string host;           // "host" anti-affinity domain
  std::string numa;           // "numa" anti-affinity domain
  std::string rack;           // physical rack label (may be empty -> synthetic)
  std::vector<std::string> failure_domain_labels;  // e.g. "rack-A", "zone-1"
  DeviceCapability device;
  ResourceInventory inventory;
  std::uint64_t artifact_distance = 0;   // transfer cost / locality (lower better)
  bool has_warm_model = false;

  friend bool operator==(const PlacementHost& a, const PlacementHost& b) noexcept {
    return a.worker_id == b.worker_id && a.boot_id == b.boot_id && a.node_id == b.node_id &&
           a.host == b.host && a.numa == b.numa && a.rack == b.rack &&
           a.failure_domain_labels == b.failure_domain_labels && a.device == b.device &&
           a.inventory == b.inventory && a.artifact_distance == b.artifact_distance &&
           a.has_warm_model == b.has_warm_model;
  }
};

// A replica already placed somewhere (for anti-affinity / diversity checks).
struct PlacedReplica {
  ReplicaId id;
  std::string host;
  std::string numa;
  std::string device_id;
  std::vector<std::string> failure_domain_labels;
};

struct PlacementFactor {
  std::string name;
  double weight = 0.0;
  double score = 0.0;         // normalized [0,1]
  double contribution = 0.0;  // weight * score
  std::string detail;
};

struct PlacementCandidate {
  PlacementHost host;
  bool eligible = false;
  std::string rejection;                 // if not eligible, why (hard constraint)
  std::vector<PlacementFactor> factors;
  double total = 0.0;                    // sum of contributions
  std::string summary;                   // tie-break key
};

struct PlacementDecision {
  bool placed = false;
  PlacementHost chosen;
  std::vector<PlacementCandidate> candidates;  // every candidate, eligible or not
  std::string note;                            // overall explanation / rejection

  const PlacementCandidate* find_candidate(const WorkerId& w) const {
    for (const auto& c : candidates) {
      if (c.host.worker_id == w) return &c;
    }
    return nullptr;
  }
};

class PlacementEngine {
public:
  explicit PlacementEngine(std::uint64_t seed = 0xabcdef123456ULL);

  PlacementDecision place(const ReplicaSetState& set, const std::vector<PlacementHost>& hosts,
                          const std::vector<PlacedReplica>& existing) const;
};

}  // namespace replicafabric
