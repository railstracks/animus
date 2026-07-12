#pragma once

#include "animus_kernel/llm/LLMProviderBase.h"
#include "animus_kernel/llm/CohereToolCallAccumulator.h"

namespace animus::kernel::llm {

class CohereProvider : public LLMProviderBase {
public:
  explicit CohereProvider(const LLMProviderConfig& config)
      : LLMProviderBase(config) {}

  std::string ProviderId() const override { return "cohere"; }

protected:
  std::string GetEndpointURL() const override;
  std::string BuildRequestBody(const LLMRequest& request) const override;
  LLMMessage ParseResponse(const std::string& body,
                           std::string* error) const override;
  std::optional<LLMToken> ParseSSELine(
      const std::string& line) const override;
  bool IsSSEDone(const std::string& line) const override;

  /// Override ProcessSSELine to feed Cohere tool call events
  /// to the CohereToolCallAccumulator (OpenAI accumulator can't parse them).
  bool ProcessSSELine(const std::string& line, std::string& accumulated,
                      LLMTokenCallback& callback) override;

  /// Override finalization to merge Cohere-specific tool calls.
  void ParseToolCallsFromResponse(
      const std::string& body,
      std::vector<LLMToolCall>& tool_calls,
      std::string& finish_reason) const override;

  /// Query Cohere's /v1/models endpoint for capabilities.
  bool FetchCapabilities(const std::string& modelId) override;

private:
  /// Cohere-specific tool call accumulator for streaming events.
  /// Mutable because ProcessSSELine is const in the base class
  /// but we need to modify the accumulator.
  mutable CohereToolCallAccumulator m_cohereToolCallAccumulator;
};

} // namespace animus::kernel::llm