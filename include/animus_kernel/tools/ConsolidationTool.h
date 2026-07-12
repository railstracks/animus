#pragma once

#include "animus_kernel/tools/ToolTypes.h"
#include "animus_kernel/tools/ToolRegistry.h"
#include "animus_kernel/MemoryStore.h"
#include "animus_kernel/OntologyStore.h"
#include "animus_kernel/MemoryFileStore.h"
#include "animus_kernel/SessionReportStore.h"

#include <string>

namespace animus::kernel {

class SessionManager;
class AgentStore;
class EmbeddingService;

// ============================================================================
// ConsolidationTool — agent-facing tool for memory curation during
//                      consolidation sessions
// ============================================================================

class ConsolidationTool : public IToolHandler {
public:
    explicit ConsolidationTool(memory::MemoryStore* memoryStore,
                               ontology::OntologyStore* ontologyStore,
                               SessionManager* sessionManager = nullptr,
                               memory::MemoryFileStore* memoryFileStore = nullptr,
                               AgentStore* agentStore = nullptr,
                               SessionReportStore* sessionReportStore = nullptr,
                               const EmbeddingService* embeddingService = nullptr);

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

private:
    // Action handlers — agent_id is always extracted from __agent_id (ChainRunner-injected)
    // and never from params. Agents must not be able to override their own identity.
    ToolResult HandleReview(const std::string& arguments, const std::string& agentId);
    ToolResult HandlePromote(const std::string& arguments, const std::string& agentId);
    ToolResult HandleMerge(const std::string& arguments, const std::string& agentId);
    ToolResult HandleRevise(const std::string& arguments, const std::string& agentId);
    ToolResult HandleHistory(const std::string& arguments, const std::string& agentId);
    ToolResult HandleRetire(const std::string& arguments, const std::string& agentId);
    ToolResult HandleTag(const std::string& arguments, const std::string& agentId);
    ToolResult HandlePerspectiveGenerate(const std::string& arguments, const std::string& agentId);
    ToolResult HandlePerspectiveReview(const std::string& arguments, const std::string& agentId);
    ToolResult HandleSummary(const std::string& arguments, const std::string& agentId);
    ToolResult HandleCreate(const std::string& arguments, const std::string& agentId);
    ToolResult HandleFetchPending(const std::string& arguments, const std::string& agentId);
    ToolResult HandleOntologyUpsert(const std::string& arguments, const std::string& agentId);
    ToolResult HandleMemoryFileFetch(const std::string& arguments, const std::string& agentId);
    ToolResult HandleMemoryFileMarkProcessed(const std::string& arguments, const std::string& agentId);
    ToolResult HandleSessionReport(const std::string& arguments, const std::string& agentId);

    // Helpers
    bool VerifyOwnership(const memory::Observation& obs, const std::string& agentId) const;
    std::optional<memory::MemoryLayer> ResolveLayer(const std::string& layerName, const std::string& agentId) const;
    std::string ObservationsToJson(const std::vector<memory::Observation>& obs) const;

    int64_t ResolveAgentId(const std::string& agentId) const;

    memory::MemoryStore* m_memoryStore;
    ontology::OntologyStore* m_ontologyStore;
    SessionManager* m_sessionManager;
    memory::MemoryFileStore* m_memoryFileStore;
    AgentStore* m_agentStore;
    SessionReportStore* m_sessionReportStore;
    const EmbeddingService* m_embeddingService;
};

} // namespace animus::kernel