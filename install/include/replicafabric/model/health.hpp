#pragma once

// Replica Fabric - health model. Health and readiness are separate concerns:
// a replica can be healthy but not ready to absorb work. Every health
// transition is an explicit, deterministic decision that can cite its
// evidence. Health evidence is typed by how it was obtained (measured,
// reported, derived, heuristic, unknown) and carries freshness, confidence,
// generation, and the failure/recovery reason.

#include <replicafabric/core/enums.hpp>
#include <replicafabric/core/identity_kinds.hpp>
#include <replicafabric/core/time.hpp>

#include <cstdint>
#include <string>

namespace replicafabric {

// A single piece of health evidence (an observation).
struct HealthEvidence {
  HealthState state = HealthState::UNKNOWN;
  HealthEvidenceKind kind = HealthEvidenceKind::UNKNOWN;
  std::string source;             // who produced it (worker, prober, derived)
  MonotonicNs observed_at_mono = 0;
  WallNs observed_at_wall = 0;
  std::uint64_t freshness_ns = 0; // age of the evidence at the time of use
  double confidence = 0.0;        // [0,1]
  HealthGeneration generation;    // the health generation this evidence belongs to
  std::string message;            // failure/recovery reason text

  friend bool operator==(const HealthEvidence& a, const HealthEvidence& b) noexcept {
    return a.state == b.state && a.kind == b.kind && a.source == b.source &&
           a.observed_at_mono == b.observed_at_mono && a.observed_at_wall == b.observed_at_wall &&
           a.freshness_ns == b.freshness_ns && a.confidence == b.confidence &&
           a.generation == b.generation && a.message == b.message;
  }
};

// The health record the replica set stores (a derived, authoritative health
// state with its provenance).
struct HealthRecord {
  HealthState state = HealthState::UNKNOWN;
  HealthEvidenceKind kind = HealthEvidenceKind::UNKNOWN;
  std::string source;
  MonotonicNs updated_at_mono = 0;
  WallNs updated_at_wall = 0;
  double confidence = 0.0;
  HealthGeneration generation;    // the health generation this record was produced under
  std::string reason;             // last transition reason (failure or recovery)
  std::uint32_t consecutive_stale = 0;      // consecutive stale-cadence misses
  std::uint32_t consecutive_failures = 0;   // consecutive failure observations

  friend bool operator==(const HealthRecord& a, const HealthRecord& b) noexcept {
    return a.state == b.state && a.kind == b.kind && a.source == b.source &&
           a.updated_at_mono == b.updated_at_mono && a.updated_at_wall == b.updated_at_wall &&
           a.confidence == b.confidence && a.generation == b.generation && a.reason == b.reason &&
           a.consecutive_stale == b.consecutive_stale &&
           a.consecutive_failures == b.consecutive_failures;
  }
};

}  // namespace replicafabric
