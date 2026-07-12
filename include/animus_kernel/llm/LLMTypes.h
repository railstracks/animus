#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace animus::kernel::llm {

// ============================================================================
// LLM Types — provider-agnostic request/response types
// ============================================================================


// ============================================================================
// Reasoning effort — controls how much thinking/reasoning the provider does
// ============================================================================

/// Reasoning effort levels. Empty string = disabled.
/// Providers map this to their own parameter format:
///   Ollama:  "reasoning_effort": "high"
///   OpenAI:  "reasoning_effort": "high" / "xhigh"
///   Z.ai:    "thinking": {"type": "enabled"}
///   Cohere:  "thinking": {"type": "enabled"}
///
/// Valid values: "", "none", "low", "medium", "high", "xhigh"
/// "xhigh" is OpenAI-specific; other providers silently ignore it.

inline bool IsReasoningEffort(const std::string& effort) {
    return effort == "none" || effort == "low" || effort == "medium"
        || effort == "high" || effort == "xhigh";
}

inline bool IsReasoningEnabled(const std::string& effort) {
    return !effort.empty() && effort != "none";
}

// ============================================================================
// Provider capabilities — what a specific model supports
// ============================================================================

struct ProviderCapabilities {
    std::string model_id;              // Model identifier
    int context_length{0};             // Max context window in tokens

    // Feature flags — queried from provider API or hardcoded
    bool supports_tools{false};        // Tool calling (function calling)
    bool supports_tool_choice{false};   // tool_choice parameter (auto/none/specific)
    bool supports_reasoning{false};     // Native thinking/reasoning
    bool supports_streaming{true};      // SSE streaming
    bool supports_json_mode{false};     // JSON output mode
    bool supports_vision{false};        // Image/multimodal input

    // Raw feature strings from provider (e.g. Cohere returns ["tools", "reasoning"])
    std::vector<std::string> raw_features;

    /// Whether tools can be used with the current configuration.
    /// Tools require the model to support them; tool_choice is optional.
    bool CanUseTools() const { return supports_tools; }

    /// Whether reasoning can be enabled.
    bool CanUseReasoning() const { return supports_reasoning; }
};

/// A single tool call in an LLM response.
struct LLMToolCall {
    std::string id;            // Unique call identifier
    std::string name;          // Tool name
    std::string arguments;     // JSON string of arguments
};

/// A single part of multi-modal content (text or image).
/// When a message has content_parts, the request builder emits the
/// OpenAI content-array format instead of a plain string.
struct ContentPart {
    std::string type;         // "text" | "image_url"
    std::string text;         // for type="text"
    std::string image_url;    // for type="image_url" — data URI or HTTP URL
    std::string detail;       // optional: "auto" | "low" | "high" (OpenAI)
};

/// A single message in the conversation history.
struct LLMMessage {
    std::string role;     // "system", "user", "assistant", "tool"
    std::string content;
    std::vector<ContentPart> content_parts;  // Multi-modal: when non-empty, emit as array
    std::string thinking_content;  // Native thinking/reasoning trace (non-streaming)
    std::string tool_call_id;  // For role="tool" responses: which call this answers
    std::string name;          // For role="tool" responses: which tool was called
    std::vector<LLMToolCall> tool_calls;  // For role="assistant": tool calls made

    /// True if this message has multi-modal content parts.
    bool HasContentParts() const { return !content_parts.empty(); }
};

/// A tool definition to include in an LLM request.
struct LLMToolDef {
    std::string type;        // Always "function" for OpenAI-compatible
    std::string name;
    std::string description;
    std::string parameters;  // JSON schema for parameters
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

    /// Tool definitions to include in the request.
    /// When empty, no tools are offered and the model responds in plain text.
    std::vector<LLMToolDef> tools;

    /// Tool choice: "auto" (model decides), "none" (no tools), or specific tool name.
    std::string tool_choice{"auto"};

    /// Reasoning effort — empty string (disabled) or "none"/"low"/"medium"/"high"/"xhigh".
    /// Providers map this to their native thinking/reasoning parameter.
    std::string reasoning_effort;
};

/// A single token/chunk yielded during streaming, or the final result.
struct LLMToken {
    std::string content;
    bool is_final{false};
    std::string finish_reason; // "stop", "length", "tool_calls", ""
    int prompt_tokens{0};
    int completion_tokens{0};

    /// True if this token is thinking/reasoning content (not user-facing).
    bool is_thinking{false};

    /// Tool calls parsed from streaming delta (populated on finish_reason="tool_calls").
    std::vector<LLMToolCall> tool_calls;
};

/// Callback invoked for each token during streaming.
using LLMTokenCallback = std::function<void(const LLMToken&)>;

/// Parsed LLM response — content + tool calls.
struct LLMResponse {
    std::string content;              // Text content (user-visible response)
    std::string thinking_content;     // Native thinking/reasoning trace
    std::vector<LLMToolCall> tool_calls; // Parsed tool invocations
    std::string finish_reason;        // "stop", "tool_calls", "length"
    int prompt_tokens{0};
    int completion_tokens{0};
    bool is_thinking{false};           // True if this token is from native thinking field
};

} // namespace animus::kernel::llm