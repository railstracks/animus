#include "animus_kernel/DiffusionProvider.h"
#include "animus_kernel/DiffusionCapabilities.h"
#include "animus_kernel/tools/HttpClient.h"
#include "animus_kernel/tools/MultipartFormData.h"

#include <json/json.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <thread>
#include <sstream>

namespace animus::kernel {

// ============================================================================
// StabilityProvider — Stability AI diffusion API implementation
// Supports v2beta (Ultra/Core/SD3) and v1 (SDXL 1.0) API styles
// ============================================================================

class StabilityProvider : public IDiffusionProvider {
public:
    StabilityProvider(HttpClient& client, const DiffusionProviderConfig& config)
        : m_client(client), m_config(config) {}

    // --- IDiffusionProvider ---

    DiffusionCapabilities GetCapabilities() const override {
        return ImageGeneration | ImageToImage | NegativePrompt | StylePreset | SeedControl;
    }

    DiffusionGenResult Generate(const DiffusionGenRequest& req,
                                std::string* error) override {
        if (req.type != "image" && req.type != "3d") {
            if (error) *error = "Stability provider currently supports image generation only";
            DiffusionGenResult r;
            r.success = false;
            r.error = *error;
            return r;
        }
        return GenerateImage(req, error);
    }

    bool PollStatus(const std::string& generationId,
                    DiffusionGenResult& outResult,
                    std::string* error) override {
        HttpClient::Request httpReq;
        httpReq.method = "GET";
        httpReq.url = m_config.base_url + "/v2beta/stable-image/results/" + generationId;
        httpReq.headers["Authorization"] = "Bearer " + m_config.api_key;
        httpReq.headers["accept"] = "application/json";
        httpReq.timeout_seconds = 30;

        auto resp = m_client.Execute(httpReq);
        if (resp.status_code == 202) return false; // still pending
        if (resp.status_code != 200) {
            if (error) *error = "Poll failed (status " + std::to_string(resp.status_code) + "): " + resp.body;
            return false;
        }

        Json::Value data = ParseJson(resp.body);
        outResult.success = true;
        outResult.finish_reason = GetString(data, "finish_reason");
        if (data.isMember("image")) {
            outResult.file_data = GetString(data, "image");
        }
        return true;
    }

    std::vector<DiffusionModel> ListModels() override {
        return s_stabilityModels;
    }

    bool DownloadMedia(const std::string& url,
                       const std::string& savePath,
                       std::string* error) override {
        HttpClient::Request req;
        req.method = "GET";
        req.url = url;
        req.timeout_seconds = 120;
        req.follow_redirects = true;

        auto resp = m_client.Execute(req);
        if (resp.status_code != 200) {
            if (error) *error = "Download failed (status " + std::to_string(resp.status_code) + ")";
            return false;
        }

        auto parent = std::filesystem::path(savePath).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);

        std::ofstream file(savePath, std::ios::binary);
        if (!file) {
            if (error) *error = "Cannot open file for writing: " + savePath;
            return false;
        }
        file.write(resp.body.data(), resp.body.size());
        file.close();
        return true;
    }

    std::string Name() const override { return m_config.id; }

    bool TestConnection(std::string* error) override {
        HttpClient::Request req;
        req.method = "GET";
        req.url = m_config.base_url + "/v1/user/balance";
        req.headers["Authorization"] = "Bearer " + m_config.api_key;
        req.timeout_seconds = 15;

        auto resp = m_client.Execute(req);
        if (resp.status_code == 200) return true;
        if (error) *error = "Connection test failed (status " + std::to_string(resp.status_code) + "): " + resp.body;
        return false;
    }

private:
    // ========================================================================
    // Image generation — dispatches to v2beta or v1 based on model
    // ========================================================================

    DiffusionGenResult GenerateImage(const DiffusionGenRequest& req, std::string* error) {
        // v1 models (SDXL) use JSON API
        if (IsV1Model(req.model)) {
            return GenerateImageV1(req, error);
        }
        // v2beta models (Ultra/Core/SD3) use multipart API
        return GenerateImageV2(req, error);
    }

    // --- v2beta API (Ultra, Core, SD 3.5) — multipart/form-data ---

