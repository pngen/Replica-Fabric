#pragma once

// Replica Fabric - readiness model. Readiness is a distinct gate from health.
// A replica may be HEALTHY but not READY (e.g. still warming, endpoint not
// registered, policy generation not current). Each readiness factor is a typed
// boolean; the aggregate ReadinessState is derived deterministically from the
// factors under the warming policy.

#include <replicafabric/core/enums.hpp>
#include <replicafabric/core/identity_kinds.hpp>
#include <replicafabric/core/time.hpp>

#include <cstdint>
#include <string>

namespace replicafabric {

// Per-factor readiness flags. All true => READY (subject to policy).
struct ReadinessFactors {
  bool model_loaded = false;
  bool artifact_validated = false;
  bool adapters_present = false;
  bool kernel_prepared = false;
  bool graph_prepared = false;
  bool memory_available = false;
  bool device_context_initialized = false;
  bool warmup_complete = false;
  bool dependencies_ready = false;
  bool endpoint_registered = false;
  bool policy_current = false;

  bool all_set() const noexcept {
    return model_loaded && artifact_validated && adapters_present && kernel_prepared &&
           graph_prepared && memory_available && device_context_initialized && warmup_complete &&
           dependencies_ready && endpoint_registered && policy_current;
  }

  friend bool operator==(const ReadinessFactors& a, const ReadinessFactors& b) noexcept {
    return a.model_loaded == b.model_loaded && a.artifact_validated == b.artifact_validated &&
           a.adapters_present == b.adapters_present && a.kernel_prepared == b.kernel_prepared &&
           a.graph_prepared == b.graph_prepared && a.memory_available == b.memory_available &&
           a.device_context_initialized == b.device_context_initialized &&
           a.warmup_complete == b.warmup_complete &&
           a.dependencies_ready == b.dependencies_ready &&
           a.endpoint_registered == b.endpoint_registered && a.policy_current == b.policy_current;
  }
};

struct ReadinessRecord {
  ReadinessState state = ReadinessState::UNKNOWN;
  ReadinessFactors factors;
  MonotonicNs updated_at_mono = 0;
  WallNs updated_at_wall = 0;
  std::string reason;  // why not ready if not ready

  friend bool operator==(const ReadinessRecord& a, const ReadinessRecord& b) noexcept {
    return a.state == b.state && a.factors == b.factors && a.updated_at_mono == b.updated_at_mono &&
           a.updated_at_wall == b.updated_at_wall && a.reason == b.reason;
  }
};

}  // namespace replicafabric
