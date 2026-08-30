#pragma once

// Replica Fabric - typed compatibility. A replica is created under a typed
// compatibility contract with its replica-set requirements; a replica created
// under incompatible state must never be promoted. Compatibility is evaluated
// across model identity, revision, tokenizer/vocabulary, adapter set, backend,
// architecture, compute capability, numeric mode, artifact generation,
// kernel/runtime ABI, and the policy fingerprint.

#include <replicafabric/core/capability.hpp>
#include <replicafabric/core/identity_kinds.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace replicafabric {

// The immutable requirements a replica set imposes on its replicas.
struct CompatibilityRequirements {
  ModelId model_id;
  std::string model_revision;  // e.g. "sha256:abc..."
  std::string tokenizer_vocab; // vocabulary fingerprint
  std::vector<std::string> required_adapters;
  BackendKind backend = BackendKind::NONE;
  std::string runtime_name;      // backend runtime identity, e.g. "triton-3.1"
  std::string architecture;      // model architecture name
  ComputeCapability min_compute; // minimum compute capability
  NumericMode numeric_mode = NumericMode::UNKNOWN;
  ArtifactGeneration artifact_generation;  // generation of the artifact the set must run
  std::string kernel_abi;                  // kernel/runtime ABI version when applicable
  std::string policy_fingerprint;          // policy hash that must match

  friend bool operator==(const CompatibilityRequirements& a, const CompatibilityRequirements& b) {
    return a.model_id == b.model_id && a.model_revision == b.model_revision &&
           a.tokenizer_vocab == b.tokenizer_vocab && a.required_adapters == b.required_adapters &&
           a.backend == b.backend && a.runtime_name == b.runtime_name &&
           a.architecture == b.architecture && a.min_compute == b.min_compute &&
           a.numeric_mode == b.numeric_mode &&
           a.artifact_generation == b.artifact_generation && a.kernel_abi == b.kernel_abi &&
           a.policy_fingerprint == b.policy_fingerprint;
  }
};

// What a replica actually declares about the artifact/backend state it hosts.
struct ReplicaCompatibility {
  ModelId model_id;
  std::string model_revision;
  std::string tokenizer_vocab;
  std::vector<std::string> adapters;
  BackendKind backend = BackendKind::NONE;
  std::string runtime_name;
  std::string architecture;
  ComputeCapability compute;
  NumericMode numeric_mode = NumericMode::UNKNOWN;
  ArtifactGeneration artifact_generation;
  std::string kernel_abi;
  std::string policy_fingerprint;

  friend bool operator==(const ReplicaCompatibility& a, const ReplicaCompatibility& b) {
    return a.model_id == b.model_id && a.model_revision == b.model_revision &&
           a.tokenizer_vocab == b.tokenizer_vocab && a.adapters == b.adapters &&
           a.backend == b.backend && a.runtime_name == b.runtime_name &&
           a.architecture == b.architecture && a.compute == b.compute &&
           a.numeric_mode == b.numeric_mode &&
           a.artifact_generation == b.artifact_generation && a.kernel_abi == b.kernel_abi &&
           a.policy_fingerprint == b.policy_fingerprint;
  }
};

// Reasons a replica may be incompatible. Issues are reported as a set; an empty
// set means compatible.
enum class CompatibilityIssue : std::uint8_t {
  NONE,
  MODEL_MISMATCH,
  REVISION_MISMATCH,
  TOKENIZER_MISMATCH,
  ADAPTER_MISSING,
  BACKEND_MISMATCH,
  RUNTIME_MISMATCH,
  ARCHITECTURE_MISMATCH,
  COMPUTE_TOO_LOW,
  NUMERIC_MISMATCH,
  ARTIFACT_GENERATION_MISMATCH,
  KERNEL_ABI_MISMATCH,
  POLICY_FINGERPRINT_MISMATCH,
};

