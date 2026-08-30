#pragma once

// Replica Fabric - warming model. Warming is tracked explicitly: cold ->
// warming -> warm -> stale-warmth -> invalidated. A candidate is operationally
// warm only when the warming policy's required steps are complete, which is not
// the same as being alive.

#include <replicafabric/core/enums.hpp>
#include <replicafabric/core/identity_kinds.hpp>
#include <replicafabric/core/time.hpp>

#include <cstdint>
#include <string>

namespace replicafabric {

struct WarmingRecord {
  WarmthState state = WarmthState::COLD;
  std::uint64_t steps_completed = 0;   // bounded warmup execution steps done
  std::uint64_t steps_required = 0;    // bounded warmup steps policy requires
  bool artifact_loading_done = false;
  bool weights_resident = false;
  bool adapters_active = false;
  bool kernel_init_done = false;
  bool graph_init_done = false;
  bool allocator_init_done = false;
  bool device_context_done = false;
  bool warmup_execution_done = false;
  bool endpoint_registered = false;
  MonotonicNs started_warming_mono = 0;
  MonotonicNs became_warm_mono = 0;
  WallNs became_warm_wall = 0;
  std::string message;

  friend bool operator==(const WarmingRecord& a, const WarmingRecord& b) noexcept {
    return a.state == b.state && a.steps_completed == b.steps_completed &&
           a.steps_required == b.steps_required && a.artifact_loading_done == b.artifact_loading_done &&
           a.weights_resident == b.weights_resident && a.adapters_active == b.adapters_active &&
           a.kernel_init_done == b.kernel_init_done && a.graph_init_done == b.graph_init_done &&
           a.allocator_init_done == b.allocator_init_done &&
           a.device_context_done == b.device_context_done &&
           a.warmup_execution_done == b.warmup_execution_done &&
           a.endpoint_registered == b.endpoint_registered &&
           a.started_warming_mono == b.started_warming_mono &&
           a.became_warm_mono == b.became_warm_mono && a.became_warm_wall == b.became_warm_wall &&
           a.message == b.message;
  }
};

}  // namespace replicafabric