    DiffusionGenResult GenerateImageV2(const DiffusionGenRequest& req, std::string* error) {
        DiffusionGenResult result;

        std::string endpoint = GetV2Endpoint(req.model);
        if (endpoint.empty()) {
            result.success = false;
            result.error = "Unknown Stability model: " + req.model;
            if (error) *error = result.error;
            return result;
        }

        MultipartFormData form;
        form.AddField("prompt", req.prompt);

        if (!req.aspect_ratio.empty())
            form.AddField("aspect_ratio", req.aspect_ratio);

        std::string fmt = req.output_format.empty() ? "png" : req.output_format;
        form.AddField("output_format", fmt);

        if (!req.negative_prompt.empty())
            form.AddField("negative_prompt", req.negative_prompt);
        if (!req.style_preset.empty())
            form.AddField("style_preset", req.style_preset);
        if (req.seed > 0)
            form.AddField("seed", std::to_string(req.seed));

        // Image-to-image
        if (!req.input_image_path.empty()) {
            std::string ct = "image/png";
            if (req.input_image_path.size() > 4) {
                std::string ext = req.input_image_path.substr(req.input_image_path.size() - 4);
                if (ext == ".jpg" || ext == "jpeg") ct = "image/jpeg";
                else if (ext == ".webp") ct = "image/webp";
            }
            form.AddFileFromPath("image", req.input_image_path, ct);
            if (req.strength > 0.0)
                form.AddField("strength", std::to_string(req.strength));
        }

        // SD 3.5 variants: model field selects the variant
        if (req.model.rfind("sd3.5", 0) == 0 || req.model.rfind("sd3", 0) == 0)
            form.AddField("model", req.model);

        std::string body = form.Build();

        std::cerr << "[stability] v2 endpoint: " << endpoint << std::endl;
        std::cerr << "[stability] content-type: " << form.ContentType() << std::endl;
        std::cerr << "[stability] body size: " << body.size() << std::endl;

        HttpClient::Request httpReq;
        httpReq.method = "POST";
        httpReq.url = m_config.base_url + endpoint;
        httpReq.headers["Authorization"] = "Bearer " + m_config.api_key;
        httpReq.headers["Content-Type"] = form.ContentType();
        httpReq.headers["accept"] = "application/json";
        httpReq.body = body;
        httpReq.timeout_seconds = 120;

        auto resp = m_client.Execute(httpReq);
        if (resp.status_code != 200) {
            result.success = false;
            result.error = "Stability generation failed (status "
                         + std::to_string(resp.status_code) + "): " + resp.body;
            if (error) *error = result.error;
            return result;
        }

        Json::Value data = ParseJson(resp.body);
        result.finish_reason = GetString(data, "finish_reason");
        if (data.isMember("image"))
            result.file_data = GetString(data, "image");

        result.success = true;
        return result;
    }

    // --- v1 API (SDXL 1.0) — application/json ---

    DiffusionGenResult GenerateImageV1(const DiffusionGenRequest& req, std::string* error) {
        DiffusionGenResult result;

        std::string engineId = GetV1EngineId(req.model);
        std::string endpoint = "/v1/generation/" + engineId + "/text-to-image";

        // Build JSON body
        Json::Value body;
        Json::Value textPrompts(Json::arrayValue);
        Json::Value promptObj;
        promptObj["text"] = req.prompt;
        promptObj["weight"] = 1.0;
        textPrompts.append(promptObj);
        body["text_prompts"] = textPrompts;

        // Dimensions: convert aspect_ratio to height/width
        int height = 1024, width = 1024;
        if (!req.aspect_ratio.empty()) {
            ParseAspectRatio(req.aspect_ratio, height, width);
        }
        body["height"] = height;
        body["width"] = width;

        body["samples"] = 1;
        body["steps"] = 30;

        if (req.cfg_scale > 0.0)
            body["cfg_scale"] = req.cfg_scale;
        else
            body["cfg_scale"] = 7;

        if (!req.negative_prompt.empty()) {
            Json::Value negPrompt;
            negPrompt["text"] = req.negative_prompt;
            negPrompt["weight"] = -1.0;
            textPrompts.append(negPrompt);
            body["text_prompts"] = textPrompts;
        }

        if (!req.style_preset.empty())
            body["style_preset"] = req.style_preset;
        if (req.seed > 0)
            body["seed"] = static_cast<Json::UInt64>(req.seed);

        Json::StreamWriterBuilder wb;
        wb.settings_["indentation"] = "";
        std::string bodyStr = Json::writeString(wb, body);

        std::cerr << "[stability] v1 endpoint: " << endpoint << std::endl;
        std::cerr << "[stability] body: " << bodyStr.substr(0, 200) << std::endl;

        HttpClient::Request httpReq;
        httpReq.method = "POST";
        httpReq.url = m_config.base_url + endpoint;
        httpReq.headers["Authorization"] = "Bearer " + m_config.api_key;
        httpReq.headers["Content-Type"] = "application/json";
        httpReq.headers["Accept"] = "application/json";
        httpReq.body = bodyStr;
        httpReq.timeout_seconds = 120;

        auto resp = m_client.Execute(httpReq);
        if (resp.status_code != 200) {
            result.success = false;
            result.error = "Stability v1 generation failed (status "
                         + std::to_string(resp.status_code) + "): " + resp.body;
            if (error) *error = result.error;
            return result;
        }

        // v1 response: { "artifacts": [{ "base64": "...", "finishReason": "SUCCESS", "seed": 123 }] }
        Json::Value data = ParseJson(resp.body);
        if (data.isMember("artifacts") && data["artifacts"].isArray() && data["artifacts"].size() > 0) {
            auto& artifact = data["artifacts"][0];
            result.file_data = GetString(artifact, "base64");
            result.finish_reason = GetString(artifact, "finishReason");
        }

        result.success = true;
        return result;
    }

