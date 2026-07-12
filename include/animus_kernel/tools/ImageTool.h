#pragma once

#include "animus_kernel/tools/ToolRegistry.h"
#include "animus_kernel/tools/HttpClient.h"
#include "animus_kernel/tools/ImageGenProvider.h"

#include <memory>
#include <string>

namespace animus::kernel {

// ============================================================================
// ImageTool — unified image analyze + generate tool
// ============================================================================
// Presented to the agent as a single "image" tool with an action parameter.
// Internally dispatches to ImageAnalyzeHandler (vision LLM) or
// ImageGenerateHandler (generation API).
//
// Requires:
// - A shared HttpClient for generation API calls
// - Provider configs for generation (base URLs, API keys)
// ============================================================================

class ChainRunner; // forward declaration for LLM access
class AgentStore;

class ImageTool : public IToolHandler {
public:
    struct Config {
        // Analysis settings
        std::string analysis_default_model;
        int analysis_max_dimension_px{1200};
        size_t analysis_max_bytes{10 * 1024 * 1024};
        int analysis_max_images{20};

        // Generation settings
        std::string generation_default_provider{"ollama"};
        ImageGenProviderConfig gen_ollama;
        ImageGenProviderConfig gen_zai;
        ImageGenProviderConfig gen_openai;
        std::string generation_output_dir{"media/generated"};
    };

    /// @param client Shared HTTP client (for image gen API calls + URL downloads)
    /// @param config Tool configuration
    /// @param chainRunner Access to the LLM provider system for vision analysis
    ImageTool(HttpClient& client,
              Config config,
              ChainRunner* chainRunner = nullptr,
              AgentStore* agentStore = nullptr);

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

private:
    HttpClient& m_client;
    Config m_config;
    ChainRunner* m_chainRunner;
    AgentStore* m_agentStore;

    // Lazy-initialized gen providers
    std::unique_ptr<IImageGenProvider> m_ollamaGen;
    std::unique_ptr<IImageGenProvider> m_zaiGen;
    std::unique_ptr<IImageGenProvider> m_openaiGen;

    /// Handle the "analyze" action.
    ToolResult ExecuteAnalyze(const ToolCall& call);

    /// Handle the "generate" action.
    ToolResult ExecuteGenerate(const ToolCall& call);

    /// Get or create the provider for a given provider name.
    IImageGenProvider* GetGenProvider(const std::string& name);

    /// Save base64 image data to a file. Returns the saved path.
    std::string SaveBase64Image(const std::string& base64_data,
                                 const std::string& content_type,
                                 const std::string& filename);

    /// Download an image from a URL and save to file.
    std::string DownloadAndSaveImage(const std::string& url,
                                      const std::string& filename);

    /// Base64 decode helper.
    static std::string Base64Decode(const std::string& encoded);

    /// Ensure output directory exists.
    void EnsureOutputDir();
};

} // namespace animus::kernel
