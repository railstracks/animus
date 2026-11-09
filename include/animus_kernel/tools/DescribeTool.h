#pragma once

#include "animus_kernel/DescribeService.h"
#include "animus_kernel/tools/ToolRegistry.h"

namespace animus::kernel {

// ============================================================================
// DescribeTool — #160 active layer: describe_image(ref, prompt?)
//
// Explicit depth on vaulted images: a different question about the same
// image hits the same cache-first DescribeService (sha256 + prompt hash)
// as auto-describe — repeat questions are free. Results are ALWAYS
// provenance-labeled: a machine description is evidence about an image,
// never ground truth.
//
// Engine policy mirrors the passive layer: local is the default; cloud
// only when the operator configured it (provider + model id). Requested
// cloud without configuration fails loud — never a silent fallback.
// ============================================================================

class DescribeTool : public IToolHandler {
public:
    // Both services must outlive the tool. cloudService may be null
    // (cloud unconfigured — engine=cloud then fails loud).
    explicit DescribeTool(DescribeService* localService,
                          DescribeService* cloudService = nullptr);

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

private:
    DescribeService* m_local;
    DescribeService* m_cloud;
};

} // namespace animus::kernel
