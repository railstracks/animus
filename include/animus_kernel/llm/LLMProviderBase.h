#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "animus_kernel/llm/ILLMProvider.h"
#include "animus_kernel/llm/LLMProviderConfig.h"
#include "animus_kernel/llm/SSEToolCallAccumulator.h"

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
  // Tool call access after streaming
  // ---------------------------------------------------------------------------

  /// Get tool calls accumulated during the last StreamComplete call.
  /// Returns empty vector if no tool calls were in the response.
  const std::vector<LLMToolCall>& GetLastToolCalls() const { return m_lastToolCalls; }

  /// Get finish reason from the last StreamComplete call.
  std::string GetLastFinishReason() const { return m_lastFinishReason; }

  /// Get prompt token count from the last LLM call (streaming or non-streaming).
  int GetLastPromptTokens() const { return m_lastPromptTokens; }

  /// Get completion token count from the last LLM call.
  int GetLastCompletionTokens() const { return m_lastCompletionTokens; }

  // ---------------------------------------------------------------------------
  // Abort / cancellation
  // ---------------------------------------------------------------------------

  /// Set an abort signal. When set, the next SSE chunk write callback returns 0
  /// (signalling curl to abort the transfer). Checked at most one chunk later.
  void SetAbortSignal(std::shared_ptr<std::atomic<bool>> signal) {
    m_abortSignal = std::move(signal);
  }

  /// True if the last streaming call was aborted by the abort signal.
  bool WasAborted() const { return m_aborted; }

  /// True if the current attempt tripped the degenerate-output guard.
  bool IsDegenerateOutput() const { return m_degenerateOutput; }

  /// Check if abort signal is set — called by StreamWriteCallback.
  bool ShouldAbort() {
    if (m_abortSignal && m_abortSignal->load()) {
      m_aborted = true;
      return true;
    }
    return false;
  }

  // ---------------------------------------------------------------------------
  // Capabilities
  // ---------------------------------------------------------------------------

  /// Query the provider for model capabilities. Populates m_capabilities.
  /// Returns true on success. Providers that don't have a models API
  /// return hardcoded defaults.
  virtual bool FetchCapabilities(const std::string& modelId);

  /// Get the cached capabilities for the current model.
  const ProviderCapabilities& GetCapabilities() const { return m_capabilities; }

  /// Return model IDs that support vision input. Default: returns current
  /// model if supports_vision is set. Providers with a model listing can
  /// override to check all models efficiently.
  virtual std::vector<std::string> GetVisionModelIds() const;

  // ---------------------------------------------------------------------------
  // SSE processing (public for use by streaming callback, not for external use)
  // ---------------------------------------------------------------------------

  /// Parse a line as SSE data and invoke token callback if content found.
  /// Strips "data: " prefix, handles [DONE], delegates to ParseSSELine().
  /// Returns true if stream should stop ([DONE] received).
  virtual bool ProcessSSELine(const std::string& line, std::string& accumulated,
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

  /// Parse tool calls from a complete (non-streaming) response body.
  /// Default: no-op (returns empty). Providers that support tool calling
  /// override this to extract tool_calls and finish_reason from the JSON.
  virtual void ParseToolCallsFromResponse(
      const std::string& body,
      std::vector<LLMToolCall>& tool_calls,
      std::string& finish_reason) const {
    (void)body; (void)tool_calls; (void)finish_reason;
  }

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

  /// Execute an HTTP POST request, retrying transient failures.
  ///
  /// For streaming (stream=true):
  ///   - Calls tokenCallback for each parsed SSE token.
  ///   - responseBody is not set.
  ///   - Retried only when the server delivered zero bytes (nothing was
  ///     forwarded to the callback); partial delivery fails fast to avoid
  ///     duplicate content.
  ///
  /// For non-streaming (stream=false):
  ///   - Sets responseBody to the full response body.
  ///   - tokenCallback is not called.
  ///
  /// Retries transport errors (timeouts, connection resets) and transient
  /// HTTP statuses (408, 429, 5xx). Other 4xx fail fast — retrying an auth
  /// or schema error is pointless. Aborts via stop signal are never retried.
  ///
  /// Retry policy is read from config.extra (per-provider):
  ///   - "retry_max_attempts"  (default "3", incl. first attempt)
  ///   - "retry_interval_ms"   (default "10000")
  ///
  /// Returns HTTP status code, or 0 on connection error (after exhausting
  /// retries). 499 = aborted by stop signal.
  int DoHTTPRequest(const std::string& body,
                    bool stream,
                    LLMTokenCallback tokenCallback,
                    std::string* responseBody,
                    std::string* error);

  /// Execute an HTTP GET request. Sets responseBody to the full response body.
  /// Returns HTTP status code, or 0 on connection error.
  int DoHTTPGet(const std::string& url,
                const std::vector<std::pair<std::string, std::string>>& headers,
                std::string* responseBody,
                std::string* error);

  int DoHTTPPost(const std::string& url,
                 const std::string& body,
                 const std::vector<std::pair<std::string, std::string>>& headers,
                 std::string* responseBody,
                 std::string* error);

  /// Access the provider configuration from subclasses.
  const LLMProviderConfig& Config() const { return m_config; }

  /// Access the SSE tool call accumulator (for provider-specific overrides).
  SSEToolCallAccumulator& ToolCallAccumulator() { return m_toolCallAccumulator; }

private:
  // Single-attempt HTTP POST (no retry). See DoHTTPRequest for the retry wrapper.
  int DoHTTPRequestOnce(const std::string& body,
                        bool stream,
                        LLMTokenCallback tokenCallback,
                        std::string* responseBody,
                        std::string* error);

  // True for HTTP statuses worth retrying (408/429/5xx/529). 0 = transport error
  // is handled by the caller explicitly.
  static bool IsRetryableHTTPStatus(int status);

  // Sleep in interruptible chunks; returns false if the abort signal fired.
  bool SleepInterruptible(int totalMs);

  // True if the string consists solely of "<unk>" repetitions and whitespace.
  static bool IsUnkOnly(const std::string& s);

  // True if a completed (non-streaming) response body is a degenerate flood.
  static bool IsDegenerateBody(const std::string& body);

  LLMProviderConfig m_config;
  bool m_available{true};

  // Accumulated tool calls, finish reason, and token usage from the last call
  std::vector<LLMToolCall> m_lastToolCalls;
  std::string m_lastFinishReason;
  int m_lastPromptTokens{0};
  int m_lastCompletionTokens{0};

  // SSE tool call accumulator — assembles delta fragments across chunks
  SSEToolCallAccumulator m_toolCallAccumulator;

  // Degenerate-output detection: some endpoints (notably Ollama Cloud)
  // occasionally degenerate into endless "<unk>" token floods, usually in
  // the reasoning channel. Aborting turns it into a retryable failure
  // instead of a multi-hour stream the human has to stop manually.
  int m_degenerateRun{0};              // consecutive junk tokens seen
  bool m_degenerateOutput{false};      // threshold exceeded this attempt
  int m_degenerateMaxRun{25};          // extra["degenerate_max_run"]

  // Optional per-request hard timeout (extra["request_timeout_ms"]).
  // 0 = current behavior (non-stream: 4x connect timeout, stream: unlimited).
  int m_requestTimeoutMs{0};

protected:
  // Cached capabilities for the current model
  ProviderCapabilities m_capabilities;

private:
  // libcurl handle — pimpl to avoid exposing curl headers
  struct HTTPImpl;
  HTTPImpl* m_http{nullptr};

  // Abort signal — when set, the streaming write callback aborts the transfer
  std::shared_ptr<std::atomic<bool>> m_abortSignal;
  bool m_aborted{false};
};

} // namespace animus::kernel::llm