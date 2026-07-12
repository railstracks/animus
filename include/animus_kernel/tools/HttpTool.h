#pragma once

#include "animus_kernel/tools/HttpClient.h"
#include "animus_kernel/tools/ToolRegistry.h"

#include <memory>

namespace animus::kernel {

// ============================================================================
// HttpTool — general-purpose HTTP client for API calls and webhooks
// ============================================================================

class HttpTool : public IToolHandler {
public:
    /// @param client Shared HTTP client instance. Must outlive this tool.
    explicit HttpTool(HttpClient& client);

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

private:
    HttpClient& m_client;
};

} // namespace animus::kernel