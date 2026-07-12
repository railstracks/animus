#include "animus_kernel/llm/CohereToolCallAccumulator.h"
#include "animus_kernel/llm/OpenAICompat.h"

#include <json/json.h>

namespace animus::kernel::llm {

using namespace openai_compat;

void CohereToolCallAccumulator::ProcessLine(const std::string& line) {
    // Parse the SSE line as JSON
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::istringstream stream(line);
    std::string errors;
    if (!Json::parseFromStream(builder, stream, &root, &errors)) {
        return;
    }

    const auto type = root.get("type", "").asString();

    if (type == "tool-call-start") {
        // Extract index from the event
        int index = root.get("index", 0).asInt();

        // Navigate to delta.message.tool_calls[0]
        const auto& delta = root.get("delta", Json::Value());
        const auto& message = delta.get("message", Json::Value());
        const auto& toolCalls = message.get("tool_calls", Json::Value());

        if (toolCalls.isArray() && !toolCalls.empty()) {
            const auto& tc = toolCalls[0];
            auto& partial = m_partials[index];
            partial.id = tc.get("id", "").asString();

            const auto& func = tc.get("function", Json::Value());
            if (func.isMember("name")) {
                partial.name = func["name"].asString();
            }
        }
    } else if (type == "tool-call-delta") {
        int index = root.get("index", 0).asInt();

        // Navigate to delta.message.tool_calls.function.arguments
        const auto& delta = root.get("delta", Json::Value());
        const auto& message = delta.get("message", Json::Value());
        const auto& toolCalls = message.get("tool_calls", Json::Value());
        const auto& func = toolCalls.get("function", Json::Value());

        if (func.isMember("arguments")) {
            auto& partial = m_partials[index];
            partial.arguments += func["arguments"].asString();
        }
    } else if (type == "tool-call-end") {
        // Tool call complete for this index — nothing extra to do,
        // the call is already accumulated from start + delta events.
    }
}

std::vector<LLMToolCall> CohereToolCallAccumulator::Finalize() {
    std::vector<LLMToolCall> result;

    for (const auto& [idx, partial] : m_partials) {
        if (!partial.name.empty()) {
            LLMToolCall call;
            call.id = partial.id;
            call.name = partial.name;
            call.arguments = partial.arguments;
            result.push_back(std::move(call));
        }
    }

    m_partials.clear();
    return result;
}

} // namespace animus::kernel::llm