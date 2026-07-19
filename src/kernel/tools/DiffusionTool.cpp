#include "animus_kernel/tools/DiffusionTool.h"
#include "animus_kernel/DiffusionCapabilities.h"

#include <json/json.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <thread>
#include <random>
#include <iomanip>
#include <sstream>
#include <cstdint>

namespace animus::kernel {

// Factory functions implemented in provider .cpp files
std::unique_ptr<IDiffusionProvider> CreateGetImgProvider(
    HttpClient& client, const DiffusionProviderConfig& config);
std::unique_ptr<IDiffusionProvider> CreateStabilityProvider(
    HttpClient& client, const DiffusionProviderConfig& config);

// ============================================================================
// Base64 decoding (for providers that return base64-encoded media)
// ============================================================================

namespace {

std::string Base64Decode(const std::string& encoded) {
    static const std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[chars[i]] = i;

    int val = 0, valb = -8;
    for (unsigned char c : encoded) {
        if (c == '=') break;
        if (T[c] == -1) continue;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

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

double GetDoubleField(const Json::Value& v, const std::string& key, double def = 0.0) {
    if (v.isMember(key) && v[key].isDouble()) return v[key].asDouble();
    if (v.isMember(key) && v[key].isInt()) return static_cast<double>(v[key].asInt());
    return def;
}

} // namespace

// ============================================================================
// Construction
// ============================================================================

DiffusionTool::DiffusionTool(HttpClient& client,
                             DiffusionStore* store,
                             Config config)
    : m_client(client), m_ownClient(), m_store(store), m_config(std::move(config)) {
    // Dedicated HTTP client for generation requests — base64 images can be several MB
    m_ownClient.SetMaxBodySize(50 * 1024 * 1024);  // 50MB
    m_ownClient.SetTlsVerify(true);
    EnsureOutputDir();
}

ToolDefinition DiffusionTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "diffusion";
    def.description = "Generate images, video, audio, or 3D models using diffusion models. "
                      "Different models produce different visual styles — use list_models "
                      "to see available options across all configured providers.";

    ToolParameter pAction;
    pAction.name = "action"; pAction.type = "string"; pAction.required = true;
    pAction.description = "generate | list_models";
    pAction.enum_values = {"generate", "list_models"};
    def.parameters.push_back(pAction);

    ToolParameter pType;
    pType.name = "type"; pType.type = "string"; pType.required = false;
    pType.description = "image | video | audio | 3d (default: image)";
    pType.enum_values = {"image", "video", "audio", "3d"};
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
    pProvider.description = "Which configured diffusion provider to use (e.g. 'getimg', 'stability-1')";
    def.parameters.push_back(pProvider);

    ToolParameter pAspect;
    pAspect.name = "aspect_ratio"; pAspect.type = "string"; pAspect.required = false;
    pAspect.description = "1:1, 16:9, 9:16, 2:3, 3:2, 4:5, 5:4, 21:9, 9:21";
    def.parameters.push_back(pAspect);

    ToolParameter pRes;
    pRes.name = "resolution"; pRes.type = "string"; pRes.required = false;
    pRes.description = "1K, 2K, 4K (images) or 720p, 1080p (video)";
    def.parameters.push_back(pRes);

    ToolParameter pFormat;
    pFormat.name = "output_format"; pFormat.type = "string"; pFormat.required = false;
    pFormat.description = "png | jpeg | webp (images), mp3 | wav (audio), glb (3D)";
    def.parameters.push_back(pFormat);

    ToolParameter pDuration;
    pDuration.name = "duration"; pDuration.type = "integer"; pDuration.required = false;
    pDuration.description = "Duration in seconds (video/audio only)";
    def.parameters.push_back(pDuration);

    ToolParameter pSound;
    pSound.name = "sound"; pSound.type = "boolean"; pSound.required = false;
    pSound.description = "Generate audio (video only, where supported)";
    def.parameters.push_back(pSound);

    // Extended parameters (Ticket 124)
    ToolParameter pNegative;
    pNegative.name = "negative_prompt"; pNegative.type = "string"; pNegative.required = false;
    pNegative.description = "Keywords of what to exclude from the output (Stability only)";
    def.parameters.push_back(pNegative);

    ToolParameter pStyle;
    pStyle.name = "style_preset"; pStyle.type = "string"; pStyle.required = false;
    pStyle.description = "Style guide: 3d-model, analog-film, anime, cinematic, "
                         "comic-book, digital-art, enhance, fantasy-art, isometric, "
                         "line-art, low-poly, modeling-compound, neon-punk, origami, "
                         "photographic, pixel-art, tile-texture (Stability only)";
    def.parameters.push_back(pStyle);

    ToolParameter pSeed;
    pSeed.name = "seed"; pSeed.type = "integer"; pSeed.required = false;
    pSeed.description = "Random seed for reproducibility (0 = random, Stability only)";
    def.parameters.push_back(pSeed);

    ToolParameter pStrength;
    pStrength.name = "strength"; pStrength.type = "number"; pStrength.required = false;
    pStrength.description = "Image-to-image influence: 0=identical to input, 1=ignore input (Stability only)";
    def.parameters.push_back(pStrength);

    ToolParameter pInputImage;
    pInputImage.name = "input_image"; pInputImage.type = "string"; pInputImage.required = false;
    pInputImage.description = "Path to input image (for image-to-image, 3D from image, upscale)";
    def.parameters.push_back(pInputImage);

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

        auto caps = provider->GetCapabilities();
        auto models = provider->ListModels();
        for (const auto& m : models) {
            Json::Value modelObj;
            modelObj["id"] = m.id;
            modelObj["type"] = m.type;
            modelObj["name"] = m.name;
            modelObj["provider"] = pconfig.id;

            // Build capability list
            Json::Value capsArr(Json::arrayValue);
            if (HasCapability(m.capabilities, ImageGeneration)) capsArr.append("image_generation");
            if (HasCapability(m.capabilities, VideoGeneration)) capsArr.append("video_generation");
            if (HasCapability(m.capabilities, AudioGeneration)) capsArr.append("audio_generation");
            if (HasCapability(m.capabilities, Model3DGeneration)) capsArr.append("3d_generation");
            if (HasCapability(m.capabilities, ImageToImage)) capsArr.append("image_to_image");
            if (HasCapability(m.capabilities, NegativePrompt)) capsArr.append("negative_prompt");
            if (HasCapability(m.capabilities, StylePreset)) capsArr.append("style_preset");
            if (HasCapability(m.capabilities, SeedControl)) capsArr.append("seed_control");
            modelObj["capabilities"] = capsArr;

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

    // Extended parameters (only sent if provider supports them)
    auto caps = provider->GetCapabilities();
    if (HasCapability(caps, NegativePrompt)) {
        genReq.negative_prompt = GetStringField(args, "negative_prompt");
    }
    if (HasCapability(caps, StylePreset)) {
        genReq.style_preset = GetStringField(args, "style_preset");
    }
    if (HasCapability(caps, SeedControl)) {
        genReq.seed = static_cast<int64_t>(GetIntField(args, "seed"));
    }
    if (HasCapability(caps, ImageToImage)) {
        genReq.input_image_path = GetStringField(args, "input_image");
        genReq.strength = GetDoubleField(args, "strength");
    }

    // Generate
    std::string genError;
    auto genResult = provider->Generate(genReq, &genError);

    if (!genResult.success) {
        result.success = false;
        result.error = genResult.error.empty() ? genError : genResult.error;
        return result;
    }

    // For async (video/audio): poll until complete
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
                genResult.file_data = pollResult.file_data;
                genResult.content_type = pollResult.content_type;
                genResult.width = pollResult.width;
                genResult.height = pollResult.height;
                genResult.duration_seconds = pollResult.duration_seconds;
                break;
            }
        }

