#pragma once

#include "animus_kernel/tools/ToolRegistry.h"
#include "animus_kernel/AttachmentStore.h"
#include "animus_kernel/SessionManager.h"

#include <string>

namespace animus::kernel {

// ============================================================================
// AttachmentTool — agent-facing tool for attaching files to chat sessions
// Ticket 123 Phase 1 — webchat only
// ============================================================================

class AttachmentTool : public IToolHandler {
public:
    AttachmentTool(AttachmentStore* store, SessionManager* sessions)
        : m_store(store), m_sessions(sessions) {}

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

private:
    AttachmentStore* m_store;
    SessionManager* m_sessions;

    std::string DetectMimeType(const std::string& filepath) const;
    std::string GenerateId() const;
    std::string ExtractFilename(const std::string& filepath) const;
    std::string EncodeBase64(const std::string& data) const;
};

} // namespace animus::kernel