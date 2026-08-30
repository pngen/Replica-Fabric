add_executable(rf_coordinator rf_coordinator.cpp)
target_link_libraries(rf_coordinator PRIVATE replicafabric ws2_32)
rf_configure_target(rf_coordinator)

add_executable(rf_worker rf_worker.cpp)
target_link_libraries(rf_worker PRIVATE replicafabric ws2_32)
rf_configure_target(rf_worker)
