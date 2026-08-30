#pragma once

// Replica Fabric - CUDA-backed replica path (RTX 5090 / Blackwell sm_120).
//
// A replica should perform real initialization: CUDA device selection, context
// initialization, bounded device allocation, host/device transfer, real kernel
// execution, synchronization, CPU-reference verification, and cleanup. This
// module exposes exactly that. It is only meaningful when built with the CUDA
// toolkit (REPLICAFABRIC_HAS_CUDA). Where hardware is unavailable the methods
// report failure; the runtime never fabricates GPU success.

#include <cstdint>
#include <cstddef>
#include <string>

namespace replicafabric {

struct CudaDeviceInfo {
  int count = 0;
  std::uint64_t total_memory = 0;
  std::uint64_t free_memory = 0;
  int compute_major = 0;
  int compute_minor = 0;
};

struct CudaPathResult {
  bool ok = false;
  std::string error;
  double kernel_checksum = 0.0;
  double cpu_checksum = 0.0;
  std::uint64_t bytes_allocated = 0;
};

#ifdef REPLICAFABRIC_HAS_CUDA
CudaDeviceInfo cuda_device_info();
CudaPathResult cuda_replica_start(std::uint64_t max_bytes, int device = 0);
CudaPathResult cuda_replica_warm(std::uint64_t bytes);
CudaPathResult cuda_replica_execute(const float* host_in, float* host_out, std::size_t n);
CudaPathResult cuda_replica_teardown();
#endif

}  // namespace replicafabric
