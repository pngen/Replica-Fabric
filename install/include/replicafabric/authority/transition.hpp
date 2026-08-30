#pragma once

// Replica Fabric - guarded lifecycle transitions.
//
// Lifecycle transitions are explicit, guarded, and deterministic. Invalid
// transitions fail (throw StateTransitionError). There are no implicit
// transitions and no "auto-heal" that silently moves a replica between states
// without an explicit, validated request.

#include <replicafabric/core/enums.hpp>
#include <replicafabric/core/error.hpp>

#include <string>

namespace replicafabric {

// Replica-set lifecycle allowed transitions.
inline bool can_transition(ReplicaSetLifecycle from, ReplicaSetLifecycle to) {
  switch (from) {
    case ReplicaSetLifecycle::CREATED:
      return to == ReplicaSetLifecycle::PROVISIONING;
    case ReplicaSetLifecycle::PROVISIONING:
      return to == ReplicaSetLifecycle::AVAILABLE ||
             to == ReplicaSetLifecycle::DEGRADED ||
             to == ReplicaSetLifecycle::FAILED ||
             to == ReplicaSetLifecycle::RETIRED;
    case ReplicaSetLifecycle::AVAILABLE:
      return to == ReplicaSetLifecycle::DEGRADED ||
             to == ReplicaSetLifecycle::DRAINING ||
             to == ReplicaSetLifecycle::RETIRED;
    case ReplicaSetLifecycle::DEGRADED:
      return to == ReplicaSetLifecycle::AVAILABLE ||
             to == ReplicaSetLifecycle::DRAINING ||
             to == ReplicaSetLifecycle::RETIRED ||
             to == ReplicaSetLifecycle::FAILED;
    case ReplicaSetLifecycle::DRAINING:
      // A draining set may finish (RETIRED) or be re-armed (AVAILABLE).
      return to == ReplicaSetLifecycle::RETIRED ||
             to == ReplicaSetLifecycle::AVAILABLE;
    case ReplicaSetLifecycle::RETIRED:
      return false;
    case ReplicaSetLifecycle::FAILED:
      return to == ReplicaSetLifecycle::RETIRED;
  }
  return false;
}

// Replica lifecycle allowed transitions.
inline bool can_transition(ReplicaLifecycle from, ReplicaLifecycle to) {
  switch (from) {
    case ReplicaLifecycle::DECLARED:
      return to == ReplicaLifecycle::ALLOCATING ||
             to == ReplicaLifecycle::STARTING ||
             to == ReplicaLifecycle::FAILED ||
             to == ReplicaLifecycle::RETIRED;
    case ReplicaLifecycle::ALLOCATING:
      return to == ReplicaLifecycle::STARTING ||
             to == ReplicaLifecycle::FAILED ||
             to == ReplicaLifecycle::RETIRED;
    case ReplicaLifecycle::STARTING:
      return to == ReplicaLifecycle::WARMING ||
             to == ReplicaLifecycle::READY ||
             to == ReplicaLifecycle::FAILED ||
             to == ReplicaLifecycle::RETIRED;
    case ReplicaLifecycle::WARMING:
      return to == ReplicaLifecycle::READY ||
             to == ReplicaLifecycle::STARTING ||
             to == ReplicaLifecycle::FAILED ||
             to == ReplicaLifecycle::RETIRED;
    case ReplicaLifecycle::READY:
      return to == ReplicaLifecycle::SERVING ||
             to == ReplicaLifecycle::WARMING ||
             to == ReplicaLifecycle::DRAINING ||
             to == ReplicaLifecycle::FAILED ||
             to == ReplicaLifecycle::RETIRED;
    case ReplicaLifecycle::SERVING:
      return to == ReplicaLifecycle::READY ||
             to == ReplicaLifecycle::DRAINING ||
             to == ReplicaLifecycle::FAILED ||
             to == ReplicaLifecycle::RETIRED;
    case ReplicaLifecycle::DRAINING:
      // A drained replica may quiesce, re-arm (explicit READY), fail, or retire.
      // It may NOT silently become SERVING without passing back through READY.
      return to == ReplicaLifecycle::QUIESCED ||
             to == ReplicaLifecycle::READY ||
             to == ReplicaLifecycle::FAILED ||
             to == ReplicaLifecycle::RETIRED;
    case ReplicaLifecycle::QUIESCED:
      return to == ReplicaLifecycle::RETIRED ||
             to == ReplicaLifecycle::READY;
    case ReplicaLifecycle::FAILED:
      return to == ReplicaLifecycle::RETIRED;
    case ReplicaLifecycle::RETIRED:
      return false;
  }
  return false;
}

// Guarded transition: throws StateTransitionError when the move is not allowed.
inline void guard_transition(ReplicaLifecycle from, ReplicaLifecycle to) {
  if (!can_transition(from, to)) {
    throw StateTransitionError("replica lifecycle transition " +
                               std::string(to_string(from)) + " -> " +
                               std::string(to_string(to)) + " is not allowed");
  }
}

inline void guard_transition(ReplicaSetLifecycle from, ReplicaSetLifecycle to) {
  if (!can_transition(from, to)) {
    throw StateTransitionError("replica-set lifecycle transition " +
                               std::string(to_string(from)) + " -> " +
                               std::string(to_string(to)) + " is not allowed");
  }
}

}  // namespace replicafabric
