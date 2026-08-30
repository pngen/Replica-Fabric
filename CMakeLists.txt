# Replica Fabric 1.0.0
# A production-grade, vendor-neutral C++20 runtime for governing replicated AI
# model and runtime state across heterogeneous infrastructure.
#
# Systems boundary:
#   * Model Cache            -> what reusable model artifact exists
#   * Inference Scheduler    -> what work should run where
#   * Replica Fabric         -> which live replica is current, healthy, ready,
#                               authoritative, and allowed to serve that work

cmake_minimum_required(VERSION 3.24)

project(ReplicaFabric
  VERSION 1.0.0
  DESCRIPTION "Vendor-neutral C++20 runtime governing replicated AI runtime state"
  HOMEPAGE_URL "https://github.com/summon-software-labs/replica-fabric"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  set(RF_COMPILE_WARNINGS /W4 /WX)
else()
  set(RF_COMPILE_WARNINGS -Wall -Wextra -Wpedantic -Werror)
endif()

option(RF_ENABLE_CUDA "Enable the CUDA-backed replica path" ON)
option(RF_BUILD_CLI "Build the CLI" ON)
option(RF_BUILD_TESTS "Build tests" ON)
option(RF_BUILD_EXAMPLES "Build examples" ON)
option(RF_BUILD_BENCHMARKS "Build benchmarks" ON)
option(RF_INSTALL "Generate install rules" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

file(GLOB_RECURSE RF_LIB_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(replicafabric STATIC ${RF_LIB_SOURCES})
add_library(replicafabric::replicafabric ALIAS replicafabric)

target_include_directories(replicafabric PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>")
target_compile_features(replicafabric PUBLIC cxx_std_20)
target_compile_options(replicafabric PRIVATE ${RF_COMPILE_WARNINGS})
target_compile_definitions(replicafabric PUBLIC REPLICAFABRIC_VERSION="${PROJECT_VERSION}")

set_target_properties(replicafabric PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION 1
  POSITION_INDEPENDENT_CODE ON)

if(MSVC)
  set_target_properties(replicafabric PROPERTIES
    DEBUG_POSTFIX "d"
    RELEASE_POSTFIX "")
endif()

if(RF_ENABLE_CUDA)
  find_package(CUDAToolkit QUIET)
  if(CUDAToolkit_FOUND)
    set(RF_CUDA_FOUND ON)
    message(STATUS "Replica Fabric: CUDA toolkit ${CUDAToolkit_VERSION} found")
    enable_language(CUDA)
    file(GLOB_RECURSE RF_CUDA_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/cuda/*.cu")
    if(RF_CUDA_SOURCES)
      add_library(replicafabric_cuda STATIC ${RF_CUDA_SOURCES})
      target_include_directories(replicafabric_cuda PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")
      set_target_properties(replicafabric_cuda PROPERTIES
        CUDA_ARCHITECTURES 120
        CUDA_STANDARD 17)
      target_compile_options(replicafabric_cuda PRIVATE ${RF_COMPILE_WARNINGS})
      target_link_libraries(replicafabric_cuda PUBLIC CUDA::cudart)
      target_compile_definitions(replicafabric_cuda PUBLIC REPLICAFABRIC_HAS_CUDA=1)
      target_link_libraries(replicafabric PUBLIC replicafabric_cuda CUDA::cudart)
      target_compile_definitions(replicafabric PUBLIC REPLICAFABRIC_HAS_CUDA=1)
    endif()
  else()
    message(STATUS "Replica Fabric: CUDA toolkit not found; building without the CUDA-backed path")
  endif()
endif()

function(rf_configure_target tgt)
  target_compile_options(${tgt} PRIVATE ${RF_COMPILE_WARNINGS})
  if(MSVC)
    target_compile_definitions(${tgt} PRIVATE _CRT_SECURE_NO_WARNINGS=1)
  endif()
endfunction()

if(RF_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(RF_BUILD_CLI)
  add_subdirectory(apps/cli)
endif()

if(RF_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(RF_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

if(RF_INSTALL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  install(TARGETS replicafabric
    EXPORT ReplicaFabricTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  if(RF_CUDA_FOUND AND TARGET replicafabric_cuda)
    install(TARGETS replicafabric_cuda
      EXPORT ReplicaFabricTargets
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif()

  install(EXPORT ReplicaFabricTargets
    FILE ReplicaFabricTargets.cmake
    NAMESPACE replicafabric::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ReplicaFabric)

  configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ReplicaFabricConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ReplicaFabricConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ReplicaFabric)

  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ReplicaFabricConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/ReplicaFabricConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ReplicaFabricConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ReplicaFabric)
endif()
