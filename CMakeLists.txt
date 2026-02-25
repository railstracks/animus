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

add_library(animus_kernel
  src/core/Jobs.cpp
  src/kernel/AgentKernel.cpp
  src/kernel/module/DynamicLibrary.cpp
  src/kernel/module/ModuleLoader.cpp
  src/kernel/module/ModuleManager.cpp
  src/kernel/session/Session.cpp
  src/kernel/session/InMemorySessionStore.cpp
  src/kernel/session/DefaultSessionRouter.cpp
  src/kernel/session/SessionManager.cpp
)

target_include_directories(animus_kernel PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Internal kernel-only headers live under src/.
target_include_directories(animus_kernel PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(animus_kernel PUBLIC animus_sdk)

# dlopen/dlsym on Linux
if(UNIX AND NOT APPLE)
  target_link_libraries(animus_kernel PRIVATE dl)
endif()

target_compile_features(animus_kernel PUBLIC cxx_std_20)

# ============================================================================
# Main Executable
# ============================================================================

add_executable(animusd
  src/main.cpp
)

target_link_libraries(animusd PRIVATE animus_kernel)

# Keep outputs predictable across generators
set_target_properties(animusd PROPERTIES
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

    target_link_libraries(animus_jobs_tests PRIVATE animus_kernel)

    set_target_properties(animus_jobs_tests PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/bin"
    )

    add_test(NAME JobsTests COMMAND animus_jobs_tests)

    add_executable(animus_module_loader_tests
      tests/ModuleLoaderTests.cpp
    )

    target_link_libraries(animus_module_loader_tests PRIVATE animus_kernel)

    # Needs internal kernel headers (src/)
    target_include_directories(animus_module_loader_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    set_target_properties(animus_module_loader_tests PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/bin"
    )

    add_test(NAME ModuleLoaderTests COMMAND animus_module_loader_tests)
    set_tests_properties(ModuleLoaderTests PROPERTIES
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    add_executable(animus_session_tests
      tests/SessionTests.cpp
    )

    target_link_libraries(animus_session_tests PRIVATE animus_kernel)

    set_target_properties(animus_session_tests PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/bin"
    )

    add_test(NAME SessionTests COMMAND animus_session_tests)
endif()

# ============================================================================
# Example module (smoke test for the loader)
# ============================================================================

add_library(animus_module_example_hello SHARED
  modules/example/hello/hello_module.cpp
)

target_link_libraries(animus_module_example_hello PRIVATE animus_sdk)

# Expose exports in the module.
target_compile_definitions(animus_module_example_hello PRIVATE ANIMUS_SDK_BUILDING_DLL)

set_target_properties(animus_module_example_hello PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/modules"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/modules"
)
