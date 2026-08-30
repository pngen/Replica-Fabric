#pragma once

// Replica Fabric - logical time. Two time bases are used:
//   * MonotonicNs - nanoseconds on a monotonic clock. Never advances
//     backwards and is immune to wall-clock jumps. Used for freshness of
//     health/readiness evidence and for ordering within a process.
//   * WallNs - nanoseconds since Unix epoch. Used for auditing/display and for
//     persisted records so recovery can reason about wall-clock scheduling.

#include <chrono>
#include <cstdint>

namespace replicafabric {

using MonotonicNs = std::uint64_t;
using WallNs = std::int64_t;

inline MonotonicNs monotonic_ns() {
  using namespace std::chrono;
  return static_cast<std::uint64_t>(
      duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}

inline WallNs wall_ns() {
  using namespace std::chrono;
  return static_cast<WallNs>(
      duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count());
}

}  // namespace replicafabric
