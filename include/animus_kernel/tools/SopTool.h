#pragma once

#include "animus_kernel/tools/ToolRegistry.h"
#include "animus_kernel/SopStore.h"
#include "animus_kernel/MemoryFileStore.h"

#include <string>

namespace animus::kernel {

// ============================================================================
// SopTool — agent-facing tool for browsing and loading SOPs
// Ticket 125: SOP System
// ============================================================================

class SopTool : public IToolHandler {
public:
    SopTool(SopStore* store, memory::MemoryFileStore* memoryFiles, int64_t agentId = 0)
        : m_store(store), m_memoryFiles(memoryFiles), m_agentId(agentId) {}

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

    /// Set the agent ID for MemoryFile writes (called per-invocation).
    void SetAgentId(int64_t agentId) { m_agentId = agentId; }

private:
    SopStore* m_store;
    memory::MemoryFileStore* m_memoryFiles;
    int64_t m_agentId;

    ToolResult HandleList(const std::string& args);
    ToolResult HandleSearch(const std::string& args);
    ToolResult HandleLoad(const std::string& args);

    std::string MetaToJson(const SopMeta& meta) const;
};

} // namespace animus::kernel
