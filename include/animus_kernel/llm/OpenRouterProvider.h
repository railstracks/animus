#pragma once

#include "animus_kernel/llm/LLMProviderBase.h"

namespace animus::kernel::llm {

// ============================================================================
// OpenRouterProvider — Multi-model aggregator via OpenAI-compatible endpoint
// ============================================================================
//
// OpenRouter (https://openrouter.ai) provides a single OpenAI-compatible API
// that routes to 315+ models from OpenAI, Anthropic, Google, Meta, DeepSeek,
// Qwen, Mistral, and many others. One API key, all major model families.
//
// Endpoint: https://openrouter.ai/api/v1/chat/completions
//
// The request/response format is standard OpenAI. Model names use the
// "provider/model" convention (e.g. "anthropic/claude-opus-4.8",
// "deepseek/deepseek-v4-pro", "openai/gpt-5.5").
//
// OpenRouter-specific features available but not yet exposed in v1:
//   - Model fallback routing (models[] + route: "fallback")
//   - Provider preferences (latency, throughput, price)
//   - Cost tracking (usage.cost in response)
//   - BYOK (attach your own provider keys)
//
// Custom headers: HTTP-Referer and X-Title are sent for app identification
// and OpenRouter dashboard ranking.

class OpenRouterProvider : public LLMProviderBase {
public:
  explicit OpenRouterProvider(const LLMProviderConfig& config)
      : LLMProviderBase(config) {}

  std::string ProviderId() const override { return "openrouter"; }

  bool FetchCapabilities(const std::string& modelId) override;

protected:
  std::vector<std::pair<std::string, std::string>> GetHeaders() const override;
  std::string BuildRequestBody(const LLMRequest& request) const override;
  LLMMessage ParseResponse(const std::string& body,
                           std::string* error) const override;
  std::optional<LLMToken> ParseSSELine(
      const std::string& line) const override;
  void ParseToolCallsFromResponse(
      const std::string& body,
      std::vector<LLMToolCall>& tool_calls,
      std::string& finish_reason) const override;
  std::vector<std::string> GetVisionModelIds() const override;
};

} // namespace animus::kernel::llm
