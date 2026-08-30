#include <replicafabric/cuda/cuda_replica.hpp>

#include <cstddef>
#include <vector>

#include "replicafabric_test.hpp"

#ifdef REPLICAFABRIC_HAS_CUDA
RF_TEST_CASE(cuda_lifecycle_round_trip) {
  using namespace replicafabric;
  CudaDeviceInfo info = cuda_device_info();
  RF_REQUIRE(info.count >= 1);
  RF_CHECK(info.compute_major >= 12);  // Blackwell sm_120

  CudaPathResult start = cuda_replica_start(16u * 1024u * 1024u, 0);
  RF_REQUIRE(start.ok);
  RF_CHECK(start.bytes_allocated > 0);

  CudaPathResult warm = cuda_replica_warm(4u * 1024u * 1024u);
  RF_REQUIRE(warm.ok);

  const std::size_t n = 1u << 18;
  std::vector<float> in(n), out(n);
  for (std::size_t i = 0; i < n; ++i) in[i] = static_cast<float>(i % 53) / 3.0f;
  CudaPathResult exec = cuda_replica_execute(in.data(), out.data(), n);
  RF_REQUIRE(exec.ok);
  RF_CHECK(exec.kernel_checksum == exec.cpu_checksum);

  CudaPathResult teardown = cuda_replica_teardown();
  RF_REQUIRE(teardown.ok);
  RF_CHECK(teardown.error.find("recovered") != std::string::npos);
}
#endif

RF_TEST_MAIN()
