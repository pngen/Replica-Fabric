#pragma once

// Replica Fabric - all strongly-typed enums and their canonical textual forms.
//
// Every state machine (replica-set lifecycle, replica lifecycle, health,
// readiness, warmth, promotion, draining, failover) is enumerated here so that
// transitions are explicit and persistence validation has a single source of
// truth. Textual forms are the canonical serialized names.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace replicafabric {
namespace detail {

template <typename E, std::size_t N>
constexpr std::string_view enum_to_string_lookup(
    E v, const std::array<std::pair<E, std::string_view>, N>& t) noexcept {
  for (const auto& p : t) {
    if (p.first == v) return p.second;
  }
  return "?";
}

template <typename E, std::size_t N>
constexpr bool enum_from_string_lookup(
    E& out, std::string_view s, const std::array<std::pair<E, std::string_view>, N>& t) noexcept {
  for (const auto& p : t) {
    if (p.second == s) {
      out = p.first;
      return true;
    }
  }
  return false;
}

template <typename E, std::size_t N>
constexpr bool enum_from_int_lookup(
    E& out, int v, const std::array<std::pair<E, std::string_view>, N>& t) noexcept {
  if (v < 0 || v >= static_cast<int>(N)) return false;
  out = t[static_cast<std::size_t>(v)].first;
  return true;
}

}  // namespace detail

#define RF_ENUM_PAIR(E, M) std::pair{ E::M, std::string_view{#M} }

// ---------------------------------------------------------------------------
// Replica set lifecycle. The ReplicaSet is the unit of a logical service,
// its desired/healthy/max counts, and its placement/promotion/policy surface.
// ---------------------------------------------------------------------------
enum class ReplicaSetLifecycle : std::uint8_t {
  CREATED,
  PROVISIONING,
  AVAILABLE,
  DEGRADED,
  DRAINING,
  RETIRED,
  FAILED,
};
inline constexpr auto kReplicaSetLifecycleTable = std::array{
    RF_ENUM_PAIR(ReplicaSetLifecycle, CREATED),
    RF_ENUM_PAIR(ReplicaSetLifecycle, PROVISIONING),
    RF_ENUM_PAIR(ReplicaSetLifecycle, AVAILABLE),
    RF_ENUM_PAIR(ReplicaSetLifecycle, DEGRADED),
    RF_ENUM_PAIR(ReplicaSetLifecycle, DRAINING),
    RF_ENUM_PAIR(ReplicaSetLifecycle, RETIRED),
    RF_ENUM_PAIR(ReplicaSetLifecycle, FAILED),
};
inline constexpr std::string_view to_string(ReplicaSetLifecycle v) noexcept {
  return detail::enum_to_string_lookup(v, kReplicaSetLifecycleTable);
}
inline constexpr bool from_string(ReplicaSetLifecycle& out, std::string_view s) noexcept {
  return detail::enum_from_string_lookup(out, s, kReplicaSetLifecycleTable);
}
inline constexpr bool from_int(ReplicaSetLifecycle& out, int v) noexcept {
  return detail::enum_from_int_lookup(out, v, kReplicaSetLifecycleTable);
}

// ---------------------------------------------------------------------------
// Replica lifecycle. A single live runtime instance derived from an artifact.
// ---------------------------------------------------------------------------
enum class ReplicaLifecycle : std::uint8_t {
  DECLARED,
  ALLOCATING,
  STARTING,
  WARMING,
  READY,
  SERVING,
  DRAINING,
  QUIESCED,
  FAILED,
  RETIRED,
};
inline constexpr auto kReplicaLifecycleTable = std::array{
    RF_ENUM_PAIR(ReplicaLifecycle, DECLARED),
    RF_ENUM_PAIR(ReplicaLifecycle, ALLOCATING),
    RF_ENUM_PAIR(ReplicaLifecycle, STARTING),
    RF_ENUM_PAIR(ReplicaLifecycle, WARMING),
    RF_ENUM_PAIR(ReplicaLifecycle, READY),
    RF_ENUM_PAIR(ReplicaLifecycle, SERVING),
    RF_ENUM_PAIR(ReplicaLifecycle, DRAINING),
    RF_ENUM_PAIR(ReplicaLifecycle, QUIESCED),
    RF_ENUM_PAIR(ReplicaLifecycle, FAILED),
    RF_ENUM_PAIR(ReplicaLifecycle, RETIRED),
};
inline constexpr std::string_view to_string(ReplicaLifecycle v) noexcept {
  return detail::enum_to_string_lookup(v, kReplicaLifecycleTable);
}
inline constexpr bool from_string(ReplicaLifecycle& out, std::string_view s) noexcept {
  return detail::enum_from_string_lookup(out, s, kReplicaLifecycleTable);
}
inline constexpr bool from_int(ReplicaLifecycle& out, int v) noexcept {
  return detail::enum_from_int_lookup(out, v, kReplicaLifecycleTable);
}

