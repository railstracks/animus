#pragma once

#include "animus_kernel/tools/HttpClient.h"
#include "animus_kernel/tools/ToolRegistry.h"

#include <memory>
#include <string>

namespace animus::kernel {

// ============================================================================
// WebFetchTool — fetch and extract readable content from URLs
// ============================================================================

class WebFetchTool : public IToolHandler {
public:
    /// @param client Shared HTTP client instance. Must outlive this tool.
    explicit WebFetchTool(HttpClient& client);

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

private:
    HttpClient& m_client;

    /// Strip HTML tags and extract readable content.
    std::string HtmlToText(const std::string& html) const;

    /// Convert HTML to simplified markdown.
    std::string HtmlToMarkdown(const std::string& html) const;

    /// Detect content type from headers and decide output format.
    std::string DetectFormat(const std::string& requestedFormat,
                             const std::string& contentType) const;
};

} // namespace animus::kernel