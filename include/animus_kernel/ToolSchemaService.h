#pragma once

#include <string>
#include <vector>

#include "animus_kernel/AgentStore.h"
#include "animus_kernel/llm/LLMTypes.h"
#include "animus_kernel/tools/ToolTypes.h"
#include "animus_kernel/tools/ToolRegistry.h"

namespace animus::kernel {

/// Generates filtered tool schemas for LLM requests.
///
/// Extracted from ChainRunner to separate schema generation
/// (which tools to offer, with what parameters) from chain execution.
///
/// Filtering pipeline:
///   1. Agent whitelist (enabled_tools) — if set, only listed tools
///   2. Session type filter — consolidation sessions get dedicated toolset;
///      typed tools only appear in matching session types
///   3. ChannelsTool gets per-agent context set/cleared during schema build
class ToolSchemaService {
public:
    ToolSchemaService(ToolRegistry& tools, AgentStore* agentStore = nullptr);

    /// All registered tools, unfiltered.
    std::vector<llm::LLMToolDef> GetAll() const;

    /// Filtered by agent whitelist (empty whitelist = all, backwards compat).
    std::vector<llm::LLMToolDef> GetForAgent(const std::string& agentId) const;

    /// Filtered by agent whitelist + session type.
    /// Consolidation sessions bypass agent whitelist and use dedicated toolset.
    std::vector<llm::LLMToolDef> GetForSession(
        const std::string& agentId,
        const std::string& sessionType) const;

    /// Convert a ToolDefinition to LLM tool schema format.
    static llm::LLMToolDef Convert(const ToolDefinition& def);

    void SetAgentStore(AgentStore* store) { m_agentStore = store; }

private:
    ToolRegistry& m_tools;
    AgentStore* m_agentStore{nullptr};
};

} // namespace animus::kernel
