#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace animus::kernel::llm {

// ============================================================================
// LLM Types — provider-agnostic request/response types
// ============================================================================

/// A single message in the conversation history.
struct LLMMessage {
  std::string role;     // "system", "user", "assistant", "tool"
  std::string content;
};

/// A request to an LLM provider.
struct LLMRequest {
  std::string model;
  std::vector<LLMMessage> messages;
  float temperature{1.0f};
  int max_tokens{-1};   // -1 = provider default
  bool stream{true};

  /// Provider-specific parameters (e.g. Z.ai "thinking", Ollama "num_ctx").
  std::unordered_map<std::string, std::string> extra;
};

/// A single token/chunk yielded during streaming, or the final result.
struct LLMToken {
  std::string content;
  bool is_final{false};
  std::string finish_reason; // "stop", "length", "tool_calls", ""
  int prompt_tokens{0};
  int completion_tokens{0};
};

/// Callback invoked for each token during streaming.
using LLMTokenCallback = std::function<void(const LLMToken&)>;

} // namespace animus::kernel::llm
