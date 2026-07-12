#pragma once

#include <string>
#include <vector>
#include <optional>
#include "animus_kernel/llm/LLMTypes.h"

namespace animus::kernel::llm {
namespace openai_compat {

/// Minimal JSON string extraction — extracts value for a given key.
std::string ExtractJsonString(const std::string& json, const std::string& key);

/// Extract a numeric value as string.
std::string ExtractJsonNumber(const std::string& json, const std::string& key);

/// Escape a string for JSON embedding.
std::string EscapeJson(const std::string& s);

/// Build a standard OpenAI /v1/chat/completions request body.
/// Now includes tool definitions and tool result messages.
std::string BuildOpenAIRequestBody(const LLMRequest& request);

/// Parse tool calls from a non-streaming OpenAI response body.
std::vector<LLMToolCall> ParseToolCalls(const std::string& body);

/// Extract finish_reason from a response body.
std::string ExtractFinishReason(const std::string& body);

// ============================================================================
// Shared helpers for providers with reasoning_content support
// (Z.ai, Alibaba/DashScope, DeepSeek)
// ============================================================================

/// Parse a non-streaming OpenAI response that may contain reasoning_content.
/// Extracts both content and thinking_content from the choices array.
/// Returns an LLMMessage with role, content, and thinking_content populated.
LLMMessage ParseResponseWithReasoning(const std::string& body,
                                       std::string* error);

/// Parse an SSE line from a provider that supports reasoning_content.
/// Checks for reasoning_content first (thinking tokens), then standard
/// content and finish_reason. Returns nullopt for empty non-final chunks.
std::optional<LLMToken> ParseSSELineWithReasoning(const std::string& line);

} // namespace openai_compat
} // namespace animus::kernel::llm