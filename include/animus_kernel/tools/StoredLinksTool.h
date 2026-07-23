#pragma once

#include "animus_kernel/tools/HttpClient.h"
#include "animus_kernel/tools/ToolRegistry.h"

#include <string>
#include <vector>

namespace animus::kernel {

// ============================================================================
// StoredLinksTool — fetch pre-configured HTTP links by identifier
//
// Operator configures named links (id, label, url) in KernelConfig.
// Agent calls stored_links("list") to see available links, or
// stored_links("sp500-4h") to fetch the URL and return the raw response.
// ============================================================================

class StoredLinksTool : public IToolHandler {
public:
    struct StoredLink {
        std::string id;
        std::string label;
        std::string url;
    };

    /// @param client Shared HTTP client instance. Must outlive this tool.
    /// @param links Pre-configured stored links from KernelConfig.
    explicit StoredLinksTool(HttpClient& client, std::vector<StoredLink> links);

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

private:
    HttpClient& m_client;
    std::vector<StoredLink> m_links;

    std::string FormatList(const std::vector<StoredLink>& links) const;
};

} // namespace animus::kernel