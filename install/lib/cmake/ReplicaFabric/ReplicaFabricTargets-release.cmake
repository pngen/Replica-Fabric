#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "replicafabric::replicafabric" for configuration "Release"
set_property(TARGET replicafabric::replicafabric APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(replicafabric::replicafabric PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/replicafabric.lib"
  )

list(APPEND _cmake_import_check_targets replicafabric::replicafabric )
list(APPEND _cmake_import_check_files_for_replicafabric::replicafabric "${_IMPORT_PREFIX}/lib/replicafabric.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
