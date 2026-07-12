#include "animus_kernel/llm/SSEToolCallAccumulator.h"

#include <algorithm>
#include <json/json.h>

namespace animus::kernel::llm {

void SSEToolCallAccumulator::ProcessLine(const std::string& line) {
    // Parse the SSE line as JSON
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::istringstream stream(line);
    std::string errors;
    if (!Json::parseFromStream(builder, stream, &root, &errors)) {
        return; // Not valid JSON — skip
    }

    // Navigate to choices[0].delta.tool_calls
    if (!root.isMember("choices") || !root["choices"].isArray() || root["choices"].empty()) {
        return;
    }

    const auto& delta = root["choices"][0].get("delta", Json::Value());
    if (!delta.isMember("tool_calls") || !delta["tool_calls"].isArray()) {
        return;
    }

    for (const auto& tc : delta["tool_calls"]) {
        // Extract index (required for matching fragments)
        if (!tc.isMember("index") || !tc["index"].isInt()) {
            continue;
        }
        int index = tc["index"].asInt();

        auto& partial = m_partials[index];

        // First chunk for this index: has id, type, function.name
        if (tc.isMember("id")) {
            partial.id = tc["id"].asString();
        }

        if (tc.isMember("function")) {
            const auto& func = tc["function"];

            // function.name may arrive in the first chunk or be empty in continuation chunks
            if (func.isMember("name") && !func["name"].asString().empty()) {
                partial.name = func["name"].asString();
            }

            // function.arguments arrive as fragments that must be concatenated
            if (func.isMember("arguments")) {
                partial.arguments += func["arguments"].asString();
            }
        }
    }
}

std::vector<LLMToolCall> SSEToolCallAccumulator::Finalize() {
    std::vector<LLMToolCall> result;

    // Sort by index to maintain order
    std::vector<int> indices;
    indices.reserve(m_partials.size());
    for (const auto& [idx, _] : m_partials) {
        indices.push_back(idx);
    }
    std::sort(indices.begin(), indices.end());

    for (int idx : indices) {
        const auto& partial = m_partials[idx];
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