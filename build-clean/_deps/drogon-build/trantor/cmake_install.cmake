# Install script for directory: /home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "lib" OR NOT CMAKE_INSTALL_COMPONENT)
  foreach(file
      "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libtrantor.so.1.5.26"
      "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libtrantor.so.1"
      )
    if(EXISTS "${file}" AND
       NOT IS_SYMLINK "${file}")
      file(RPATH_CHECK
           FILE "${file}"
           RPATH "/usr/local/lib")
    endif()
  endforeach()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE SHARED_LIBRARY FILES
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-build/trantor/libtrantor.so.1.5.26"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-build/trantor/libtrantor.so.1"
    )
  foreach(file
      "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libtrantor.so.1.5.26"
      "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libtrantor.so.1"
      )
    if(EXISTS "${file}" AND
       NOT IS_SYMLINK "${file}")
      file(RPATH_CHANGE
           FILE "${file}"
           OLD_RPATH "::::::::::::::"
           NEW_RPATH "/usr/local/lib")
      if(CMAKE_INSTALL_DO_STRIP)
        execute_process(COMMAND "/usr/bin/strip" "${file}")
      endif()
    endif()
  endforeach()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "lib" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE SHARED_LIBRARY FILES "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-build/trantor/libtrantor.so")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/trantor" TYPE FILE FILES "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-build/trantor/exports/trantor/exports.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/trantor/net" TYPE FILE FILES
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/net/EventLoop.h"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/net/EventLoopThread.h"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/net/EventLoopThreadPool.h"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/net/InetAddress.h"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/net/TcpClient.h"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/net/TcpConnection.h"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/net/TcpServer.h"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/net/AsyncStream.h"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/net/callbacks.h"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/net/Resolver.h"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/net/Channel.h"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/net/Certificate.h"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/net/TLSPolicy.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/trantor/utils" TYPE FILE FILES
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/utils/AsyncFileLogger.h"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/utils/ConcurrentTaskQueue.h"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/utils/Date.h"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/utils/Funcs.h"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/utils/LockFreeQueue.h"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/utils/LogStream.h"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/utils/Logger.h"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/utils/MsgBuffer.h"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/utils/NonCopyable.h"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/utils/ObjectPool.h"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/utils/SerialTaskQueue.h"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/utils/TaskQueue.h"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/utils/TimingWheel.h"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/trantor/utils/Utilities.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "dev" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/Trantor" TYPE FILE FILES
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-build/trantor/CMakeFiles/TrantorConfig.cmake"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-build/trantor/TrantorConfigVersion.cmake"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/cmake_modules/Findc-ares.cmake"
    "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-src/trantor/cmake_modules/FindBotan.cmake"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "dev" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/Trantor/TrantorTargets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/Trantor/TrantorTargets.cmake"
         "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-build/trantor/CMakeFiles/Export/e2741f8d78b158992c2d7ed6a282eae4/TrantorTargets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/Trantor/TrantorTargets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/Trantor/TrantorTargets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/Trantor" TYPE FILE FILES "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-build/trantor/CMakeFiles/Export/e2741f8d78b158992c2d7ed6a282eae4/TrantorTargets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/Trantor" TYPE FILE FILES "/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/drogon-build/trantor/CMakeFiles/Export/e2741f8d78b158992c2d7ed6a282eae4/TrantorTargets-release.cmake")
  endif()
endif()

