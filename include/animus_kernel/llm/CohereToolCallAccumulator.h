#pragma once

#include <map>
#include <string>
#include <vector>
#include "animus_kernel/llm/LLMTypes.h"

namespace animus::kernel::llm {

// ============================================================================
// CohereToolCallAccumulator — accumulate Cohere v2 streaming tool calls
// ============================================================================
// Cohere streams tool calls differently from OpenAI:
//   tool-call-start → {index, id, name}
//   tool-call-delta → {index, arguments fragment}
//   tool-call-end   → {index}
// We accumulate id/name/arguments by index and finalize on message-end.

class CohereToolCallAccumulator {
public:
    /// Process a raw SSE data line from Cohere's streaming response.
    void ProcessLine(const std::string& line);

    /// Return accumulated tool calls and reset state.
    std::vector<LLMToolCall> Finalize();

    /// Return true if any tool calls have been started.
    bool HasToolCalls() const { return !m_partials.empty(); }

private:
    struct PartialCall {
        std::string id;
        std::string name;
        std::string arguments;  // Concatenated fragments
    };

    std::map<int, PartialCall> m_partials;
};

} // namespace animus::kernel::llm