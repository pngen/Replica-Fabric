#pragma once

// Replica Fabric - policies. Each policy is a small typed ruleset that governs
// one slice of a replica set. Separating policies from the state they govern
// makes the runtime explainable: every decision can cite the policy values it
// consulted.

#include <replicafabric/core/enums.hpp>
#include <replicafabric/core/time.hpp>

#include <cstdint>

namespace replicafabric {

// Health policy: how health observations are validated and how staleness is
// defined.
struct HealthPolicy {
  bool enabled = true;
  std::uint64_t expected_interval_ns = 0;  // 0 = no expected cadence enforced
  std::uint64_t stale_after_ns = 5'000'000'000ULL;   // 5s
  std::uint64_t degrade_after_ns = 0;                 // 0 = never degrade
  std::uint32_t unhealthy_after_misses = 3;           // consecutive stale misses
  std::uint32_t quarantine_after_failures = 3;        // consecutive failures
  std::uint32_t min_healthy_observations = 1;         // before promoting to HEALTHY
  double health_threshold = 0.5;                      // min health confidence
  HealthState initial = HealthState::STARTING;

  friend bool operator==(const HealthPolicy& a, const HealthPolicy& b) noexcept {
    return a.enabled == b.enabled && a.expected_interval_ns == b.expected_interval_ns &&
           a.stale_after_ns == b.stale_after_ns && a.degrade_after_ns == b.degrade_after_ns &&
           a.unhealthy_after_misses == b.unhealthy_after_misses &&
           a.quarantine_after_failures == b.quarantine_after_failures &&
           a.min_healthy_observations == b.min_healthy_observations &&
           a.health_threshold == b.health_threshold && a.initial == b.initial;
  }
};

// Warming policy: what it means for a replica to be operationally ready, not
// merely alive.
struct WarmingPolicy {
  bool enabled = true;
  bool require_artifact_loaded = true;
  bool require_weights_resident = true;
  bool require_adapters_active = true;
  bool require_kernel_init = true;
  bool require_graph_init = true;
  bool require_allocator_init = true;
  bool require_device_context = true;
  bool require_bounded_warmup = true;
  bool require_endpoint = true;
  std::uint64_t warmup_work_steps = 0;  // bounded warmup execution steps
  std::uint64_t warmup_timeout_ns = 30'000'000'000ULL;  // 30s (no watchdog; advisory only)

  friend bool operator==(const WarmingPolicy& a, const WarmingPolicy& b) noexcept {
    return a.enabled == b.enabled && a.require_artifact_loaded == b.require_artifact_loaded &&
           a.require_weights_resident == b.require_weights_resident &&
           a.require_adapters_active == b.require_adapters_active &&
           a.require_kernel_init == b.require_kernel_init &&
           a.require_graph_init == b.require_graph_init &&
           a.require_allocator_init == b.require_allocator_init &&
           a.require_device_context == b.require_device_context &&
           a.require_bounded_warmup == b.require_bounded_warmup &&
           a.require_endpoint == b.require_endpoint &&
           a.warmup_work_steps == b.warmup_work_steps &&
           a.warmup_timeout_ns == b.warmup_timeout_ns;
  }
};

// Draining policy: rejection and cancellation are explicit.
struct DrainingPolicy {
  bool reject_new_work = true;
  bool allow_existing_to_finish = true;
  bool allow_force_cancel = false;  // force cancellation only under explicit policy
  std::uint64_t grace_period_ns = 30'000'000'000ULL;  // 30s
  std::uint64_t force_cancel_after_ns = 0;            // 0 = never force

  friend bool operator==(const DrainingPolicy& a, const DrainingPolicy& b) noexcept {
    return a.reject_new_work == b.reject_new_work &&
           a.allow_existing_to_finish == b.allow_existing_to_finish &&
           a.allow_force_cancel == b.allow_force_cancel &&
           a.grace_period_ns == b.grace_period_ns &&
           a.force_cancel_after_ns == b.force_cancel_after_ns;
  }
};

// Promotion policy: the gates a standby/canary/replacement must clear.
struct PromotionPolicy {
  bool require_ready = true;
  bool require_healthy = true;
  HealthState min_health = HealthState::HEALTHY;
  bool require_compatible = true;
  bool require_placement_valid = true;
  bool require_resources = true;
  bool require_authority_current = true;
  bool allow_canary = false;
  bool auto_promote_standby = true;

  friend bool operator==(const PromotionPolicy& a, const PromotionPolicy& b) noexcept {
    return a.require_ready == b.require_ready && a.require_healthy == b.require_healthy &&
           a.min_health == b.min_health && a.require_compatible == b.require_compatible &&
           a.require_placement_valid == b.require_placement_valid &&
           a.require_resources == b.require_resources &&
           a.require_authority_current == b.require_authority_current &&
           a.allow_canary == b.allow_canary && a.auto_promote_standby == b.auto_promote_standby;
  }
};

// Failover policy: how an eligible replacement is selected deterministically.
struct FailoverPolicy {
  bool require_diversity = true;               // prefer spread across failure domains
  bool prefer_standby_first = true;            // standby over canary/replacement
  std::uint32_t max_attempts = 1;              // max selection attempts before giving up
  bool preserve_generation = true;             // keep the same replica-set generation
  bool reject_ambiguous_outcome = false;       // if true, never claim lost work succeeded

  friend bool operator==(const FailoverPolicy& a, const FailoverPolicy& b) noexcept {
    return a.require_diversity == b.require_diversity &&
           a.prefer_standby_first == b.prefer_standby_first && a.max_attempts == b.max_attempts &&
           a.preserve_generation == b.preserve_generation &&
           a.reject_ambiguous_outcome == b.reject_ambiguous_outcome;
  }
};

// The full policy bundle carried by a replica set and versioned as one unit
// (PolicyGeneration).
struct PolicyBundle {
  HealthPolicy health;
  WarmingPolicy warming;
  DrainingPolicy draining;
  PromotionPolicy promotion;
  FailoverPolicy failover;

  friend bool operator==(const PolicyBundle& a, const PolicyBundle& b) noexcept {
    return a.health == b.health && a.warming == b.warming && a.draining == b.draining &&
           a.promotion == b.promotion && a.failover == b.failover;
  }
};

}  // namespace replicafabric
