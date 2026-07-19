#pragma once

#include "animus_kernel/tools/ToolRegistry.h"
#include "animus_kernel/tools/HttpClient.h"
#include "animus_kernel/DiffusionProvider.h"
#include "animus_kernel/DiffusionStore.h"

#include <memory>
#include <map>

namespace animus::kernel {

// ============================================================================
// DiffusionTool — agent-facing tool for diffusion image/video generation
// ============================================================================

class DiffusionTool : public IToolHandler {
public:
    struct Config {
        std::string output_dir{"media/generated"};
        int video_poll_interval_sec{5};
        int video_timeout_sec{300};
    };

    DiffusionTool(HttpClient& client,
                  DiffusionStore* store,
                 Config config = {});
                  Config config);

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

    /// Create a provider instance from config
    std::unique_ptr<IDiffusionProvider> CreateProvider(const DiffusionProviderConfig& config);

private:
    HttpClient& m_client;
    DiffusionStore* m_store;
    Config m_config;

    ToolResult ExecuteGenerate(const ToolCall& call);
    ToolResult ExecuteListModels(const ToolCall& call);

    std::string EnsureOutputDir();
    std::string GenerateFilename(const std::string& type, const std::string& format);
};

} // namespace animus::kernel
