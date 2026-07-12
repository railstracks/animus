#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "animus_kernel/llm/LLMTypes.h"

namespace animus::kernel::llm {

// ============================================================================
// SSEToolCallAccumulator — accumulates tool call delta fragments across
// multiple SSE chunks into complete LLMToolCall objects.
//
// OpenAI streaming sends tool calls as delta fragments:
//   1st chunk: {"index":0,"id":"call_abc","type":"function","function":{"name":"reply","arguments":""}}
//   2nd chunk: {"index":0,"function":{"arguments":"{\"te"}}
//   3rd chunk: {"index":0,"function":{"arguments":"xt\": \"Hello"}}
//   4th chunk: {"index":0,"function":{"arguments":"\"}"}}
//   Final:     {"finish_reason":"tool_calls"}
//
// This accumulator tracks partial tool calls by index and assembles
// them as fragments arrive. Call Finalize() when the stream ends
// (finish_reason="tool_calls" or stream done) to get complete results.
// ============================================================================

class SSEToolCallAccumulator {
public:
    /// Process an SSE data line that may contain tool call delta fragments.
    /// Extracts index, id, function.name, and function.arguments from the line
    /// and accumulates them into partial tool calls.
    void ProcessLine(const std::string& line);

    /// Finalize accumulation after the stream ends.
    /// Returns all assembled tool calls with complete names and arguments.
    /// Clears internal state for reuse.
    std::vector<LLMToolCall> Finalize();

    /// Check if we have any partial tool calls being accumulated.
    bool HasPartialCalls() const { return !m_partials.empty(); }

private:
    struct PartialToolCall {
        std::string id;
        std::string name;
        std::string arguments; // accumulated argument fragments
    };

    // Indexed partial tool calls (OpenAI uses "index" to match fragments)
    std::unordered_map<int, PartialToolCall> m_partials;
};

} // namespace animus::kernel::llm