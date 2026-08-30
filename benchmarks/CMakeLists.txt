add_executable(replicafabric-bench rf_bench.cpp)
target_link_libraries(replicafabric-bench PRIVATE replicafabric)
rf_configure_target(replicafabric-bench)
