#pragma once

// Replica Fabric - typed capabilities and resource inventory.

#include <replicafabric/core/enums.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace replicafabric {

// Compute capability (e.g. CUDA SM major.minor). Ordered for compatibility
// comparisons.
struct ComputeCapability {
  int major = 0;
  int minor = 0;

  friend bool operator==(const ComputeCapability& a, const ComputeCapability& b) noexcept {
    return a.major == b.major && a.minor == b.minor;
  }
  friend bool operator!=(const ComputeCapability& a, const ComputeCapability& b) noexcept {
    return !(a == b);
  }
  friend bool operator<(const ComputeCapability& a, const ComputeCapability& b) noexcept {
    if (a.major != b.major) return a.major < b.major;
    return a.minor < b.minor;
  }
  friend bool operator<=(const ComputeCapability& a, const ComputeCapability& b) noexcept {
    return !(b < a);
  }
};

inline std::string compute_capability_str(const ComputeCapability& c) {
  return std::to_string(c.major) + "." + std::to_string(c.minor);
}

// A backend/runtime identity plus the compute capability it provides.
struct BackendCapability {
  BackendKind kind = BackendKind::NONE;
  std::string name;  // e.g. "triton", "tensorrt-9", "onnxruntime"
  ComputeCapability compute;
  NumericMode numeric = NumericMode::UNKNOWN;

  friend bool operator==(const BackendCapability& a, const BackendCapability& b) noexcept {
    return a.kind == b.kind && a.name == b.name && a.compute == b.compute && a.numeric == b.numeric;
  }
};

// An accelerator device visible to a worker.
struct DeviceCapability {
  AcceleratorKind kind = AcceleratorKind::NONE;
  std::string device_id;      // e.g. "cuda:0", "cpu:0", "rocm:1"
  ComputeCapability compute;  // effective compute capability for the device
  std::uint64_t memory_bytes = 0;

  friend bool operator==(const DeviceCapability& a, const DeviceCapability& b) noexcept {
    return a.kind == b.kind && a.device_id == b.device_id && a.compute == b.compute &&
           a.memory_bytes == b.memory_bytes;
  }
};

// A worker's inventory of available resources.
struct ResourceInventory {
  std::uint64_t total_memory_bytes = 0;
  std::uint64_t free_memory_bytes = 0;
  int accelerator_count = 0;
  std::vector<DeviceCapability> devices;
  std::vector<BackendCapability> backends;

  friend bool operator==(const ResourceInventory& a, const ResourceInventory& b) noexcept {
    return a.total_memory_bytes == b.total_memory_bytes &&
           a.free_memory_bytes == b.free_memory_bytes && a.accelerator_count == b.accelerator_count &&
           a.devices == b.devices && a.backends == b.backends;
  }
};

}  // namespace replicafabric
