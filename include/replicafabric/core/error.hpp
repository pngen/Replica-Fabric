#pragma once

// Replica Fabric - error taxonomy. All exceptions derive from
// ReplicaFabricError so callers can catch one base type. Distinct subclasses
// communicate the failure class (state-machine violation, stale authority,
// validation, persistence, protocol framing, placement).

#include <stdexcept>
#include <string>

namespace replicafabric {

class ReplicaFabricError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class StateTransitionError : public ReplicaFabricError {
public:
  using ReplicaFabricError::ReplicaFabricError;
};

class StaleAuthorityError : public ReplicaFabricError {
public:
  using ReplicaFabricError::ReplicaFabricError;
};

class ValidationError : public ReplicaFabricError {
public:
  using ReplicaFabricError::ReplicaFabricError;
};

class PersistenceError : public ReplicaFabricError {
public:
  using ReplicaFabricError::ReplicaFabricError;
};

class ProtocolError : public ReplicaFabricError {
public:
  using ReplicaFabricError::ReplicaFabricError;
};

class PlacementError : public ReplicaFabricError {
public:
  using ReplicaFabricError::ReplicaFabricError;
};

}  // namespace replicafabric
