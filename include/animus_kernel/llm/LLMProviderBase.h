#pragma once

#include <optional>
#include <string>
#include <vector>

#include "animus_kernel/llm/ILLMProvider.h"
#include "animus_kernel/llm/LLMProviderConfig.h"

namespace animus::kernel::llm {

// ============================================================================
// LLMProviderBase — shared HTTP/SSE machinery + template methods
// ============================================================================

/// Base class for all concrete providers.
///
/// Implements Complete() and StreamComplete() using HTTP(S) requests with
/// Server-Sent Events streaming. Concrete providers override template methods
/// to handle their specific request/response formats.
///
/// Template method pattern:
///   Complete()      → DoHTTPRequest(non-streaming) → BuildRequestBody / ParseResponse
///   StreamComplete() → DoHTTPRequest(streaming)    → BuildRequestBody / ParseSSELine
class LLMProviderBase : public ILLMProvider {
public:
  explicit LLMProviderBase(const LLMProviderConfig& config);

  ~LLMProviderBase() override;

  // Disable copy
  LLMProviderBase(const LLMProviderBase&) = delete;
  LLMProviderBase& operator=(const LLMProviderBase&) = delete;

  // ---------------------------------------------------------------------------
  // ILLMProvider implementation
  // ---------------------------------------------------------------------------

  LLMMessage Complete(const LLMRequest& request,
                      std::string* error) override;

  LLMMessage StreamComplete(const LLMRequest& request,
                            LLMTokenCallback callback,
                            std::string* error) override;

  bool IsAvailable() const override;

  // ---------------------------------------------------------------------------
  // SSE processing (public for use by streaming callback, not for external use)
  // ---------------------------------------------------------------------------

  /// Parse a line as SSE data and invoke token callback if content found.
  /// Strips "data: " prefix, handles [DONE], delegates to ParseSSELine().
  /// Returns true if stream should stop ([DONE] received).
  bool ProcessSSELine(const std::string& line, std::string& accumulated,
                      LLMTokenCallback& callback);

  /// Append raw SSE data, parse complete lines, set done=true on [DONE].
  /// Used by the streaming curl write callback.
  void AppendSSEData(const char* data, size_t len, bool* done);

  /// Mark provider as (un)available (e.g. after auth failure).
  void SetAvailable(bool available);

protected:
  // ---------------------------------------------------------------------------
  // Template methods — concrete providers override these
  // ---------------------------------------------------------------------------

  /// Build the JSON request body for this provider.
  virtual std::string BuildRequestBody(const LLMRequest& request) const = 0;

  /// Parse a non-streaming JSON response body into an LLMMessage.
  virtual LLMMessage ParseResponse(const std::string& body,
                                   std::string* error) const = 0;

  /// Parse a single SSE data line into a token.
  /// Return std::nullopt for lines that don't carry content (keep-alive, etc.)
  virtual std::optional<LLMToken> ParseSSELine(
      const std::string& line) const = 0;

  /// Return true if this SSE line signals end-of-stream.
  /// Default: checks for "[DONE]".
  virtual bool IsSSEDone(const std::string& line) const;

  /// Build the full URL for the chat endpoint.
  /// Default: base_url + "/chat/completions"
  virtual std::string GetEndpointURL() const;

  /// Build HTTP headers (auth, content-type, provider-specific).
  virtual std::vector<std::pair<std::string, std::string>> GetHeaders() const;

  // ---------------------------------------------------------------------------
  // Shared HTTP machinery
  // ---------------------------------------------------------------------------

  /// Execute an HTTP POST request.
  ///
  /// For streaming (stream=true):
  ///   - Calls tokenCallback for each parsed SSE token.
  ///   - responseBody is not set.
  ///
  /// For non-streaming (stream=false):
  ///   - Sets responseBody to the full response body.
  ///   - tokenCallback is not called.
  ///
  /// Returns HTTP status code, or 0 on connection error.
  int DoHTTPRequest(const std::string& body,
                    bool stream,
                    LLMTokenCallback tokenCallback,
                    std::string* responseBody,
                    std::string* error);

  /// Access the provider configuration from subclasses.
  const LLMProviderConfig& Config() const { return m_config; }

private:
  LLMProviderConfig m_config;
  bool m_available{true};

  // libcurl handle — pimpl to avoid exposing curl headers
  struct HTTPImpl;
  HTTPImpl* m_http{nullptr};
};

} // namespace animus::kernel::llm
