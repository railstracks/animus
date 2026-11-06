# ============================================================================
# LuaAdapterSyncGuard — scripts/*.lua vs state/lua/_shared/*.lua divergence
# guard (#265)
#
# The runtime loads state/lua/_shared/ (AdminServer m_luaScriptDir) and the
# docker build ships it; feature work lands in scripts/. Nothing else syncs
# the two trees, and drift accumulated silently three times before this
# guard: bluesky 157 lines (#36 facets), mastodon 397 lines (#145 surface
# undeployed — #258), irc 50 lines runtime-ahead (#209/#205 — #265 stage 1).
#
# Fails when ANY pair diverges or EITHER tree holds an unpaired .lua file.
# Run via ctest: ctest -R LuaAdapterSyncGuard --output-on-failure
# ============================================================================

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(SCRIPTS_DIR "${REPO_ROOT}/scripts")
set(SHARED_DIR "${REPO_ROOT}/state/lua/_shared")

set(FAILURES "")

file(GLOB SCRIPTS_LUA "${SCRIPTS_DIR}/*.lua")
file(GLOB SHARED_LUA "${SHARED_DIR}/*.lua")

if(NOT SCRIPTS_LUA AND NOT SHARED_LUA)
  message(FATAL_ERROR "LuaAdapterSyncGuard: no .lua files found — directories moved? scripts/ and state/lua/_shared/ are both empty.")
endif()

# Pair check: every scripts/*.lua must have a _shared/ counterpart.
foreach(script_file ${SCRIPTS_LUA})
  get_filename_component(name "${script_file}" NAME)
  set(shared_file "${SHARED_DIR}/${name}")
  if(NOT EXISTS "${shared_file}")
    list(APPEND FAILURES "unpaired: scripts/${name} has no state/lua/_shared/ counterpart")
    continue()
  endif()
  file(READ "${script_file}" script_content)
  file(READ "${shared_file}" shared_content)
  if(NOT script_content STREQUAL shared_content)
    list(APPEND FAILURES "diverged: scripts/${name} != state/lua/_shared/${name}")
  endif()
endforeach()

# Reverse pair check: every _shared/*.lua must have a scripts/ counterpart.
foreach(shared_file ${SHARED_LUA})
  get_filename_component(name "${shared_file}" NAME)
  set(script_file "${SCRIPTS_DIR}/${name}")
  if(NOT EXISTS "${script_file}")
    list(APPEND FAILURES "unpaired: state/lua/_shared/${name} has no scripts/ counterpart")
  endif()
endforeach()

list(LENGTH FAILURES failure_count)
if(failure_count GREATER 0)
  message(FATAL_ERROR "LuaAdapterSyncGuard: ${failure_count} adapter-tree divergence(s) detected. Feature work belongs in scripts/, fixes must be applied to BOTH trees (or converge before committing):\n  - ${FAILURES}")
endif()

list(LENGTH SCRIPTS_LUA scripts_count)
message(STATUS "LuaAdapterSyncGuard: ${scripts_count} adapter script(s) paired and identical between scripts/ and state/lua/_shared/") 
