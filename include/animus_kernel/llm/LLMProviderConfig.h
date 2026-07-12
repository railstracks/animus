#pragma once

#include <string>
#include <unordered_map>

namespace animus::kernel::llm {

// ============================================================================
// LLMProviderConfig — provider configuration (not provider-specific)
// ============================================================================

struct LLMProviderConfig {
  std::string provider_id; // "openai", "zai", "ollama", "mistral", "cohere"
  std::string base_url;    // e.g. "https://api.openai.com/v1"
  std::string api_key;     // empty for Ollama
  std::string default_model;

  int connect_timeout_ms{30000};
  int stream_idle_timeout_ms{120000};

  /// Provider-specific parameters.
  /// e.g. Z.ai: thinking=enabled, Ollama: num_ctx=4096, Cohere: safety_mode=CONTEXTUAL
  std::unordered_map<std::string, std::string> extra;
};

} // namespace animus::kernel::llm
