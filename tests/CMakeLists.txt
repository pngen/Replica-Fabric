# Replica Fabric test suite.
#
# No timeout machinery exists anywhere in this build: no CTest TIMEOUT
# properties, no watchdogs, no process time limits, no process timeouts.
# Tests run to natural completion, failure, crash, or manual termination after
# diagnosing a genuine hang.

set(RF_TEST_SUPPORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../test_support")

function(rf_add_test_module module)
  add_executable(${module} ${ARGN})
  target_link_libraries(${module} PRIVATE replicafabric)
  target_include_directories(${module} PRIVATE "${RF_TEST_SUPPORT_DIR}")
  rf_configure_target(${module})
  add_test(NAME ${module} COMMAND ${module})
endfunction()

# --- unit ------------------------------------------------------------------
rf_add_test_module(unit_tests
  unit/main.cpp
  unit/test_core.cpp
  unit/test_framework.cpp
  unit/test_controller.cpp
)