inline std::string_view compatibility_issue_str(CompatibilityIssue c) {
  switch (c) {
    case CompatibilityIssue::NONE: return "NONE";
    case CompatibilityIssue::MODEL_MISMATCH: return "MODEL_MISMATCH";
    case CompatibilityIssue::REVISION_MISMATCH: return "REVISION_MISMATCH";
    case CompatibilityIssue::TOKENIZER_MISMATCH: return "TOKENIZER_MISMATCH";
    case CompatibilityIssue::ADAPTER_MISSING: return "ADAPTER_MISSING";
    case CompatibilityIssue::BACKEND_MISMATCH: return "BACKEND_MISMATCH";
    case CompatibilityIssue::RUNTIME_MISMATCH: return "RUNTIME_MISMATCH";
    case CompatibilityIssue::ARCHITECTURE_MISMATCH: return "ARCHITECTURE_MISMATCH";
    case CompatibilityIssue::COMPUTE_TOO_LOW: return "COMPUTE_TOO_LOW";
    case CompatibilityIssue::NUMERIC_MISMATCH: return "NUMERIC_MISMATCH";
    case CompatibilityIssue::ARTIFACT_GENERATION_MISMATCH: return "ARTIFACT_GENERATION_MISMATCH";
    case CompatibilityIssue::KERNEL_ABI_MISMATCH: return "KERNEL_ABI_MISMATCH";
    case CompatibilityIssue::POLICY_FINGERPRINT_MISMATCH: return "POLICY_FINGERPRINT_MISMATCH";
  }
  return "?";
}

struct CompatibilityResult {
  bool compatible = true;
  std::vector<CompatibilityIssue> issues;

  static CompatibilityResult ok() { return CompatibilityResult{true, {}}; }
};

// Deterministic typed compatibility evaluation. Returns reasons, never a single
// opaque score.
inline CompatibilityResult check_compatibility(const CompatibilityRequirements& req,
                                                const ReplicaCompatibility& got) {
  CompatibilityResult res;
  res.compatible = true;
  if (got.model_id != req.model_id) {
    res.compatible = false;
    res.issues.push_back(CompatibilityIssue::MODEL_MISMATCH);
  }
  if (got.model_revision != req.model_revision) {
    res.compatible = false;
    res.issues.push_back(CompatibilityIssue::REVISION_MISMATCH);
  }
  if (got.tokenizer_vocab != req.tokenizer_vocab) {
    res.compatible = false;
    res.issues.push_back(CompatibilityIssue::TOKENIZER_MISMATCH);
  }
  for (const auto& a : req.required_adapters) {
    bool present = false;
    for (const auto& g : got.adapters) {
      if (g == a) {
        present = true;
        break;
      }
    }
    if (!present) {
      res.compatible = false;
      res.issues.push_back(CompatibilityIssue::ADAPTER_MISSING);
    }
  }
  if (got.backend != req.backend) {
    res.compatible = false;
    res.issues.push_back(CompatibilityIssue::BACKEND_MISMATCH);
  }
  if (got.runtime_name != req.runtime_name) {
    res.compatible = false;
    res.issues.push_back(CompatibilityIssue::RUNTIME_MISMATCH);
  }
  if (got.architecture != req.architecture) {
    res.compatible = false;
    res.issues.push_back(CompatibilityIssue::ARCHITECTURE_MISMATCH);
  }
  if (got.compute < req.min_compute) {
    res.compatible = false;
    res.issues.push_back(CompatibilityIssue::COMPUTE_TOO_LOW);
  }
  if (got.numeric_mode != req.numeric_mode) {
    res.compatible = false;
    res.issues.push_back(CompatibilityIssue::NUMERIC_MISMATCH);
  }
  if (got.artifact_generation != req.artifact_generation) {
    res.compatible = false;
    res.issues.push_back(CompatibilityIssue::ARTIFACT_GENERATION_MISMATCH);
  }
  if (got.kernel_abi != req.kernel_abi) {
    res.compatible = false;
    res.issues.push_back(CompatibilityIssue::KERNEL_ABI_MISMATCH);
  }
  if (got.policy_fingerprint != req.policy_fingerprint) {
    res.compatible = false;
    res.issues.push_back(CompatibilityIssue::POLICY_FINGERPRINT_MISMATCH);
  }
  return res;
}

}  // namespace replicafabric
