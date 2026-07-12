#pragma once

#include <string>

#include "animus_kernel/llm/LLMTypes.h"

namespace animus::kernel::llm {

// ============================================================================
// ILLMProvider — pure interface for LLM backends
// ============================================================================

/// The kernel interacts exclusively through this interface.
/// No HTTP awareness, no provider specifics — just "send messages, get response".
class ILLMProvider {
public:
  virtual ~ILLMProvider() = default;

  /// Synchronous (non-streaming) completion.
  /// Returns the assistant message on success, or an empty message on error
  /// (with details in *error).
  virtual LLMMessage Complete(const LLMRequest& request,
                              std::string* error) = 0;

  /// Streaming completion. Invokes callback for each token chunk.
  /// Returns the fully assembled assistant message on success.
  virtual LLMMessage StreamComplete(const LLMRequest& request,
                                    LLMTokenCallback callback,
                                    std::string* error) = 0;

  /// Provider identity (e.g. "openai", "zai", "ollama").
  virtual std::string ProviderId() const = 0;

  /// Quick health check — returns false if the provider is known to be
  /// unavailable (auth failure, unreachable, etc.).
  virtual bool IsAvailable() const = 0;
};

} // namespace animus::kernel::llm