// ---------------------------------------------------------------------------
// Health state. Health measures whether the replica is actually working.
// ---------------------------------------------------------------------------
enum class HealthState : std::uint8_t {
  UNKNOWN,
  STARTING,
  HEALTHY,
  DEGRADED,
  UNHEALTHY,
  FAILED,
  QUARANTINED,
};
inline constexpr auto kHealthStateTable = std::array{
    RF_ENUM_PAIR(HealthState, UNKNOWN),
    RF_ENUM_PAIR(HealthState, STARTING),
    RF_ENUM_PAIR(HealthState, HEALTHY),
    RF_ENUM_PAIR(HealthState, DEGRADED),
    RF_ENUM_PAIR(HealthState, UNHEALTHY),
    RF_ENUM_PAIR(HealthState, FAILED),
    RF_ENUM_PAIR(HealthState, QUARANTINED),
};
inline constexpr std::string_view to_string(HealthState v) noexcept {
  return detail::enum_to_string_lookup(v, kHealthStateTable);
}
inline constexpr bool from_string(HealthState& out, std::string_view s) noexcept {
  return detail::enum_from_string_lookup(out, s, kHealthStateTable);
}
inline constexpr bool from_int(HealthState& out, int v) noexcept {
  return detail::enum_from_int_lookup(out, v, kHealthStateTable);
}

// Evidence kind classifies how a health observation was obtained.
enum class HealthEvidenceKind : std::uint8_t {
  UNKNOWN,
  MEASURED,
  REPORTED,
  DERIVED,
  HEURISTIC,
};
inline constexpr auto kHealthEvidenceKindTable = std::array{
    RF_ENUM_PAIR(HealthEvidenceKind, UNKNOWN),
    RF_ENUM_PAIR(HealthEvidenceKind, MEASURED),
    RF_ENUM_PAIR(HealthEvidenceKind, REPORTED),
    RF_ENUM_PAIR(HealthEvidenceKind, DERIVED),
    RF_ENUM_PAIR(HealthEvidenceKind, HEURISTIC),
};
inline constexpr std::string_view to_string(HealthEvidenceKind v) noexcept {
  return detail::enum_to_string_lookup(v, kHealthEvidenceKindTable);
}
inline constexpr bool from_string(HealthEvidenceKind& out, std::string_view s) noexcept {
  return detail::enum_from_string_lookup(out, s, kHealthEvidenceKindTable);
}
inline constexpr bool from_int(HealthEvidenceKind& out, int v) noexcept {
  return detail::enum_from_int_lookup(out, v, kHealthEvidenceKindTable);
}

// Warmth state tracks whether a replica has loaded/absorbed its workload.
enum class WarmthState : std::uint8_t {
  COLD,
  WARMING,
  WARM,
  STALE_WARMTH,
  INVALIDATED,
};
inline constexpr auto kWarmthStateTable = std::array{
    RF_ENUM_PAIR(WarmthState, COLD),
    RF_ENUM_PAIR(WarmthState, WARMING),
    RF_ENUM_PAIR(WarmthState, WARM),
    RF_ENUM_PAIR(WarmthState, STALE_WARMTH),
    RF_ENUM_PAIR(WarmthState, INVALIDATED),
};
inline constexpr std::string_view to_string(WarmthState v) noexcept {
  return detail::enum_to_string_lookup(v, kWarmthStateTable);
}
inline constexpr bool from_string(WarmthState& out, std::string_view s) noexcept {
  return detail::enum_from_string_lookup(out, s, kWarmthStateTable);
}
inline constexpr bool from_int(WarmthState& out, int v) noexcept {
  return detail::enum_from_int_lookup(out, v, kWarmthStateTable);
}

