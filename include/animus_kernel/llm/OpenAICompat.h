#pragma once

#include <string>
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
std::string BuildOpenAIRequestBody(const LLMRequest& request);

} // namespace openai_compat
} // namespace animus::kernel::llm
