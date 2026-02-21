cmake_minimum_required(VERSION 3.15)

project(animus LANGUAGES CXX)

add_executable(animus
  src/main.cpp
)

target_compile_features(animus PRIVATE cxx_std_20)

# Keep outputs predictable across generators
set_target_properties(animus PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/bin"
)