// Readiness state. Health and readiness are separate; a replica can be healthy
// but not ready to absorb work.
enum class ReadinessState : std::uint8_t {
  UNKNOWN,
  NOT_READY,
  READY,
};
inline constexpr auto kReadinessStateTable = std::array{
    RF_ENUM_PAIR(ReadinessState, UNKNOWN),
    RF_ENUM_PAIR(ReadinessState, NOT_READY),
    RF_ENUM_PAIR(ReadinessState, READY),
};
inline constexpr std::string_view to_string(ReadinessState v) noexcept {
  return detail::enum_to_string_lookup(v, kReadinessStateTable);
}
inline constexpr bool from_string(ReadinessState& out, std::string_view s) noexcept {
  return detail::enum_from_string_lookup(out, s, kReadinessStateTable);
}
inline constexpr bool from_int(ReadinessState& out, int v) noexcept {
  return detail::enum_from_int_lookup(out, v, kReadinessStateTable);
}

// Promotion state records a replica's role within an authority generation.
enum class PromotionState : std::uint8_t {
  NOT_PROMOTED,
  STANDBY,
  CANARY,
  REPLACEMENT,
  RECOVERED,
  PRIMARY,
};
inline constexpr auto kPromotionStateTable = std::array{
    RF_ENUM_PAIR(PromotionState, NOT_PROMOTED),
    RF_ENUM_PAIR(PromotionState, STANDBY),
    RF_ENUM_PAIR(PromotionState, CANARY),
    RF_ENUM_PAIR(PromotionState, REPLACEMENT),
    RF_ENUM_PAIR(PromotionState, RECOVERED),
    RF_ENUM_PAIR(PromotionState, PRIMARY),
};
inline constexpr std::string_view to_string(PromotionState v) noexcept {
  return detail::enum_to_string_lookup(v, kPromotionStateTable);
}
inline constexpr bool from_string(PromotionState& out, std::string_view s) noexcept {
  return detail::enum_from_string_lookup(out, s, kPromotionStateTable);
}
inline constexpr bool from_int(PromotionState& out, int v) noexcept {
  return detail::enum_from_int_lookup(out, v, kPromotionStateTable);
}

// Replica role within a set (independent of promotion history).
enum class ReplicaRole : std::uint8_t {
  NONE,
  PRIMARY,
  SECONDARY,
  STANDBY,
  CANARY,
};
inline constexpr auto kReplicaRoleTable = std::array{
    RF_ENUM_PAIR(ReplicaRole, NONE),
    RF_ENUM_PAIR(ReplicaRole, PRIMARY),
    RF_ENUM_PAIR(ReplicaRole, SECONDARY),
    RF_ENUM_PAIR(ReplicaRole, STANDBY),
    RF_ENUM_PAIR(ReplicaRole, CANARY),
};
inline constexpr std::string_view to_string(ReplicaRole v) noexcept {
  return detail::enum_to_string_lookup(v, kReplicaRoleTable);
}
inline constexpr bool from_string(ReplicaRole& out, std::string_view s) noexcept {
  return detail::enum_from_string_lookup(out, s, kReplicaRoleTable);
}
inline constexpr bool from_int(ReplicaRole& out, int v) noexcept {
  return detail::enum_from_int_lookup(out, v, kReplicaRoleTable);
}

// Drain state is first-class and deterministic.
enum class DrainState : std::uint8_t {
  NOT_DRAINING,
  DRAINING,
  QUIESCING,
  QUIESCED,
  COMPLETE,
  CANCELLED,
};
inline constexpr auto kDrainStateTable = std::array{
    RF_ENUM_PAIR(DrainState, NOT_DRAINING),
    RF_ENUM_PAIR(DrainState, DRAINING),
    RF_ENUM_PAIR(DrainState, QUIESCING),
    RF_ENUM_PAIR(DrainState, QUIESCED),
    RF_ENUM_PAIR(DrainState, COMPLETE),
    RF_ENUM_PAIR(DrainState, CANCELLED),
};
inline constexpr std::string_view to_string(DrainState v) noexcept {
  return detail::enum_to_string_lookup(v, kDrainStateTable);
}
inline constexpr bool from_string(DrainState& out, std::string_view s) noexcept {
  return detail::enum_from_string_lookup(out, s, kDrainStateTable);
}
inline constexpr bool from_int(DrainState& out, int v) noexcept {
  return detail::enum_from_int_lookup(out, v, kDrainStateTable);
}

