#include "animus_kernel/tools/DiffusionTool.h"
#include "animus_kernel/tools/Base64.h"

#include <json/json.h>
#include <iostream>
#include <filesystem>
#include <chrono>
#include <thread>
#include <random>
#include <iomanip>
#include <sstream>

// GetImg provider implementation
#include "animus_kernel/DiffusionProvider.h"
#include "animus_kernel/tools/HttpClient.h"

// GetImgProvider is defined in GetImgProvider.cpp (same namespace)
// We reference it here via forward declaration
namespace animus::kernel {
    class GetImgProvider;
}

namespace animus::kernel {

// Forward-declared GetImgProvider is in GetImgProvider.cpp
// We need to construct it — include the implementation header
// Since GetImgProvider.cpp defines the class in the same namespace,
// we declare it here for the factory method.

// Actually, GetImgProvider is fully defined in GetImgProvider.cpp.
// For the factory, we'll need to reference it. Let's use a different approach:
// The GetImgProvider class is defined entirely in GetImgProvider.cpp.
// We'll create it there via a factory function.

} // namespace animus::kernel

namespace animus::kernel {

// Factory function implemented in GetImgProvider.cpp
// This avoids header exposure of the full class
std::unique_ptr<IDiffusionProvider> CreateGetImgProvider(
    HttpClient& client, const DiffusionProviderConfig& config);

} // namespace animus::kernel

namespace animus::kernel {

DiffusionTool::DiffusionTool(HttpClient& client,
                             DiffusionStore* store,
                             Config config)
    : m_client(client), m_store(store), m_config(std::move(config)) {
    EnsureOutputDir();
}

ToolDefinition DiffusionTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "diffusion";
    def.description = "Generate images or video using diffusion models (e.g. GetImg). "
                      "Different models produce different visual styles — choose the model "
                      "that matches the desired aesthetic (e.g. SynthWave, Anime, Cinematic, "
                      "Photorealistic). Use list_models to see available models.";

    def.parameters.push_back({"action", "string", true,
        "generate | list_models"});
    def.parameters.push_back({"type", "string", false,
        "image | video (default: image)"});
    def.parameters.push_back({"prompt", "string", false,
        "Text description of the media to generate (required for generate)"});
    def.parameters.push_back({"model", "string", false,
        "Diffusion model ID. Choose based on desired visual style. "
        "Use action=list_models to see available options."});
    def.parameters.push_back({"provider", "string", false,
        "Which configured diffusion provider to use (e.g. 'getimg')"});
    def.parameters.push_back({"aspect_ratio", "string", false,
        "1:1, 16:9, 9:16, 2:3, 3:2"});
    def.parameters.push_back({"resolution", "string", false,
        "1K, 2K, 4K (images) or 720p, 1080p (video)"});
    def.parameters.push_back({"output_format", "string", false,
        "png | jpeg | webp (images only)"});
    def.parameters.push_back({"duration", "integer", false,
        "Duration in seconds (video only)"});
    def.parameters.push_back({"sound", "boolean", false,
        "Generate audio (video only, where supported)"});

    return def;
}

ToolResult DiffusionTool::Execute(const ToolCall& call) {
    std::string action = call.GetParam("action");

    if (action == "list_models") {
        return ExecuteListModels(call);
    }
    if (action == "generate") {
        return ExecuteGenerate(call);
    }

    ToolResult r;
    r.success = false;
    r.error = "Unknown action: " + action + ". Use 'generate' or 'list_models'.";
    return r;
}

ToolResult DiffusionTool::ExecuteListModels(const ToolCall& call) {
    ToolResult result;

    if (!m_store) {
        result.success = false;
        result.error = "No diffusion store configured";
        return result;
    }

    auto providers = m_store->ListProviders();
    if (providers.empty()) {
        result.success = false;
        result.error = "No diffusion providers configured. "
                        "Ask an administrator to add a diffusion provider.";
        return result;
    }

    Json::Value modelsJson(Json::arrayValue);
    for (const auto& pconfig : providers) {
        auto provider = CreateProvider(pconfig);
        if (!provider) continue;

        auto models = provider->ListModels();
        for (const auto& m : models) {
            Json::Value modelObj;
            modelObj["id"] = m.id;
            modelObj["type"] = m.type;
            modelObj["name"] = m.name;
            modelObj["provider"] = pconfig.id;
            modelsJson.append(modelObj);
        }
    }

    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    result.success = true;
    result.content = Json::writeString(wb, modelsJson);
    return result;
}

