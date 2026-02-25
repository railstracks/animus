cmake_minimum_required(VERSION 3.15)

project(animus LANGUAGES CXX)

# ============================================================================
# Options
# ============================================================================

option(ANIMUS_BUILD_TESTS "Build test executables" ON)

# ============================================================================
# Compiler Settings
# ============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# SDK (stable module ABI boundary)
# ============================================================================

add_library(animus_sdk INTERFACE)

target_include_directories(animus_sdk INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ============================================================================
# Core Library
# ============================================================================

add_library(animus_core
  src/core/Jobs.cpp
  src/kernel/module/DynamicLibrary.cpp
  src/kernel/module/ModuleLoader.cpp
)

target_include_directories(animus_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Internal kernel-only headers live under src/.
target_include_directories(animus_core PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(animus_core PUBLIC animus_sdk)

# dlopen/dlsym on Linux
if(UNIX AND NOT APPLE)
  target_link_libraries(animus_core PRIVATE dl)
endif()

target_compile_features(animus_core PUBLIC cxx_std_20)

# ============================================================================
# Main Executable
# ============================================================================

add_executable(animus
  src/main.cpp
)

target_link_libraries(animus PRIVATE animus_core)

# Keep outputs predictable across generators
set_target_properties(animus PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/bin"
)

# ============================================================================
# Tests
# ============================================================================

if(ANIMUS_BUILD_TESTS)
    enable_testing()

    add_executable(animus_jobs_tests
      tests/JobsTests.cpp
    )

    target_link_libraries(animus_jobs_tests PRIVATE animus_core)

    set_target_properties(animus_jobs_tests PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/bin"
    )

    add_test(NAME JobsTests COMMAND animus_jobs_tests)
endif()