// Failover state.
enum class FailoverState : std::uint8_t {
  NONE,
  TRIGGERED,
  SELECTING,
  PROMOTING,
  COMPLETE,
  FAILED,
};
inline constexpr auto kFailoverStateTable = std::array{
    RF_ENUM_PAIR(FailoverState, NONE),
    RF_ENUM_PAIR(FailoverState, TRIGGERED),
    RF_ENUM_PAIR(FailoverState, SELECTING),
    RF_ENUM_PAIR(FailoverState, PROMOTING),
    RF_ENUM_PAIR(FailoverState, COMPLETE),
    RF_ENUM_PAIR(FailoverState, FAILED),
};
inline constexpr std::string_view to_string(FailoverState v) noexcept {
  return detail::enum_to_string_lookup(v, kFailoverStateTable);
}
inline constexpr bool from_string(FailoverState& out, std::string_view s) noexcept {
  return detail::enum_from_string_lookup(out, s, kFailoverStateTable);
}
inline constexpr bool from_int(FailoverState& out, int v) noexcept {
  return detail::enum_from_int_lookup(out, v, kFailoverStateTable);
}

// Outcome of a piece of work; ambiguous outcomes are surfaced explicitly.
enum class WorkOutcome : std::uint8_t {
  UNKNOWN,
  SUCCEEDED,
  FAILED,
  AMBIGUOUS,
};
inline constexpr auto kWorkOutcomeTable = std::array{
    RF_ENUM_PAIR(WorkOutcome, UNKNOWN),
    RF_ENUM_PAIR(WorkOutcome, SUCCEEDED),
    RF_ENUM_PAIR(WorkOutcome, FAILED),
    RF_ENUM_PAIR(WorkOutcome, AMBIGUOUS),
};
inline constexpr std::string_view to_string(WorkOutcome v) noexcept {
  return detail::enum_to_string_lookup(v, kWorkOutcomeTable);
}
inline constexpr bool from_string(WorkOutcome& out, std::string_view s) noexcept {
  return detail::enum_from_string_lookup(out, s, kWorkOutcomeTable);
}
inline constexpr bool from_int(WorkOutcome& out, int v) noexcept {
  return detail::enum_from_int_lookup(out, v, kWorkOutcomeTable);
}

// Memory residency state.
enum class MemoryResidencyState : std::uint8_t {
  UNKNOWN,
  NOT_RESIDENT,
  RESIDENT,
  EVICTED,
};
inline constexpr auto kMemoryResidencyStateTable = std::array{
    RF_ENUM_PAIR(MemoryResidencyState, UNKNOWN),
    RF_ENUM_PAIR(MemoryResidencyState, NOT_RESIDENT),
    RF_ENUM_PAIR(MemoryResidencyState, RESIDENT),
    RF_ENUM_PAIR(MemoryResidencyState, EVICTED),
};
inline constexpr std::string_view to_string(MemoryResidencyState v) noexcept {
  return detail::enum_to_string_lookup(v, kMemoryResidencyStateTable);
}
inline constexpr bool from_string(MemoryResidencyState& out, std::string_view s) noexcept {
  return detail::enum_from_string_lookup(out, s, kMemoryResidencyStateTable);
}
inline constexpr bool from_int(MemoryResidencyState& out, int v) noexcept {
  return detail::enum_from_int_lookup(out, v, kMemoryResidencyStateTable);
}

