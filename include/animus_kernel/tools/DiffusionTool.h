#pragma once

#include "animus_kernel/tools/ToolRegistry.h"
#include "animus_kernel/tools/HttpClient.h"
#include "animus_kernel/DiffusionProvider.h"
#include "animus_kernel/DiffusionStore.h"

#include <memory>
#include <map>
#include <json/json.h>

namespace animus::kernel {

// ============================================================================
// DiffusionTool — agent-facing tool for diffusion media generation
// Supports image, video, audio, and 3D via pluggable providers
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
                  Config config);

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

    /// Create a provider instance from config
    std::unique_ptr<IDiffusionProvider> CreateProvider(const DiffusionProviderConfig& config);

private:
    HttpClient& m_client;        // shared kernel client (for provider list/CRUD)
    HttpClient m_ownClient;      // dedicated client for generation requests (larger body limit)
    DiffusionStore* m_store;
    Config m_config;

    ToolResult ExecuteGenerate(const ToolCall& call, const Json::Value& args);
    ToolResult ExecuteListModels(const ToolCall& call, const Json::Value& args);

    std::string EnsureOutputDir();
    std::string GenerateFilename(const std::string& type, const std::string& format);

    /// Save base64-encoded file data to disk
    bool SaveBase64(const std::string& base64Data, const std::string& savePath);
};

} // namespace animus::kernel