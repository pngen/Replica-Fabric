add_executable(replicafabric-cli rf_cli.cpp)
target_link_libraries(replicafabric-cli PRIVATE replicafabric)
rf_configure_target(replicafabric-cli)
