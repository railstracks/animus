#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Drogon::Drogon" for configuration "Release"
set_property(TARGET Drogon::Drogon APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(Drogon::Drogon PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libdrogon.so.1.9.12"
  IMPORTED_SONAME_RELEASE "libdrogon.so.1"
  )

list(APPEND _cmake_import_check_targets Drogon::Drogon )
list(APPEND _cmake_import_check_files_for_Drogon::Drogon "${_IMPORT_PREFIX}/lib/libdrogon.so.1.9.12" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
