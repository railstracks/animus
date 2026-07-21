#include "animus_kernel/AdminServer.h"
#include "animus_kernel/ChainRunner.h"
#include "animus_kernel/CompactionService.h"
#include "animus_kernel/SessionManager.h"
#include "animus_kernel/scheduler/Scheduler.h"
#include "animus_kernel/tools/ToolRegistry.h"
#include "animus_kernel/OntologyStore.h"
#include "animus_kernel/MemoryFileStore.h"
#include "animus_kernel/MemorySearch.h"
#include "animus_kernel/llm/LLMProviderBase.h"
#include "kernel/admin/internal/AdminServerInternals.h"
#include "kernel/admin/AdminUiResources.h"
#include "kernel/admin/TemplateResources.h"
#include "animus_kernel/admin/AiDiaryFormat.h"
#include "animus_kernel/GallivantingStore.h"
#include "animus_kernel/lua/ScriptStore.h"
#include "animus_kernel/lua/FilesystemScriptLoader.h"
#include "animus_kernel/lua/LuaState.h"
#include "animus_kernel/tools/ChannelsTool.h"
#include "animus_kernel/tools/WebSearchTool.h"
#include "animus_kernel/SessionNotesStore.h"
#include "animus_kernel/SessionReportStore.h"
#include "animus_kernel/tools/HttpClient.h"
#include "animus_kernel/tools/WebSearchTool.h"
#include "animus_kernel/SessionTagsStore.h"

#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <array>
#include <regex>
#include <sstream>

#include <drogon/drogon.h>

namespace animus::kernel {
using namespace adminserver_internal;

void AdminServer::RegisterRoutesProvidersAndSpa() {
#include "kernel/admin/internal/AdminServerRoutesProvidersAndSpa.inc"
}

void AdminServer::RegisterRoutesAgentsAndRuntime() {
#include "kernel/admin/internal/AdminServerRoutesAgentsAndRuntime.inc"
}

void AdminServer::RegisterRoutesInterfacesSessionsMemory() {
#include "kernel/admin/internal/AdminServerRoutesInterfacesSessionsMemory.inc"
}

void AdminServer::RegisterRoutesDiary() {
#include "kernel/admin/internal/AdminServerRoutesDiary.inc"
}

void AdminServer::RegisterRoutesGallivanting() {
#include "kernel/admin/internal/AdminServerRoutesGallivanting.inc"
}

void AdminServer::RegisterRoutesLua() {
#include "kernel/admin/internal/AdminServerRoutesLua.inc"
}

void AdminServer::RegisterRoutesChannels() {
#include "kernel/admin/internal/AdminServerRoutesChannels.inc"
}

void AdminServer::RegisterRoutesNodes() {
#include "kernel/admin/internal/AdminServerRoutesNodes.inc"
}

void AdminServer::RegisterRoutesSearch() {
#include "kernel/admin/internal/AdminServerRoutesSearch.inc"
}

void AdminServer::RegisterRoutesPromptLogs() {
#include "kernel/admin/internal/AdminServerRoutesPromptLogs.inc"
}

void AdminServer::RegisterRoutesAuth() {
#include "kernel/admin/internal/AdminServerRoutesAuth.inc"
}

void AdminServer::RegisterRoutesDiffusion() {
#include "kernel/admin/internal/AdminServerRoutesDiffusion.inc"
}

void AdminServer::RegisterRoutesSops() {
#include "kernel/admin/internal/AdminServerRoutesSops.inc"
}

} // namespace animus::kernel
