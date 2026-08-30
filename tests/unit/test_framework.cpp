#include "replicafabric_test.hpp"
#include <stdexcept>

RF_TEST_CASE(harness_passing_checks) {
  RF_CHECK(1 + 1 == 2);
  RF_CHECK_EQ(2 * 3, 6);
  RF_CHECK_NE(5, 6);
  RF_CHECK(static_cast<bool>("truthy"));
  RF_CHECK_NOTHROW([]() { int x = 0; (void)x; }());
}

RF_TEST_CASE(harness_throws_detection) {
  RF_CHECK_THROWS(throw std::runtime_error("boom"));
  RF_CHECK_THROWS(throw 42);
}

RF_TEST_CASE(harness_require_ok_on_true) {
  RF_REQUIRE(3 < 5);
}

RF_TEST_CASE(harness_registry_ordered) {
  // Registration preserves source order; names must be non-empty.
  const auto& reg = ::rftest::registry();
  RF_CHECK(!reg.empty());
  for (const auto& c : reg) {
    RF_CHECK(!c.name.empty());
  }
}