        if (genResult.media_url.empty() && genResult.file_data.empty()) {
            result.success = false;
            result.error = "Generation timed out after " + std::to_string(timeout) + "s";
            return result;
        }
    }

    // Save the generated media
    std::string ext = (type == "video") ? "mp4"
                    : (type == "audio") ? "mp3"
                    : (type == "3d") ? "glb"
                    : genReq.output_format;
    if (ext.empty()) ext = "png";
    std::string filename = GenerateFilename(type, ext);
    std::string savePath = EnsureOutputDir() + "/" + filename;

    // If we have base64 data, decode and save directly
    if (!genResult.file_data.empty()) {
        std::string decoded = Base64Decode(genResult.file_data);
        std::ofstream file(savePath, std::ios::binary);
        if (!file) {
            result.success = false;
            result.error = "Cannot open file for writing: " + savePath;
            return result;
        }
        file.write(decoded.data(), decoded.size());
        file.close();
    } else if (!genResult.media_url.empty()) {
        // Download from URL
        std::string dlError;
        if (!provider->DownloadMedia(genResult.media_url, savePath, &dlError)) {
            result.success = false;
            result.error = "Generation succeeded but download failed: " + dlError;
            return result;
        }
    } else {
        result.success = false;
        result.error = "Generation succeeded but no media URL or file data returned";
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
    if (!genResult.finish_reason.empty() && genResult.finish_reason != "SUCCESS") {
        result.output += " [finish: " + genResult.finish_reason + "]";
    }
    return result;
}

std::unique_ptr<IDiffusionProvider> DiffusionTool::CreateProvider(
    const DiffusionProviderConfig& config) {
    if (config.type == "getimg") {
        return CreateGetImgProvider(m_ownClient, config);
    }
    if (config.type == "stability") {
        return CreateStabilityProvider(m_ownClient, config);
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

bool DiffusionTool::SaveBase64(const std::string& base64Data, const std::string& savePath) {
    std::string decoded = Base64Decode(base64Data);
    std::ofstream file(savePath, std::ios::binary);
    if (!file) return false;
    file.write(decoded.data(), decoded.size());
    file.close();
    return true;
}

} // namespace animus::kernel
