#include "animus_kernel/tools/DiffusionTool.h"

#include <json/json.h>
#include <iostream>
#include <filesystem>
#include <chrono>
#include <thread>
#include <random>
#include <iomanip>
#include <sstream>

namespace animus::kernel {

// Factory function implemented in GetImgProvider.cpp
std::unique_ptr<IDiffusionProvider> CreateGetImgProvider(
    HttpClient& client, const DiffusionProviderConfig& config);

// ============================================================================
// Helpers (anonymous namespace, same pattern as ImageTool)
// ============================================================================

namespace {

Json::Value ParseArgs(const std::string& args) {
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::istringstream stream(args);
    std::string errors;
    Json::parseFromStream(builder, stream, &root, &errors);
    return root;
}

std::string GetStringField(const Json::Value& v, const std::string& key,
                            const std::string& def = "") {
    return (v.isMember(key) && v[key].isString()) ? v[key].asString() : def;
}

int GetIntField(const Json::Value& v, const std::string& key, int def = 0) {
    if (v.isMember(key) && v[key].isInt()) return v[key].asInt();
    return def;
}

bool GetBoolField(const Json::Value& v, const std::string& key, bool def = false) {
    if (v.isMember(key) && v[key].isBool()) return v[key].asBool();
    if (v.isMember(key) && v[key].isString()) return v[key].asString() == "true";
    return def;
}

} // namespace

// ============================================================================
// Construction
// ============================================================================

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

    ToolParameter pAction;
    pAction.name = "action"; pAction.type = "string"; pAction.required = true;
    pAction.description = "generate | list_models";
    pAction.enum_values = {"generate", "list_models"};
    def.parameters.push_back(pAction);

    ToolParameter pType;
    pType.name = "type"; pType.type = "string"; pType.required = false;
    pType.description = "image | video (default: image)";
    pType.enum_values = {"image", "video"};
    def.parameters.push_back(pType);

    ToolParameter pPrompt;
    pPrompt.name = "prompt"; pPrompt.type = "string"; pPrompt.required = false;
    pPrompt.description = "Text description of the media to generate (required for generate)";
    def.parameters.push_back(pPrompt);

    ToolParameter pModel;
    pModel.name = "model"; pModel.type = "string"; pModel.required = false;
    pModel.description = "Diffusion model ID. Choose based on desired visual style. "
                         "Use action=list_models to see available options.";
    def.parameters.push_back(pModel);

    ToolParameter pProvider;
    pProvider.name = "provider"; pProvider.type = "string"; pProvider.required = false;
    pProvider.description = "Which configured diffusion provider to use (e.g. 'getimg')";
    def.parameters.push_back(pProvider);

    ToolParameter pAspect;
    pAspect.name = "aspect_ratio"; pAspect.type = "string"; pAspect.required = false;
    pAspect.description = "1:1, 16:9, 9:16, 2:3, 3:2";
    def.parameters.push_back(pAspect);

    ToolParameter pRes;
    pRes.name = "resolution"; pRes.type = "string"; pRes.required = false;
    pRes.description = "1K, 2K, 4K (images) or 720p, 1080p (video)";
    def.parameters.push_back(pRes);

    ToolParameter pFormat;
    pFormat.name = "output_format"; pFormat.type = "string"; pFormat.required = false;
    pFormat.description = "png | jpeg | webp (images only)";
    def.parameters.push_back(pFormat);

    ToolParameter pDuration;
    pDuration.name = "duration"; pDuration.type = "integer"; pDuration.required = false;
    pDuration.description = "Duration in seconds (video only)";
    def.parameters.push_back(pDuration);

    ToolParameter pSound;
    pSound.name = "sound"; pSound.type = "boolean"; pSound.required = false;
    pSound.description = "Generate audio (video only, where supported)";
    def.parameters.push_back(pSound);

    return def;
}

ToolResult DiffusionTool::Execute(const ToolCall& call) {
    auto args = ParseArgs(call.arguments);
    if (args.empty()) {
        ToolResult r;
        r.call_id = call.id;
        r.success = false;
        r.error = "Failed to parse arguments";
        return r;
    }

    std::string action = GetStringField(args, "action");

    if (action == "list_models") {
        return ExecuteListModels(call, args);
    }
    if (action == "generate") {
        return ExecuteGenerate(call, args);
    }

    ToolResult r;
    r.call_id = call.id;
    r.success = false;
    r.error = "Unknown action: " + action + ". Use 'generate' or 'list_models'.";
    return r;
}

ToolResult DiffusionTool::ExecuteListModels(const ToolCall& call, const Json::Value& args) {
    ToolResult result;
    result.call_id = call.id;

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
    result.output = Json::writeString(wb, modelsJson);
    return result;
}

ToolResult DiffusionTool::ExecuteGenerate(const ToolCall& call, const Json::Value& args) {
    ToolResult result;
    result.call_id = call.id;

    std::string prompt = GetStringField(args, "prompt");
    if (prompt.empty()) {
        result.success = false;
        result.error = "prompt is required for generate action";
        return result;
    }

    std::string model = GetStringField(args, "model");
    if (model.empty()) {
        result.success = false;
        result.error = "model is required for generate action. "
                        "Use action=list_models to see available models.";
        return result;
    }

    std::string type = GetStringField(args, "type", "image");
    std::string providerId = GetStringField(args, "provider");

    if (!m_store) {
        result.success = false;
        result.error = "No diffusion store configured";
        return result;
    }

    DiffusionProviderConfig pconfig;
    bool found = false;

    if (!providerId.empty()) {
        auto opt = m_store->GetProvider(providerId);
        if (opt) { pconfig = *opt; found = true; }
    } else {
        auto providers = m_store->ListProviders();
        if (!providers.empty()) { pconfig = providers[0]; found = true; }
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
    genReq.aspect_ratio = GetStringField(args, "aspect_ratio");
    if (genReq.aspect_ratio.empty() && !pconfig.default_aspect_ratio.empty()) {
        genReq.aspect_ratio = pconfig.default_aspect_ratio;
    }
    genReq.resolution = GetStringField(args, "resolution");
    genReq.output_format = GetStringField(args, "output_format", "png");
    genReq.duration = GetIntField(args, "duration");
    genReq.sound = GetBoolField(args, "sound");

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
    result.output = "Generated " + type + " saved to: " + savePath;
    if (genResult.width > 0 && genResult.height > 0) {
        result.output += " (" + std::to_string(genResult.width) + "x"
                       + std::to_string(genResult.height) + ")";
    }
    if (genResult.duration_seconds > 0) {
        result.output += " " + std::to_string(genResult.duration_seconds) + "s";
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