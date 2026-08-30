#include <replicafabric/cuda/cuda_replica.hpp>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

int main() {
  using namespace replicafabric;
  CudaDeviceInfo info = cuda_device_info();
  std::printf("CUDA device count=%d compute=%d.%d total=%llu free=%llu\n",
              info.count, info.compute_major, info.compute_minor,
              static_cast<unsigned long long>(info.total_memory),
              static_cast<unsigned long long>(info.free_memory));
  if (info.count < 1) { std::printf("CUDA demo: no CUDA device available\n"); return 2; }

  // Cold replica startup.
  const std::uint64_t buf_bytes = 64u * 1024u * 1024u;  // 64 MiB
  CudaPathResult start = cuda_replica_start(buf_bytes, 0);
  std::printf("cold start ok=%d bytes=%llu err=%s\n", start.ok, static_cast<unsigned long long>(start.bytes_allocated), start.error.c_str());
  if (!start.ok) return 1;

  // Warm readiness.
  CudaPathResult warm = cuda_replica_warm(buf_bytes / 4u);
  std::printf("warm ok=%d err=%s\n", warm.ok, warm.error.c_str());
  if (!warm.ok) return 1;

  // Serving execution with CPU reference verification.
  const std::size_t n = 1u << 20;
  std::vector<float> in(n), out(n);
  for (std::size_t i = 0; i < n; ++i) in[i] = static_cast<float>(i % 101) / 7.0f;
  CudaPathResult exec = cuda_replica_execute(in.data(), out.data(), n);
  std::printf("execute ok=%d kernel_checksum=%.3f cpu_checksum=%.3f err=%s\n",
              exec.ok, exec.kernel_checksum, exec.cpu_checksum, exec.error.c_str());
  if (!exec.ok) return 1;
  bool verify = true;
  for (std::size_t i = 0; i < n; i += 4099) {
    const float expected = in[i] * 2.0f + 1.0f;
    if (std::fabs(out[i] - expected) > 1e-4f) { verify = false; break; }
  }
  std::printf("output spot-check=%s\n", verify ? "PASS" : "FAIL");
  if (!verify) return 1;

  // Drain / retirement / teardown with memory recovery verification.
  CudaPathResult teardown = cuda_replica_teardown();
  std::printf("teardown ok=%d err=%s\n", teardown.ok, teardown.error.c_str());
  if (!teardown.ok) return 1;

  std::printf("CUDA DEMO: PASSED\n");
  return 0;
}
