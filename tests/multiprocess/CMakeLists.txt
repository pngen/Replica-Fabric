add_executable(rf_multiprocess test_multiprocess.cpp)
target_link_libraries(rf_multiprocess PRIVATE replicafabric ws2_32)
rf_configure_target(rf_multiprocess)
add_test(NAME multiprocess
  COMMAND rf_multiprocess
          "$<TARGET_FILE_DIR:rf_coordinator>/rf_coordinator.exe"
          "$<TARGET_FILE_DIR:rf_worker>/rf_worker.exe")
