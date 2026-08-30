#include <replicafabric/cuda/cuda_replica.hpp>

#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace replicafabric {

namespace {
float* g_d_in = nullptr;
float* g_d_out = nullptr;
std::uint64_t g_allocated = 0;
std::uint64_t g_pre_start_free = 0;

__global__ void rf_scale_add(const float* in, float* out, int n) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) out[i] = in[i] * 2.0f + 1.0f;
}

const char* err_str(cudaError_t e) { return cudaGetErrorString(e); }

std::uint64_t free_mem() {
  std::size_t free_b = 0, total_b = 0;
  if (cudaMemGetInfo(&free_b, &total_b) != cudaSuccess) return 0;
  return static_cast<std::uint64_t>(free_b);
}
}  // namespace

CudaDeviceInfo cuda_device_info() {
  CudaDeviceInfo info;
  if (cudaGetDeviceCount(&info.count) != cudaSuccess) return info;
  if (info.count < 1) return info;
  cudaDeviceProp prop{};
  cudaGetDeviceProperties(&prop, 0);
  info.compute_major = prop.major;
  info.compute_minor = prop.minor;
  std::size_t free_b = 0, total_b = 0;
  if (cudaMemGetInfo(&free_b, &total_b) == cudaSuccess) {
    info.free_memory = static_cast<std::uint64_t>(free_b);
    info.total_memory = static_cast<std::uint64_t>(total_b);
  }
  return info;
}

CudaPathResult cuda_replica_start(std::uint64_t max_bytes, int device) {
  CudaPathResult r;
  const cudaError_t de = cudaSetDevice(device);
  if (de != cudaSuccess) { r.error = std::string("cudaSetDevice failed: ") + err_str(de); return r; }
  g_pre_start_free = free_mem();
  std::size_t free_b = 0, total_b = 0;
  if (cudaMemGetInfo(&free_b, &total_b) != cudaSuccess) { r.error = "cudaMemGetInfo failed"; return r; }
  std::uint64_t bytes = std::min<std::uint64_t>(max_bytes, static_cast<std::uint64_t>(free_b) / 2u);
  if (cudaMalloc(&g_d_in, bytes) != cudaSuccess) { r.error = "cudaMalloc d_in failed"; return r; }
  if (cudaMalloc(&g_d_out, bytes) != cudaSuccess) {
    cudaFree(g_d_in); g_d_in = nullptr; r.error = "cudaMalloc d_out failed"; return r;
  }
  g_allocated = bytes;
  if (cudaDeviceSynchronize() != cudaSuccess) { r.error = "device sync failed"; return r; }
  r.ok = true; r.bytes_allocated = bytes; r.error = "cold start ok";
  return r;
}

CudaPathResult cuda_replica_warm(std::uint64_t bytes) {
  CudaPathResult r;
  if (cudaSetDevice(0) != cudaSuccess) { r.error = "device reset needed"; return r; }
  // Bounded warmup: run the kernel on a small buffer to initialize any
  // lazy kernel/graph state.
  const int n = static_cast<int>(bytes / sizeof(float) / 4u);
  float* h = new float[static_cast<std::size_t>(n * 4)];
  for (int i = 0; i < n * 4; ++i) h[i] = static_cast<float>(i % 7);
  float* d = nullptr;
  if (cudaMalloc(&d, static_cast<std::size_t>(n * 4) * sizeof(float)) != cudaSuccess) { delete[] h; r.error = "warm alloc failed"; return r; }
  if (cudaMemcpy(d, h, static_cast<std::size_t>(n * 4) * sizeof(float), cudaMemcpyHostToDevice) != cudaSuccess) { delete[] h; cudaFree(d); r.error = "warm memcpy failed"; return r; }
  float* d2 = nullptr;
  if (cudaMalloc(&d2, static_cast<std::size_t>(n * 4) * sizeof(float)) != cudaSuccess) { delete[] h; cudaFree(d); r.error = "warm out alloc failed"; return r; }
  const int blocks = (n * 4 + 255) / 256;
  rf_scale_add<<<blocks, 256>>>(d, d2, n * 4);
  const cudaError_t k = cudaDeviceSynchronize();
  cudaFree(d); cudaFree(d2); delete[] h;
  if (k != cudaSuccess) { r.error = std::string("warmup kernel failed: ") + err_str(k); return r; }
  r.ok = true; r.bytes_allocated = g_allocated; r.error = "warm ok";
  return r;
}

CudaPathResult cuda_replica_execute(const float* host_in, float* host_out, std::size_t n) {
  CudaPathResult r;
  if (g_d_in == nullptr || g_d_out == nullptr) { r.error = "replica not started"; return r; }
  const std::uint64_t need = static_cast<std::uint64_t>(n) * sizeof(float);
  if (need > g_allocated) { r.error = "buffer too small"; return r; }
  if (cudaMemcpy(g_d_in, host_in, need, cudaMemcpyHostToDevice) != cudaSuccess) { r.error = "H2D failed"; return r; }
  const int blocks = static_cast<int>((n + 255) / 256);
  rf_scale_add<<<blocks, 256>>>(g_d_in, g_d_out, static_cast<int>(n));
  if (cudaDeviceSynchronize() != cudaSuccess) { r.error = "kernel sync failed"; return r; }
  if (cudaMemcpy(host_out, g_d_out, need, cudaMemcpyDeviceToHost) != cudaSuccess) { r.error = "D2H failed"; return r; }
  double kern = 0.0, cpu = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    const float expected = host_in[i] * 2.0f + 1.0f;
    kern += host_out[i];
    cpu += expected;
  }
  if (std::fabs(kern - cpu) > 1e-3 * (static_cast<double>(n))) { r.error = "CPU reference verification failed"; return r; }
  r.ok = true; r.kernel_checksum = kern; r.cpu_checksum = cpu; r.bytes_allocated = g_allocated; r.error = "execute ok";
  return r;
}

CudaPathResult cuda_replica_teardown() {
  CudaPathResult r;
  if (g_d_in) { cudaFree(g_d_in); g_d_in = nullptr; }
  if (g_d_out) { cudaFree(g_d_out); g_d_out = nullptr; }
  if (cudaDeviceSynchronize() != cudaSuccess) { r.error = "teardown sync failed"; return r; }
  if (cudaDeviceReset() != cudaSuccess) { r.error = "device reset failed"; return r; }
  const std::uint64_t now = free_mem();
  const bool recovered = (now >= g_pre_start_free);
  r.ok = recovered;
  r.error = recovered ? "teardown ok; memory recovered" : "teardown: memory not fully recovered";
  return r;
}

}  // namespace replicafabric