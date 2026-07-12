#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include <json/json.h>

#include "animus_kernel/admin/OAuthState.h"

namespace animus::kernel {

class OAuthManager {
public:
    struct OperationResult {
        int httpStatusCode{200};
        Json::Value body{Json::objectValue};
    };

    struct ParsedCallback {
        bool isOpenAIDeviceAuthCallback{false};
        std::string code;
        std::string state;
        std::string redirectUri;
    };

    enum class BrowserFlowLookupStatus {
        Found,
        NotFound,
        Expired
    };

    void StoreBrowserFlow(
        const std::string& providerId,
        const PendingOAuthCodeFlowState& flow,
        std::uint64_t nowUnixMs);

    BrowserFlowLookupStatus GetActiveBrowserFlow(
        const std::string& providerId,
        std::uint64_t nowUnixMs,
        PendingOAuthCodeFlowState* outFlow);

    void ClearBrowserFlow(const std::string& providerId);

    ParsedCallback ParseBrowserCallback(const std::string& callbackUrl) const;
    OperationResult ExchangeBrowserAuthorizationCode(
        const std::string& code,
        const std::string& redirectUri,
        const std::string& codeVerifier) const;
    OperationResult StartOpenAICodexBrowserFlow(
        const std::string& providerId,
        const std::string& redirectUri,
        std::uint64_t nowUnixMs);
    OperationResult CompleteOpenAICodexBrowserFlow(
        const std::string& providerId,
        const std::string& code,
        const std::string& state,
        const std::string& callbackUrl,
        std::uint64_t nowUnixMs);
    OperationResult StartOpenAICodexDeviceFlow(const std::string& providerId) const;
    OperationResult PollOpenAICodexDeviceFlow(
        const std::string& providerId,
        const Json::Value& body) const;
    OperationResult RequestOpenAIDeviceCode() const;
    OperationResult PollOpenAIDeviceCodeAndExchange(
        const std::string& deviceAuthId,
        const std::string& userCode) const;

    // Twitter OAuth 2.0 PKCE browser flow
    OperationResult StartTwitterOAuthFlow(
        const std::string& channelId,
        const std::string& clientId,
        const std::string& redirectUri,
        std::uint64_t nowUnixMs);
    OperationResult CompleteTwitterOAuthFlow(
        const std::string& channelId,
        const std::string& clientId,
        const std::string& clientSecret,
        const std::string& authorizationCode,
        const std::string& state,
        const std::string& redirectUri);

private:
    void PruneExpiredLocked(std::uint64_t nowUnixMs);

    std::unordered_map<std::string, PendingOAuthCodeFlowState> m_pendingOauthCodeFlowsByProviderId;
    std::unordered_map<std::string, PendingOAuthCodeFlowState> m_pendingTwitterFlowsByChannelId;
    mutable std::mutex m_oauthFlowMutex;
};

} // namespace animus::kernel
