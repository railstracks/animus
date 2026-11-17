#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Trantor::Trantor" for configuration "Release"
set_property(TARGET Trantor::Trantor APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(Trantor::Trantor PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libtrantor.so.1.5.26"
  IMPORTED_SONAME_RELEASE "libtrantor.so.1"
  )

list(APPEND _cmake_import_check_targets Trantor::Trantor )
list(APPEND _cmake_import_check_files_for_Trantor::Trantor "${_IMPORT_PREFIX}/lib/libtrantor.so.1.5.26" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
