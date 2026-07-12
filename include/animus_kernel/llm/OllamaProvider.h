#pragma once

#include "animus_kernel/llm/LLMProviderBase.h"

namespace animus::kernel::llm {

class OllamaProvider : public LLMProviderBase {
public:
  explicit OllamaProvider(const LLMProviderConfig& config)
      : LLMProviderBase(config) {}

  std::string ProviderId() const override { return "ollama"; }

protected:
  std::string BuildRequestBody(const LLMRequest& request) const override;
  LLMMessage ParseResponse(const std::string& body,
                           std::string* error) const override;
  std::optional<LLMToken> ParseSSELine(
      const std::string& line) const override;

  /// Parse tool calls from non-streaming Ollama responses (OpenAI-compat format).
  void ParseToolCallsFromResponse(
      const std::string& body,
      std::vector<LLMToolCall>& tool_calls,
      std::string& finish_reason) const override;

  /// Query Ollama /v1/models for available models.
  bool FetchCapabilities(const std::string& modelId) override;

  /// Return vision-capable models from the local Ollama instance.
  /// Checks all models in raw_features against known vision prefixes.
  std::vector<std::string> GetVisionModelIds() const override;
};

} // namespace animus::kernel::llm