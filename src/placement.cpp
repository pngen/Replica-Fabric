#include <replicafabric/placement/placement.hpp>

#include <algorithm>
#include <cmath>
#include <map>

namespace replicafabric {

PlacementEngine::PlacementEngine(std::uint64_t seed) { (void)seed; }

namespace {

double normalize_mem(std::uint64_t free, std::uint64_t total) {
  if (total == 0) return 0.0;
  return static_cast<double>(free) / static_cast<double>(total);
}

bool has_backend(const PlacementHost& h, BackendKind kind) {
  if (kind == BackendKind::NONE) return true;
  for (const auto& b : h.inventory.backends) {
    if (b.kind == kind) return true;
  }
  return false;
}

}  // namespace

PlacementDecision PlacementEngine::place(const ReplicaSetState& set,
                                         const std::vector<PlacementHost>& hosts,
                                         const std::vector<PlacedReplica>& existing) const {
  PlacementDecision d;

  // Build a lookup of existing replicas by domain to evaluate anti-affinity.
  auto occupied = [&](const PlacementHost& h, const std::string& domain) -> bool {
    for (const auto& e : existing) {
      if (domain == "host" && e.host == h.host && !e.host.empty()) return true;
      if (domain == "numa" && e.numa == h.numa && !e.numa.empty()) return true;
      if (domain == "device" && e.device_id == h.device.device_id && !e.device_id.empty()) return true;
      if (domain == "rack") {
        for (const auto& lbl : e.failure_domain_labels) {
          for (const auto& hl : h.failure_domain_labels) {
            if (lbl == hl) return true;
          }
        }
      }
      for (const auto& dom : h.failure_domain_labels) {
        for (const auto& edm : e.failure_domain_labels) {
          if (domain == edm && dom == edm) return true;
        }
      }
    }
    return false;
  };

  // Sort hosts deterministically so candidate ordering is stable.
  std::vector<PlacementHost> sorted = hosts;
  std::sort(sorted.begin(), sorted.end(), [](const PlacementHost& a, const PlacementHost& b) {
    const std::string ka = a.worker_id.str() + "|" + a.device.device_id;
    const std::string kb = b.worker_id.str() + "|" + b.device.device_id;
    return ka < kb;
  });

  for (const auto& host : sorted) {
    PlacementCandidate cand;
    cand.host = host;

    // --- hard constraints --------------------------------------------------
    std::string hard_reject;
    if (!has_backend(host, set.backend)) {
      hard_reject = "missing required backend " + std::string(to_string(set.backend));
    } else if (host.device.compute < set.min_compute) {
      hard_reject = "compute capability too low";
    } else if (host.inventory.free_memory_bytes < set.memory_requirement_bytes) {
      hard_reject = "insufficient free memory";
    } else if (host.inventory.accelerator_count <
               static_cast<int>(set.accelerator_requirement)) {
      hard_reject = "insufficient accelerators";
    }

    if (!hard_reject.empty()) {
      cand.eligible = false;
      cand.rejection = hard_reject;
      d.candidates.push_back(std::move(cand));
      continue;
    }

    // --- weighted component factors ---------------------------------------
    auto add_factor = [&](std::string name, double weight, double score, std::string detail) {
      PlacementFactor f;
      f.name = std::move(name);
      f.weight = weight;
      f.score = score;
      f.contribution = weight * score;
      f.detail = std::move(detail);
      cand.factors.push_back(std::move(f));
    };

    const double mem = normalize_mem(host.inventory.free_memory_bytes,
                                     host.inventory.total_memory_bytes);
    add_factor("free_memory", 3.0, mem,
               std::to_string(host.inventory.free_memory_bytes) + " free bytes");

    const double compute = (host.device.compute.major * 10.0 + host.device.compute.minor) / 200.0;
    add_factor("accelerator_capability", 2.0, std::min(1.0, compute),
               "compute " + std::to_string(host.device.compute.major) + "." +
                   std::to_string(host.device.compute.minor));

    const double warm = host.has_warm_model ? 1.0 : 0.0;
    add_factor("warm_state", 2.5, warm, host.has_warm_model ? "warm model resident" : "cold model");

    // transfer cost / artifact locality (lower distance is better).
    const double locality = host.artifact_distance == 0 ? 1.0 : 1.0 / (1.0 + host.artifact_distance);
    add_factor("artifact_locality", 1.5, locality,
               "artifact distance " + std::to_string(host.artifact_distance));

    // anti-affinity / diversity across the set's domains.
    double diversity = 1.0;
    std::string diversity_detail = "unique across affinity domains";
    for (const auto& dom : set.placement_policy.anti_affinity_domains) {
      if (occupied(host, dom)) {
        diversity = 0.0;
        diversity_detail = "shares " + dom + " domain with an existing replica";
        break;
      }
    }
    add_factor("failure_domain_diversity", 4.0, diversity, diversity_detail);

    // preferred host/numa/device/failure-domain bonus (tie-break helper).
    double pref = 0.0;
    const auto& pol = set.placement_policy;
    if (!pol.preferred_host.empty() && host.host == pol.preferred_host) pref += 0.5;
    if (!pol.preferred_device.empty() && host.device.device_id == pol.preferred_device) pref += 0.5;
    if (!pol.preferred_failure_domain.empty()) {
      for (const auto& lbl : host.failure_domain_labels) {
        if (lbl == pol.preferred_failure_domain) { pref += 0.5; break; }
      }
    }
    add_factor("preference", 1.0, std::min(1.0, pref), "placement preference bonus");

    double total = 0.0;
    for (const auto& f : cand.factors) total += f.contribution;
    cand.total = total;
    cand.eligible = true;
    cand.summary = host.worker_id.str() + "|" + host.device.device_id;
    d.candidates.push_back(std::move(cand));
  }

  // Select the best eligible candidate (deterministic tie-break by summary).
  const PlacementCandidate* best = nullptr;
  for (const auto& c : d.candidates) {
    if (!c.eligible) continue;
    if (best == nullptr || c.total > best->total ||
        (c.total == best->total && c.summary < best->summary)) {
      best = &c;
    }
  }

  if (best == nullptr) {
    d.placed = false;
    d.note = "no eligible placement: all candidates failed hard constraints";
    return d;
  }

  d.placed = true;
  d.chosen = best->host;
  d.note = "chosen host " + best->host.worker_id.str();
  return d;
}

}  // namespace replicafabric
