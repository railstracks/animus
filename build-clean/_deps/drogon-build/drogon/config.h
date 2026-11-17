#pragma once

#define USE_POSTGRESQL 0
#define LIBPQ_SUPPORTS_BATCH_MODE 0
#define USE_MYSQL 0
#define USE_SQLITE3 0
#define HAS_STD_FILESYSTEM_PATH 1
/* #undef OpenSSL_FOUND */
/* #undef Boost_FOUND */

#define COMPILATION_FLAGS "-O3 -DNDEBUG -std=c++20"
#define COMPILER_COMMAND "c++"
#define COMPILER_ID "GNU"

#define INCLUDING_DIRS " -I/home/claw/.openclaw/workspace/projects/animus/build-clean/_deps/jsoncpp-src/include -I/usr/local/include"
