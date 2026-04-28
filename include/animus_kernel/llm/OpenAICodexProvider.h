#pragma once

#include "animus_kernel/llm/OpenAIProvider.h"

namespace animus::kernel::llm {

// ============================================================================
// OpenAICodexProvider — extends OpenAI with OAuth token refresh
// ============================================================================

class OpenAICodexProvider : public OpenAIProvider {
public:
  explicit OpenAICodexProvider(const LLMProviderConfig& config);

  std::string ProviderId() const override { return "openai-codex"; }

  // Override to inject token refresh before requests
  LLMMessage Complete(const LLMRequest& request,
                      std::string* error) override;
  LLMMessage StreamComplete(const LLMRequest& request,
                            LLMTokenCallback callback,
                            std::string* error) override;

private:
  static constexpr int64_t REFRESH_THRESHOLD_MS = 300 * 1000; // 5 minutes
  static constexpr const char* TOKEN_ENDPOINT =
      "https://auth.openai.com/oauth/token";
  static constexpr const char* CLIENT_ID =
      "app_EMoamEEZ73f0CkXaXp7hrann";

  std::string m_accessToken;
  std::string m_refreshToken;
  int64_t m_expiresMs{0};
  std::string m_authFilePath;

  /// Check if token is expired or near-expiry, refresh if needed.
  /// Returns true if a valid token is available.
  bool EnsureTokenValid(std::string* error);

  /// Refresh the access token using the stored refresh token.
  bool RefreshToken(std::string* error);

  /// Load tokens from auth file.
  bool LoadTokens(std::string* error);

  /// Persist updated tokens back to auth file.
  void PersistTokens() const;

  /// Override GetHeaders to use the OAuth access token.
  std::vector<std::pair<std::string, std::string>> GetHeaders() const override;
};

} // namespace animus::kernel::llm
