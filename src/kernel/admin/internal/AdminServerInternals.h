#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <drogon/drogon.h>
#include <drogon/WebSocketConnection.h>
#include <json/json.h>

#include "animus_kernel/AdminServer.h"
#include "animus_kernel/Session.h"
#include "animus_kernel/admin/ObservationStreamTypes.h"
#include "animus_kernel/tools/ToolTypes.h"

namespace animus::kernel::adminserver_internal {

extern std::once_flag g_handlersRegistered;
extern std::atomic<AdminServer*> g_activeAdminServer;

bool ParseJsonPayload(const std::string& payload, Json::Value* out, std::string* error);
std::string MaskSecret(const std::string& value);
bool ShouldServeAdminUiFromFilesystem();
std::string DetectAdminUiMimeType(std::string_view relativePath);
drogon::HttpResponsePtr BuildEmbeddedAdminUiResponse(std::string_view relativePath, bool cacheable);
Json::Value BuildObservationStreamEvent(const KernelConfig::MemoryObservation& observation);
bool ObservationMatchesFilter(
    const KernelConfig::MemoryObservation& observation,
    const ObservationStreamFilter& filter);
bool IsSensitiveFieldName(const std::string& key);
Json::Value MaskInterfaceConfig(const Json::Value& input);
Json::Value BuildInterfaceJson(const InterfaceState& iface, bool includeMaskedConfig);
bool ParsePrefixedUInt64(const std::string& text, const std::string& prefix, std::uint64_t* out);
std::string JsonToCompactString(const Json::Value& value);
std::vector<std::filesystem::path> CandidateAdminUiDistDirectories();
bool IsSafeAdminAssetPath(const std::string& relativePath);
bool ResolveAdminUiFilePath(const std::string& relativePath, std::filesystem::path* resolvedPath);
bool ParseSessionId(const std::string& text, SessionId* out);
Json::Value BuildSessionSummaryJson(const Session& session);
Json::Value BuildSessionTurnJson(const SessionTurn& turn);
std::uint32_t ParsePaginationParam(
    const drogon::HttpRequestPtr& req,
    const char* name,
    std::uint32_t defaultValue,
    std::uint32_t minValue,
    std::uint32_t maxValue);
std::uint64_t NowUnixMs();
bool IsValidModelToken(const std::string& token);
Json::Value BuildAgentJson(const KernelConfig::AgentRuntimeConfig& config, std::uint64_t reinitCount);
Json::Value BuildModelJson(const KernelConfig::AgentRuntimeConfig& config, std::uint64_t reinitCount);
void AddBadRequestError(const std::string& message, Json::Value* out);
Json::Value BuildAgentEntityJson(const Agent& a);

// Small response helpers to reduce repetitive JSON response boilerplate.
void SendJsonResponse(
    const std::function<void(const drogon::HttpResponsePtr&)>& callback,
    const Json::Value& body,
    drogon::HttpStatusCode code = drogon::k200OK);
void SendJsonError(
    const std::function<void(const drogon::HttpResponsePtr&)>& callback,
    const std::string& message,
    drogon::HttpStatusCode code);
void SendServerNotActive(
    const std::function<void(const drogon::HttpResponsePtr&)>& callback);

std::string UrlEncode(const std::string& input);

} // namespace animus::kernel::adminserver_internal
