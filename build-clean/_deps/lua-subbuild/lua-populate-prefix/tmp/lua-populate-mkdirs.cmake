# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/lua-src"
  "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/lua-build"
  "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/lua-subbuild/lua-populate-prefix"
  "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/lua-subbuild/lua-populate-prefix/tmp"
  "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/lua-subbuild/lua-populate-prefix/src/lua-populate-stamp"
  "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/lua-subbuild/lua-populate-prefix/src"
  "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/lua-subbuild/lua-populate-prefix/src/lua-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/lua-subbuild/lua-populate-prefix/src/lua-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/lua-subbuild/lua-populate-prefix/src/lua-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