// Placement state.
enum class PlacementState : std::uint8_t {
  PENDING,
  ASSIGNED,
  FAILED,
  INVALIDATED,
};
inline constexpr auto kPlacementStateTable = std::array{
    RF_ENUM_PAIR(PlacementState, PENDING),
    RF_ENUM_PAIR(PlacementState, ASSIGNED),
    RF_ENUM_PAIR(PlacementState, FAILED),
    RF_ENUM_PAIR(PlacementState, INVALIDATED),
};
inline constexpr std::string_view to_string(PlacementState v) noexcept {
  return detail::enum_to_string_lookup(v, kPlacementStateTable);
}
inline constexpr bool from_string(PlacementState& out, std::string_view s) noexcept {
  return detail::enum_from_string_lookup(out, s, kPlacementStateTable);
}
inline constexpr bool from_int(PlacementState& out, int v) noexcept {
  return detail::enum_from_int_lookup(out, v, kPlacementStateTable);
}

// Accelerator kind.
enum class AcceleratorKind : std::uint8_t {
  NONE,
  CPU,
  CUDA,
  ROCM,
  SYCL,
  CUSTOM,
};
inline constexpr auto kAcceleratorKindTable = std::array{
    RF_ENUM_PAIR(AcceleratorKind, NONE),
    RF_ENUM_PAIR(AcceleratorKind, CPU),
    RF_ENUM_PAIR(AcceleratorKind, CUDA),
    RF_ENUM_PAIR(AcceleratorKind, ROCM),
    RF_ENUM_PAIR(AcceleratorKind, SYCL),
    RF_ENUM_PAIR(AcceleratorKind, CUSTOM),
};
inline constexpr std::string_view to_string(AcceleratorKind v) noexcept {
  return detail::enum_to_string_lookup(v, kAcceleratorKindTable);
}
inline constexpr bool from_string(AcceleratorKind& out, std::string_view s) noexcept {
  return detail::enum_from_string_lookup(out, s, kAcceleratorKindTable);
}
inline constexpr bool from_int(AcceleratorKind& out, int v) noexcept {
  return detail::enum_from_int_lookup(out, v, kAcceleratorKindTable);
}

// Backend / runtime kind.
enum class BackendKind : std::uint8_t {
  NONE,
  TRITON,
  TENSORRT,
  ONNX,
  VLLM,
  CUSTOM,
};
inline constexpr auto kBackendKindTable = std::array{
    RF_ENUM_PAIR(BackendKind, NONE),
    RF_ENUM_PAIR(BackendKind, TRITON),
    RF_ENUM_PAIR(BackendKind, TENSORRT),
    RF_ENUM_PAIR(BackendKind, ONNX),
    RF_ENUM_PAIR(BackendKind, VLLM),
    RF_ENUM_PAIR(BackendKind, CUSTOM),
};
inline constexpr std::string_view to_string(BackendKind v) noexcept {
  return detail::enum_to_string_lookup(v, kBackendKindTable);
}
inline constexpr bool from_string(BackendKind& out, std::string_view s) noexcept {
  return detail::enum_from_string_lookup(out, s, kBackendKindTable);
}
inline constexpr bool from_int(BackendKind& out, int v) noexcept {
  return detail::enum_from_int_lookup(out, v, kBackendKindTable);
}

// Numeric mode for compatibility typing.
enum class NumericMode : std::uint8_t {
  UNKNOWN,
  FP32,
  FP16,
  BF16,
  FP8,
  INT8,
  MIXED,
};
inline constexpr auto kNumericModeTable = std::array{
    RF_ENUM_PAIR(NumericMode, UNKNOWN),
    RF_ENUM_PAIR(NumericMode, FP32),
    RF_ENUM_PAIR(NumericMode, FP16),
    RF_ENUM_PAIR(NumericMode, BF16),
    RF_ENUM_PAIR(NumericMode, FP8),
    RF_ENUM_PAIR(NumericMode, INT8),
    RF_ENUM_PAIR(NumericMode, MIXED),
};
inline constexpr std::string_view to_string(NumericMode v) noexcept {
  return detail::enum_to_string_lookup(v, kNumericModeTable);
}
inline constexpr bool from_string(NumericMode& out, std::string_view s) noexcept {
  return detail::enum_from_string_lookup(out, s, kNumericModeTable);
}
inline constexpr bool from_int(NumericMode& out, int v) noexcept {
  return detail::enum_from_int_lookup(out, v, kNumericModeTable);
}

}  // namespace replicafabric
