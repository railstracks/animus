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
//
// Some providers (e.g. Ollama Cloud with Mistral/Qwen) send multiple
// distinct tool calls with the same "index" value (typically 0).
// To handle this, we detect when a fragment arrives with a new "id"
// at the same index as an existing partial — this signals a new tool
// call, not a continuation. We auto-increment a sub-index to keep
// them separate.
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

    // Key: composite of (index, sub-index) to handle providers that
    // reuse the same index for multiple distinct tool calls.
    // We use a single int key where the high bits are the original
    // index and the low 4 bits are the sub-index (up to 16 tool calls
    // per index).
    std::unordered_map<int, PartialToolCall> m_partials;

    // Track the last seen id per original index to detect new tool calls
    std::unordered_map<int, std::string> m_lastIdByIndex;

    // Track the next sub-index to use per original index
    std::unordered_map<int, int> m_nextSubIndex;

    /// Compute a composite key from index and sub-index.
    static int CompositeKey(int index, int subIndex) {
        return (index << 4) | (subIndex & 0xF);
    }
};

} // namespace animus::kernel::llm