ToolResult DiffusionTool::ExecuteGenerate(const ToolCall& call) {
    ToolResult result;

    std::string prompt = call.GetParam("prompt");
    if (prompt.empty()) {
        result.success = false;
        result.error = "prompt is required for generate action";
        return result;
    }

    std::string model = call.GetParam("model");
    if (model.empty()) {
        result.success = false;
        result.error = "model is required for generate action. "
                        "Use action=list_models to see available models.";
        return result;
    }

    std::string type = call.GetParam("type");
    if (type.empty()) type = "image";

    std::string providerId = call.GetParam("provider");

    // Find provider
    if (!m_store) {
        result.success = false;
        result.error = "No diffusion store configured";
        return result;
    }

    DiffusionProviderConfig pconfig;
    bool found = false;

    if (!providerId.empty()) {
        auto opt = m_store->GetProvider(providerId);
        if (opt) {
            pconfig = *opt;
            found = true;
        }
    } else {
        // Use first available provider
        auto providers = m_store->ListProviders();
        if (!providers.empty()) {
            pconfig = providers[0];
            found = true;
        }
    }

    if (!found) {
        result.success = false;
        result.error = "No diffusion provider found"
                        + (providerId.empty() ? "" : " with id '" + providerId + "'");
        return result;
    }

    auto provider = CreateProvider(pconfig);
    if (!provider) {
        result.success = false;
        result.error = "Failed to create provider '" + pconfig.id + "' (type: " + pconfig.type + ")";
        return result;
    }

    // Build request
    DiffusionGenRequest genReq;
    genReq.prompt = prompt;
    genReq.model = model;
    genReq.type = type;
    genReq.aspect_ratio = call.GetParam("aspect_ratio");
    if (genReq.aspect_ratio.empty() && !pconfig.default_aspect_ratio.empty()) {
        genReq.aspect_ratio = pconfig.default_aspect_ratio;
    }
    genReq.resolution = call.GetParam("resolution");
    genReq.output_format = call.GetParam("output_format");
    if (genReq.output_format.empty()) genReq.output_format = "png";

    std::string durStr = call.GetParam("duration");
    if (!durStr.empty()) {
        try { genReq.duration = std::stoi(durStr); } catch (...) {}
    }

    std::string soundStr = call.GetParam("sound");
    genReq.sound = (soundStr == "true" || soundStr == "1");

    // Generate
    std::string genError;
    auto genResult = provider->Generate(genReq, &genError);

    if (!genResult.success) {
        result.success = false;
        result.error = genResult.error.empty() ? genError : genResult.error;
        return result;
    }

    // For async (video): poll until complete
    if (genResult.async && !genResult.generation_id.empty()) {
        int pollInterval = m_config.video_poll_interval_sec;
        int elapsed = 0;
        int timeout = m_config.video_timeout_sec;

        while (elapsed < timeout) {
            std::this_thread::sleep_for(std::chrono::seconds(pollInterval));
            elapsed += pollInterval;

            DiffusionGenResult pollResult;
            std::string pollError;
            bool terminal = provider->PollStatus(genResult.generation_id, pollResult, &pollError);

            if (terminal) {
                if (!pollResult.success) {
                    result.success = false;
                    result.error = pollResult.error.empty() ? pollError : pollResult.error;
                    return result;
                }
                // Completed — copy download info
                genResult.media_url = pollResult.media_url;
                genResult.content_type = pollResult.content_type;
                genResult.width = pollResult.width;
                genResult.height = pollResult.height;
                genResult.duration_seconds = pollResult.duration_seconds;
                break;
            }
        }

        if (genResult.media_url.empty()) {
            result.success = false;
            result.error = "Video generation timed out after " + std::to_string(timeout) + "s";
            return result;
        }
    }

    // Download media
    std::string ext = (type == "video") ? "mp4" : genReq.output_format;
    if (ext.empty()) ext = "png";
    std::string filename = GenerateFilename(type, ext);
    std::string savePath = EnsureOutputDir() + "/" + filename;

    std::string dlError;
    if (!provider->DownloadMedia(genResult.media_url, savePath, &dlError)) {
        result.success = false;
        result.error = "Generation succeeded but download failed: " + dlError;
        return result;
    }

    result.success = true;
    result.content = "Generated " + type + " saved to: " + savePath;
    if (genResult.width > 0 && genResult.height > 0) {
        result.content += " (" + std::to_string(genResult.width) + "x"
                        + std::to_string(genResult.height) + ")";
    }
    if (genResult.duration_seconds > 0) {
        result.content += " " + std::to_string(genResult.duration_seconds) + "s";
    }
    return result;
}

std::unique_ptr<IDiffusionProvider> DiffusionTool::CreateProvider(
    const DiffusionProviderConfig& config) {
    if (config.type == "getimg") {
        return CreateGetImgProvider(m_client, config);
    }
    std::cerr << "[diffusion] Unknown provider type: " << config.type << std::endl;
    return nullptr;
}

std::string DiffusionTool::EnsureOutputDir() {
    auto dir = std::filesystem::path(m_config.output_dir);
    std::filesystem::create_directories(dir);
    return m_config.output_dir;
}

std::string DiffusionTool::GenerateFilename(const std::string& type, const std::string& format) {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 9999);

    std::ostringstream oss;
    oss << "diffusion_" << type << "_" << ms << "_"
        << std::setfill('0') << std::setw(4) << dis(gen) << "." << format;
    return oss.str();
}

} // namespace animus::kernel