    // --- Model classification helpers ---

    bool IsV1Model(const std::string& model) const {
        return model.rfind("sdxl", 0) == 0
            || model.rfind("stable-diffusion-xl", 0) == 0;
    }

    std::string GetV1EngineId(const std::string& model) const {
        if (model == "sdxl-1.0" || model == "sdxl")
            return "stable-diffusion-xl-1024-v1-0";
        if (model.rfind("stable-diffusion-xl", 0) == 0)
            return model;
        // Default: assume it's an engine ID
        return "stable-diffusion-xl-1024-v1-0";
    }

    std::string GetV2Endpoint(const std::string& model) const {
        if (model == "ultra" || model.rfind("ultra", 0) == 0)
            return "/v2beta/stable-image/generate/ultra";
        if (model == "core" || model.rfind("core", 0) == 0)
            return "/v2beta/stable-image/generate/core";
        if (model.rfind("sd3.5", 0) == 0 || model.rfind("sd3", 0) == 0)
            return "/v2beta/stable-image/generate/sd3";
        return "";
    }

    void ParseAspectRatio(const std::string& ratio, int& h, int& w) const {
        // SDXL supports specific dimension combinations
        if (ratio == "1:1")      { h = 1024; w = 1024; }
        else if (ratio == "16:9")  { h = 768;  w = 1344; }
        else if (ratio == "9:16")  { h = 1344; w = 768;  }
        else if (ratio == "3:2")   { h = 896;  w = 1152; }
        else if (ratio == "2:3")   { h = 1152; w = 896;  }
        else if (ratio == "4:5")   { h = 1152; w = 960;  }
        else if (ratio == "5:4")   { h = 960;  w = 1152; }
        else if (ratio == "21:9")  { h = 640;  w = 1536; }
        else if (ratio == "9:21")  { h = 1536; w = 640;  }
        else                        { h = 1024; w = 1024; }
    }

    // --- Helpers ---

    static Json::Value ParseJson(const std::string& str) {
        Json::Value root;
        Json::CharReaderBuilder rb;
        std::string errs;
        std::istringstream stream(str);
        Json::parseFromStream(rb, stream, &root, &errs);
        return root;
    }

    static std::string GetString(const Json::Value& v, const std::string& key) {
        if (v.isMember(key) && !v[key].isNull()) return v[key].asString();
        return "";
    }

    static std::vector<DiffusionModel> s_stabilityModels;

    HttpClient& m_client;
    DiffusionProviderConfig m_config;
};

// Static model list — Stability has no models endpoint
std::vector<DiffusionModel> StabilityProvider::s_stabilityModels = {
    // v2beta models
    {"ultra",             "image", "Stable Image Ultra",
     ImageGeneration | ImageToImage | NegativePrompt | StylePreset | SeedControl},
    {"core",              "image", "Stable Image Core",
     ImageGeneration | NegativePrompt | StylePreset | SeedControl},
    {"sd3.5-large",       "image", "SD 3.5 Large",
     ImageGeneration | ImageToImage | NegativePrompt | SeedControl},
    {"sd3.5-large-turbo", "image", "SD 3.5 Large Turbo",
     ImageGeneration | ImageToImage | NegativePrompt | SeedControl},
    {"sd3.5-medium",      "image", "SD 3.5 Medium",
     ImageGeneration | ImageToImage | NegativePrompt | SeedControl},
    {"sd3.5-flash",       "image", "SD 3.5 Flash",
     ImageGeneration | ImageToImage | NegativePrompt | SeedControl},
    // v1 model
    {"sdxl-1.0",          "image", "SDXL 1.0",
     ImageGeneration | NegativePrompt | StylePreset | SeedControl},
};

} // namespace animus::kernel

// Factory function for DiffusionTool
namespace animus::kernel {
std::unique_ptr<IDiffusionProvider> CreateStabilityProvider(
    HttpClient& client, const DiffusionProviderConfig& config) {
    return std::make_unique<StabilityProvider>(client, config);
}
} // namespace animus::kernel