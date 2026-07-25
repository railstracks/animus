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

        // Check for a new tool call at this index: if this fragment has an
        // "id" that differs from the last one we saw at this index, it's a
        // new tool call (some providers reuse index 0 for multiple calls).
        std::string fragmentId;
        if (tc.isMember("id")) {
            fragmentId = tc["id"].asString();
        }

        int subIndex = 0;
        if (!fragmentId.empty()) {
            auto it = m_lastIdByIndex.find(index);
            if (it != m_lastIdByIndex.end() && it->second != fragmentId) {
                // New tool call at the same index — increment sub-index
                subIndex = ++m_nextSubIndex[index];
            } else if (it == m_lastIdByIndex.end()) {
                // First tool call at this index
                m_nextSubIndex[index] = 0;
            }
            m_lastIdByIndex[index] = fragmentId;
        } else {
            // Continuation fragment — use the current sub-index for this index
            auto it = m_nextSubIndex.find(index);
            subIndex = (it != m_nextSubIndex.end()) ? it->second : 0;
        }

        int key = CompositeKey(index, subIndex);
        auto& partial = m_partials[key];

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

    // Sort by composite key to maintain order (index first, then sub-index)
    std::vector<int> keys;
    keys.reserve(m_partials.size());
    for (const auto& [key, _] : m_partials) {
        keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());

    for (int key : keys) {
        const auto& partial = m_partials[key];
        if (!partial.name.empty()) {
            LLMToolCall call;
            call.id = partial.id;
            call.name = partial.name;
            call.arguments = partial.arguments;
            result.push_back(std::move(call));
        }
    }

    m_partials.clear();
    m_lastIdByIndex.clear();
    m_nextSubIndex.clear();
    return result;
}

} // namespace animus::kernel::llm