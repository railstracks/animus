# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/jsoncpp-src"
  "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/jsoncpp-build"
  "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/jsoncpp-subbuild/jsoncpp-populate-prefix"
  "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/jsoncpp-subbuild/jsoncpp-populate-prefix/tmp"
  "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/jsoncpp-subbuild/jsoncpp-populate-prefix/src/jsoncpp-populate-stamp"
  "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/jsoncpp-subbuild/jsoncpp-populate-prefix/src"
  "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/jsoncpp-subbuild/jsoncpp-populate-prefix/src/jsoncpp-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/jsoncpp-subbuild/jsoncpp-populate-prefix/src/jsoncpp-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/jsoncpp-subbuild/jsoncpp-populate-prefix/src/jsoncpp-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
