cmake_minimum_required(VERSION 3.15)

project(animus LANGUAGES C CXX)

# ============================================================================
# Options
# ============================================================================

option(ANIMUS_BUILD_TESTS "Build test executables" ON)
option(ANIMUS_USE_SYSTEM_DROGON "Use a preinstalled Drogon package" OFF)
option(ANIMUS_ADMIN_UI_EMBED "Embed built admin UI assets into animus binary" ON)
option(ANIMUS_ADMIN_UI_BUILD "Build admin UI during CMake build (requires npm)" OFF)
option(ANIMUS_WITH_POSTGRESQL "Enable PostgreSQL backend (requires libpq)" OFF)

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
# Third-party Dependencies
# ============================================================================

if(ANIMUS_USE_SYSTEM_DROGON)
  find_package(Drogon CONFIG REQUIRED)
else()
  include(FetchContent)

  FetchContent_Declare(
    jsoncpp
    GIT_REPOSITORY https://github.com/open-source-parsers/jsoncpp.git
    GIT_TAG 1.9.6
    GIT_SHALLOW TRUE
  )
  set(JSONCPP_WITH_TESTS OFF CACHE BOOL "" FORCE)
  set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF CACHE BOOL "" FORCE)
  set(JSONCPP_WITH_PKGCONFIG_SUPPORT OFF CACHE BOOL "" FORCE)
  set(BUILD_OBJECT_LIBS OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(jsoncpp)

  # Drogon expects these names through FindJsoncpp.cmake.
  set(JSONCPP_INCLUDE_DIRS "${jsoncpp_SOURCE_DIR}/include" CACHE PATH "" FORCE)
  set(JSONCPP_LIBRARIES "jsoncpp_lib" CACHE STRING "" FORCE)

  set(BUILD_CTL OFF CACHE BOOL "" FORCE)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_ORM OFF CACHE BOOL "" FORCE)
  set(BUILD_YAML_CONFIG OFF CACHE BOOL "" FORCE)
  set(USE_SUBMODULE ON CACHE BOOL "" FORCE)

  FetchContent_Declare(
    drogon
    GIT_REPOSITORY https://github.com/drogonframework/drogon.git
    GIT_TAG v1.9.12
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(drogon)

  # Lua 5.4 — embedded scripting runtime
  # Lua uses a manual Makefile-based build, so we compile it directly
  # as part of animus_kernel. FetchContent just downloads the source.
  # We exclude liolib.c (io library blocked in sandbox) but include loslib.c
  # (only os.time/date/difftime exposed via sandbox replacement).
  FetchContent_Declare(
    lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG v5.4.8
    GIT_SHALLOW TRUE
  )
  set(LUA_SRC_DIR "${lua_SOURCE_DIR}" CACHE PATH "Lua source directory" FORCE)
  FetchContent_MakeAvailable(lua)
endif()

# ============================================================================
# llama.cpp — for in-process embedding generation (Ticket 091)
# ============================================================================
option(ANIMUS_WITH_EMBEDDINGS "Enable llama.cpp for in-process embedding generation" ON)

if(ANIMUS_WITH_EMBEDDINGS)
  include(FetchContent)
  set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
  set(GGML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GGML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(LLAMA_CURL OFF CACHE BOOL "" FORCE)
  set(GGML_ACCELERATE OFF CACHE BOOL "" FORCE)
  set(GGML_BLAS OFF CACHE BOOL "" FORCE)
  set(GGML_CUDA OFF CACHE BOOL "" FORCE)
  set(GGML_METAL OFF CACHE BOOL "" FORCE)
  set(GGML_VULKAN OFF CACHE BOOL "" FORCE)
  set(GGML_NATIVE ON CACHE BOOL "" FORCE)
  # Don't force BUILD_SHARED_LIBS globally — it breaks jsoncpp target resolution
  # ggml/llama handle their own build type internally

  FetchContent_Declare(
    llama_cpp
    GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
    GIT_TAG b9715
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(llama_cpp)
  set(ANIMUS_EMBEDDING_LIBS llama ggml)
  set(ANIMUS_HAS_EMBEDDINGS TRUE)
else()
  set(ANIMUS_EMBEDDING_LIBS)
  set(ANIMUS_HAS_EMBEDDINGS FALSE)
endif()

set(ANIMUS_DROGON_TARGET "")
if(TARGET Drogon::Drogon)
  set(ANIMUS_DROGON_TARGET Drogon::Drogon)
elseif(TARGET drogon)
  set(ANIMUS_DROGON_TARGET drogon)
elseif(TARGET drogon::drogon)
  set(ANIMUS_DROGON_TARGET drogon::drogon)
endif()

if(ANIMUS_DROGON_TARGET STREQUAL "")
  message(FATAL_ERROR "Could not resolve Drogon CMake target name.")
endif()

# ============================================================================
# Admin UI Embedding
# ============================================================================

set(ANIMUS_ADMIN_UI_RESOURCE_SOURCE
  "${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/admin/AdminUiResourcesStub.cpp"
)

if(ANIMUS_ADMIN_UI_EMBED)
  set(ANIMUS_ADMIN_UI_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/admin-ui/dist")
  set(ANIMUS_ADMIN_UI_EMBED_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated/AdminUiEmbeddedResources.cpp")

  if(ANIMUS_ADMIN_UI_BUILD)
    find_program(ANIMUS_NPM_EXECUTABLE npm REQUIRED)
    add_custom_command(
      OUTPUT "${ANIMUS_ADMIN_UI_DIST_DIR}/index.html"
      COMMAND "${ANIMUS_NPM_EXECUTABLE}" install
      COMMAND "${ANIMUS_NPM_EXECUTABLE}" run build
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/admin-ui"
      COMMENT "Building admin UI (npm run build)"
      VERBATIM
    )
    set(ANIMUS_ADMIN_UI_BUILD_OUTPUT "${ANIMUS_ADMIN_UI_DIST_DIR}/index.html")
  else()
    set(ANIMUS_ADMIN_UI_BUILD_OUTPUT "")
  endif()

  add_custom_command(
    OUTPUT "${ANIMUS_ADMIN_UI_EMBED_CPP}"
    COMMAND
      ${CMAKE_COMMAND}
      -DINPUT_DIR=${ANIMUS_ADMIN_UI_DIST_DIR}
      -DOUTPUT_CPP=${ANIMUS_ADMIN_UI_EMBED_CPP}
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedAdminUi.cmake"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedAdminUi.cmake"
      "${ANIMUS_ADMIN_UI_BUILD_OUTPUT}"
    COMMENT "Embedding admin UI assets into C++ source"
    VERBATIM
  )

  set(ANIMUS_ADMIN_UI_RESOURCE_SOURCE "${ANIMUS_ADMIN_UI_EMBED_CPP}")
endif()

# ============================================================================
# Template Embedding
# ============================================================================

option(ANIMUS_TEMPLATES_EMBED "Embed template files into animus binary" ON)

set(ANIMUS_TEMPLATES_RESOURCE_SOURCE
  "${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/admin/TemplateResourcesStub.cpp"
)

if(ANIMUS_TEMPLATES_EMBED)
  set(ANIMUS_TEMPLATES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/templates")
  set(ANIMUS_TEMPLATES_EMBED_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated/TemplateEmbeddedResources.cpp")

  add_custom_command(
    OUTPUT "${ANIMUS_TEMPLATES_EMBED_CPP}"
    COMMAND
      ${CMAKE_COMMAND}
      -DINPUT_DIR=${ANIMUS_TEMPLATES_DIR}
      -DOUTPUT_CPP=${ANIMUS_TEMPLATES_EMBED_CPP}
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedTemplates.cmake"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedTemplates.cmake"
    COMMENT "Embedding template files into C++ source"
    VERBATIM
  )

  set(ANIMUS_TEMPLATES_RESOURCE_SOURCE "${ANIMUS_TEMPLATES_EMBED_CPP}")
endif()

# ============================================================================
# Lua 5.4 (compiled directly, not via CMake)
# ============================================================================

if(NOT ANIMUS_USE_SYSTEM_DROGON)
  # Lua sources from FetchContent
  # All Lua sources included. Sandbox is enforced at runtime by LuaState::SetupSandbox()
  # which removes io, os.execute, dofile, loadfile, require, debug, and load.
  set(LUA_SOURCES
    ${lua_SOURCE_DIR}/lapi.c
    ${lua_SOURCE_DIR}/lauxlib.c
    ${lua_SOURCE_DIR}/lbaselib.c
    ${lua_SOURCE_DIR}/lcode.c
    ${lua_SOURCE_DIR}/lcorolib.c
    ${lua_SOURCE_DIR}/lctype.c
    ${lua_SOURCE_DIR}/ldblib.c
    ${lua_SOURCE_DIR}/ldebug.c
    ${lua_SOURCE_DIR}/ldo.c
    ${lua_SOURCE_DIR}/ldump.c
    ${lua_SOURCE_DIR}/lfunc.c
    ${lua_SOURCE_DIR}/lgc.c
    ${lua_SOURCE_DIR}/linit.c
    ${lua_SOURCE_DIR}/liolib.c
    ${lua_SOURCE_DIR}/llex.c
    ${lua_SOURCE_DIR}/lmathlib.c
    ${lua_SOURCE_DIR}/lmem.c
    ${lua_SOURCE_DIR}/loadlib.c
    ${lua_SOURCE_DIR}/lobject.c
    ${lua_SOURCE_DIR}/lopcodes.c
    ${lua_SOURCE_DIR}/loslib.c
    ${lua_SOURCE_DIR}/lparser.c
    ${lua_SOURCE_DIR}/lstate.c
    ${lua_SOURCE_DIR}/lstring.c
    ${lua_SOURCE_DIR}/lstrlib.c
    ${lua_SOURCE_DIR}/ltable.c
    ${lua_SOURCE_DIR}/ltablib.c
    ${lua_SOURCE_DIR}/ltm.c
    ${lua_SOURCE_DIR}/lundump.c
    ${lua_SOURCE_DIR}/lutf8lib.c
    ${lua_SOURCE_DIR}/lvm.c
    ${lua_SOURCE_DIR}/lzio.c
  )

  add_library(lua54 STATIC ${LUA_SOURCES})
  target_include_directories(lua54 PUBLIC ${lua_SOURCE_DIR})
  target_compile_definitions(lua54 PRIVATE LUA_COMPAT_5_3)
  # Lua is C, not C++. Compile as C even though project is C++.
  set_target_properties(lua54 PROPERTIES LINKER_LANGUAGE C)
  target_compile_options(lua54 PRIVATE -w)  # suppress Lua warnings
  set_property(TARGET lua54 PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

# ============================================================================
# Core Library
# ============================================================================

add_library(animus_kernel
  ${ANIMUS_ADMIN_UI_RESOURCE_SOURCE}
  ${ANIMUS_TEMPLATES_RESOURCE_SOURCE}
  src/core/Jobs.cpp
  src/kernel/admin/AdminServer.cpp
  src/kernel/admin/AdminServerRoutes.cpp
  src/kernel/admin/AdminServerWebSockets.cpp
  src/kernel/admin/AgentManager.cpp
  src/kernel/admin/ChatSessionService.cpp
  src/kernel/admin/ConstitutionManager.cpp
  src/kernel/admin/InterfaceManager.cpp
  src/kernel/admin/MemoryManager.cpp
  src/kernel/admin/ObservationStreamHub.cpp
  src/kernel/admin/OAuthManager.cpp
  src/kernel/admin/ProviderManager.cpp
  src/kernel/admin/DiaryManager.cpp
  src/kernel/admin/AiDiaryFormat.cpp
  src/kernel/interfaces/IrcInterface.cpp
  src/kernel/AgentKernel.cpp
  src/kernel/data/SqliteDataStore.cpp
  src/kernel/data/PgDataStore.cpp
  src/kernel/data/SchemaHelpers.cpp
  src/kernel/data/DatabaseConfig.cpp
  src/kernel/memory/MemoryStore.cpp
  src/kernel/memory/MemoryFileStore.cpp
  src/kernel/tools/NodeTool.cpp
  src/kernel/NodeManager.cpp
  src/kernel/NodeDaemon.cpp
  src/kernel/EmbeddingService.cpp
  src/kernel/memory/MemorySearch.cpp
  src/kernel/ontology/OntologyStore.cpp
  src/kernel/agent/AgentStore.cpp
  src/kernel/module/DynamicLibrary.cpp
  src/kernel/module/ModuleLoader.cpp
  src/kernel/module/ModuleManager.cpp
  src/kernel/session/ConversationCompactor.cpp
  src/kernel/session/InMemorySessionStore.cpp
  src/kernel/session/SqliteSessionStore.cpp
  src/kernel/session/SessionNotesStore.cpp
  src/kernel/session/SessionReportStore.cpp
  src/kernel/session/SessionTagsStore.cpp
  src/kernel/session/DefaultSessionRouter.cpp
  src/kernel/session/SessionRoutingRule.cpp
  src/kernel/session/Session.cpp
  src/kernel/session/SessionManager.cpp
  src/kernel/chain/PromptAssembler.cpp
  src/kernel/chain/ChainRunner.cpp
  src/kernel/PromptLogStore.cpp
  src/kernel/auth/AuthStore.cpp
  src/kernel/auth/AuthManager.cpp
  src/kernel/chain/CompactionService.cpp
  src/kernel/chain/CompactionSummaryGenerator.cpp
  src/kernel/tools/ToolRegistry.cpp
  src/kernel/tools/HttpClient.cpp
  src/kernel/tools/FileTool.cpp
  src/kernel/tools/ShellExecTool.cpp
  src/kernel/tools/HttpTool.cpp
  src/kernel/tools/WebFetchTool.cpp
  src/kernel/tools/WebSearchTool.cpp
  src/kernel/tools/ConsolidationTool.cpp
  src/kernel/tools/DiaryTool.cpp
  src/kernel/tools/AgentTool.cpp
  src/kernel/tools/ProjectTool.cpp
  src/kernel/tools/GallivantingTool.cpp
  src/kernel/tools/MemoryTool.cpp
  src/kernel/tools/ScheduleTool.cpp
  src/kernel/tools/SessionsTool.cpp
  src/kernel/tools/CompositeTool.cpp
  src/kernel/tools/ChannelsTool.cpp
  src/kernel/tools/LuaTool.cpp
  src/kernel/tools/DiceTool.cpp
  src/kernel/tools/CalculatorTool.cpp
  src/kernel/tools/ToolsTool.cpp
  src/kernel/tools/ImageTool.cpp
  src/kernel/email/EmailTool.cpp
  src/kernel/scheduler/ScheduleStore.cpp
  src/kernel/scheduler/Scheduler.cpp
  src/kernel/social/SocialEventPoller.cpp
  src/kernel/social/TelegramBotApi.cpp
  src/kernel/ChannelManager.cpp
  src/kernel/MessageQueue.cpp
            src/kernel/DiscordGatewayLoop.cpp
            src/kernel/whatsapp/WABinary.cpp
            src/kernel/whatsapp/WhatsAppCryptoHelpers.cpp
            src/kernel/whatsapp/WhatsAppMessageHandler.cpp
            src/kernel/whatsapp/WhatsAppNoise.cpp
            src/kernel/whatsapp/WhatsAppTransport.cpp
            src/kernel/WhatsAppGatewayLoop.cpp
            src/kernel/signal/SignalTypes.cpp
            src/kernel/signal/InMemorySignalStore.cpp
            src/kernel/signal/SignalCrypto.cpp
            src/kernel/signal/SignalSessionManager.cpp
            src/kernel/signal/DoubleRatchet.cpp
  src/kernel/consolidation/ConsolidationStore.cpp
  src/kernel/gallivanting/GallivantingStore.cpp
  src/kernel/consolidation/ConsolidationPipeline.cpp
  src/kernel/context/ContextProviderRegistry.cpp
  src/kernel/context/IdentityProvider.cpp
  src/kernel/context/SessionNotesProvider.cpp
  src/kernel/context/SessionReportProvider.cpp
  src/kernel/context/ActiveMemoryProvider.cpp
            src/kernel/context/ChannelContextProvider.cpp
  src/kernel/lua/LuaState.cpp
  src/kernel/lua/LuaToolHandler.cpp
  src/kernel/lua/ScriptStore.cpp
  src/kernel/lua/FilesystemScriptLoader.cpp
  src/kernel/AgentConfigStore.cpp
  src/kernel/llm/CohereToolCallAccumulator.cpp
  src/kernel/llm/SSEToolCallAccumulator.cpp
  src/kernel/ProviderThrottle.cpp
  src/kernel/llm/LLMProviderBase.cpp
  src/kernel/llm/LLMProviderRegistry.cpp
  src/kernel/llm/OpenAIProvider.cpp
  src/kernel/llm/OpenAICodexProvider.cpp
  src/kernel/llm/OpenAICompat.cpp
  src/kernel/llm/ZaiProvider.cpp
  src/kernel/llm/AlibabaProvider.cpp
  src/kernel/llm/DeepSeekProvider.cpp
  src/kernel/llm/OpenRouterProvider.cpp
  src/kernel/llm/OllamaProvider.cpp
  src/kernel/llm/CohereProvider.cpp
)

target_include_directories(animus_kernel PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${JSONCPP_INCLUDE_DIRS}
)

target_include_directories(animus_kernel PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor
  ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(animus_kernel PUBLIC animus_sdk jsoncpp_lib)
target_link_libraries(animus_kernel PRIVATE ${ANIMUS_DROGON_TARGET})

if(NOT ANIMUS_USE_SYSTEM_DROGON)
  target_link_libraries(animus_kernel PRIVATE lua54)
endif()

if(UNIX AND NOT APPLE)
  target_link_libraries(animus_kernel PRIVATE dl)
  find_package(CURL REQUIRED)
  find_package(OpenSSL REQUIRED)
  find_package(SQLite3 REQUIRED)
  target_link_libraries(animus_kernel PRIVATE CURL::libcurl SQLite::SQLite3 OpenSSL::Crypto OpenSSL::SSL sodium)
endif()

# Embedding support (optional)
if(ANIMUS_WITH_EMBEDDINGS AND ANIMUS_HAS_EMBEDDINGS)
  target_link_libraries(animus_kernel PRIVATE ${ANIMUS_EMBEDDING_LIBS})
  target_compile_definitions(animus_kernel PRIVATE ANIMUS_HAS_EMBEDDINGS=1)
  # ggml depends on zlib and pthreads
  find_package(ZLIB)
  if(ZLIB_FOUND)
    target_link_libraries(animus_kernel PRIVATE ZLIB::ZLIB)
  else()
    target_link_libraries(animus_kernel PRIVATE z)
  endif()
  find_package(Threads REQUIRED)
  target_link_libraries(animus_kernel PRIVATE Threads::Threads)
endif()

# PostgreSQL backend (optional)
if(ANIMUS_WITH_POSTGRESQL)
  find_package(PostgreSQL REQUIRED)
  target_include_directories(animus_kernel PRIVATE ${PostgreSQL_INCLUDE_DIRS})
  target_link_libraries(animus_kernel PRIVATE ${PostgreSQL_LIBRARIES})
  target_compile_definitions(animus_kernel PRIVATE ANIMUS_WITH_POSTGRESQL)
endif()

target_compile_features(animus_kernel PUBLIC cxx_std_20)

set_target_properties(animus_kernel PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist"
)

# ============================================================================
# Main Executable
# ============================================================================

add_executable(animusd
  src/main.cpp
)

target_link_libraries(animusd PRIVATE animus_kernel)

set_target_properties(animusd PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/bin"
  ENABLE_EXPORTS TRUE
)

target_link_options(animusd PRIVATE -rdynamic)

# ============================================================================
# Migration Tool (SQLite → PostgreSQL)
# ============================================================================

if(ANIMUS_WITH_POSTGRESQL)
    add_executable(animus-migrate
      src/migrate.cpp
    )

    target_link_libraries(animus-migrate PRIVATE animus_kernel)

    set_target_properties(animus-migrate PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/bin"
    )
endif()

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

    add_executable(animus_admin_server_tests
      tests/AdminServerTests.cpp
    )
    target_link_libraries(animus_admin_server_tests PRIVATE animus_kernel)
    set_target_properties(animus_admin_server_tests PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/bin"
    )
    add_test(NAME AdminServerTests COMMAND animus_admin_server_tests)

    add_executable(animus_admin_server_disabled_tests
      tests/AdminServerDisabledTests.cpp
    )
    target_link_libraries(animus_admin_server_disabled_tests PRIVATE animus_kernel)
    set_target_properties(animus_admin_server_disabled_tests PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/bin"
    )
    add_test(NAME AdminServerDisabledTests COMMAND animus_admin_server_disabled_tests)

    add_executable(animus_agent_config_reload_tests
      tests/AgentConfigReloadTests.cpp
    )
    target_link_libraries(animus_agent_config_reload_tests PRIVATE animus_kernel)
    set_target_properties(animus_agent_config_reload_tests PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/bin"
    )
    add_test(NAME AgentConfigReloadTests COMMAND animus_agent_config_reload_tests)

    add_executable(animus_llm_provider_tests
      tests/LLMProviderTests.cpp
    )
    target_link_libraries(animus_llm_provider_tests PRIVATE animus_kernel)
    set_target_properties(animus_llm_provider_tests PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/bin"
    )
    add_test(NAME LLMProviderTests COMMAND animus_llm_provider_tests)

    add_executable(animus_diary_tests
      tests/DiaryManagerTests.cpp
    )
    target_link_libraries(animus_diary_tests PRIVATE animus_kernel jsoncpp_lib)
    target_include_directories(animus_diary_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${JSONCPP_INCLUDE_DIRS}
    )
    set_target_properties(animus_diary_tests PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/bin"
    )
    add_test(NAME DiaryManagerTests COMMAND animus_diary_tests)

    add_executable(animus_scheduler_tests
      tests/SchedulerTests.cpp
    )
    target_link_libraries(animus_scheduler_tests PRIVATE animus_kernel jsoncpp_lib)
    target_include_directories(animus_scheduler_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${JSONCPP_INCLUDE_DIRS}
    )
    set_target_properties(animus_scheduler_tests PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/bin"
    )
    add_test(NAME SchedulerTests COMMAND animus_scheduler_tests)

    add_executable(animus_consolidation_tests
      tests/ConsolidationTests.cpp
    )
    target_link_libraries(animus_consolidation_tests PRIVATE animus_kernel jsoncpp_lib)
    target_include_directories(animus_consolidation_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${JSONCPP_INCLUDE_DIRS}
    )
    set_target_properties(animus_consolidation_tests PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/bin"
    )
    add_test(NAME ConsolidationTests COMMAND animus_consolidation_tests)

    add_executable(animus_ontology_store_tests
      tests/OntologyStoreTests.cpp
    )
    target_link_libraries(animus_ontology_store_tests PRIVATE animus_kernel jsoncpp_lib)
    target_include_directories(animus_ontology_store_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${JSONCPP_INCLUDE_DIRS}
    )
    set_target_properties(animus_ontology_store_tests PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/bin"
    )
    add_test(NAME OntologyStoreTests COMMAND animus_ontology_store_tests)

    add_executable(animus_memory_file_store_tests
      tests/MemoryFileStoreTests.cpp
    )
    target_link_libraries(animus_memory_file_store_tests PRIVATE animus_kernel jsoncpp_lib)
    target_include_directories(animus_memory_file_store_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${JSONCPP_INCLUDE_DIRS}
    )
    set_target_properties(animus_memory_file_store_tests PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/bin"
    )
    add_test(NAME MemoryFileStoreTests COMMAND animus_memory_file_store_tests)

    add_executable(animus_memory_search_tests
      tests/MemorySearchTests.cpp
    )
    target_link_libraries(animus_memory_search_tests PRIVATE animus_kernel jsoncpp_lib)
    target_include_directories(animus_memory_search_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${JSONCPP_INCLUDE_DIRS}
    )
    set_target_properties(animus_memory_search_tests PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/bin"
    )
    add_test(NAME MemorySearchTests COMMAND animus_memory_search_tests)

    add_executable(animus_lua_tests
      tests/LuaTests.cpp
    )
    target_link_libraries(animus_lua_tests PRIVATE animus_kernel jsoncpp_lib)
    target_include_directories(animus_lua_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${JSONCPP_INCLUDE_DIRS}
    )
    set_target_properties(animus_lua_tests PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/bin"
    )
    add_test(NAME LuaTests COMMAND animus_lua_tests)

    add_executable(animus_social_tool_tests
      tests/ChannelsToolTests.cpp
    )
    target_link_libraries(animus_social_tool_tests PRIVATE animus_kernel jsoncpp_lib)
    target_include_directories(animus_social_tool_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${JSONCPP_INCLUDE_DIRS}
    )
    set_target_properties(animus_social_tool_tests PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/bin"
    )
    add_test(NAME ChannelsToolTests COMMAND animus_social_tool_tests)

    add_executable(animus_signal_protocol_tests
      tests/SignalProtocolTests.cpp
    )
    target_link_libraries(animus_signal_protocol_tests PRIVATE animus_kernel)
    target_include_directories(animus_signal_protocol_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    set_target_properties(animus_signal_protocol_tests PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/bin"
    )
    add_test(NAME SignalProtocolTests COMMAND animus_signal_protocol_tests)
endif()

# ============================================================================
# Example module (smoke test for the module loader)
# ============================================================================

add_library(animus_module_example_hello SHARED
  modules/example/hello/hello_module.cpp
)

target_link_libraries(animus_module_example_hello PRIVATE animus_sdk)
target_compile_definitions(animus_module_example_hello PRIVATE ANIMUS_SDK_BUILDING_DLL)

set_target_properties(animus_module_example_hello PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/modules"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/modules"
)
