#pragma once

#include "animus_kernel/llm/LLMProviderBase.h"

#include <unordered_map>

namespace animus::kernel::llm {

// ============================================================================
// OpenAICodexProvider — OAuth auth + ChatGPT Codex Responses transport
// ============================================================================

class OpenAICodexProvider : public LLMProviderBase {
public:
  explicit OpenAICodexProvider(const LLMProviderConfig& config);

  std::string ProviderId() const override { return "openai-codex"; }

  // Inject OAuth token refresh before requests.
  LLMMessage Complete(const LLMRequest& request,
                      std::string* error) override;
  LLMMessage StreamComplete(const LLMRequest& request,
                            LLMTokenCallback callback,
                            std::string* error) override;
  bool FetchCapabilities(const std::string& modelId) override;

private:
  struct StreamingToolCall {
    std::string call_id;
    std::string name;
    std::string arguments;
  };

  static constexpr int64_t REFRESH_THRESHOLD_MS = 300 * 1000; // 5 minutes
  static constexpr const char* TOKEN_ENDPOINT =
      "https://auth.openai.com/oauth/token";
  static constexpr const char* CLIENT_ID =
      "app_EMoamEEZ73f0CkXaXp7hrann";
  static constexpr const char* DEFAULT_BASE_URL =
      "https://chatgpt.com/backend-api/codex";

  std::string m_accessToken;
  std::string m_refreshToken;
  int64_t m_expiresMs{0};
  std::string m_authFilePath;
  mutable std::unordered_map<std::string, StreamingToolCall>
      m_streamToolCallsByItemId;
  mutable std::unordered_map<std::string, std::string>
      m_streamItemIdByCallId;
  mutable bool m_streamSawToolCall{false};

  /// Check if token is expired or near-expiry, refresh if needed.
  /// Returns true if a valid token is available.
  bool EnsureTokenValid(std::string* error);

  /// Refresh the access token using the stored refresh token.
  bool RefreshToken(std::string* error);

  /// Load tokens from auth file.
  bool LoadTokens(std::string* error);

  /// Persist updated tokens back to auth file.
  void PersistTokens() const;

  /// Reset per-stream state before parsing a new SSE stream.
  void ResetStreamingState() const;

protected:
  // Responses protocol overrides.
  std::string BuildRequestBody(const LLMRequest& request) const override;
  LLMMessage ParseResponse(const std::string& body,
                           std::string* error) const override;
  std::optional<LLMToken> ParseSSELine(
      const std::string& line) const override;
  bool ProcessSSELine(const std::string& line, std::string& accumulated,
                      LLMTokenCallback& callback) override;
  void ParseToolCallsFromResponse(
      const std::string& body,
      std::vector<LLMToolCall>& tool_calls,
      std::string& finish_reason) const override;
  std::string GetEndpointURL() const override;

private:
  /// Override GetHeaders to use the OAuth access token.
  std::vector<std::pair<std::string, std::string>> GetHeaders() const override;
};

} // namespace animus::kernel::llm
