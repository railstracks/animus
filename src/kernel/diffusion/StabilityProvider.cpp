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
        // Phase 1: image generation only
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
        httpReq.headers["Accept"] = "application/json";
        httpReq.timeout_seconds = 30;

        auto resp = m_client.Execute(httpReq);
        if (resp.status_code == 202) {
            return false; // still pending
        }
        if (resp.status_code != 200) {
            if (error) *error = "Poll failed (status " + std::to_string(resp.status_code) + "): " + resp.body;
            return false;
        }

        Json::Value data = ParseJson(resp.body);
        outResult.success = true;
        outResult.finish_reason = GetString(data, "finish_reason");
        if (data.isMember("image")) {
            outResult.file_data = GetString(data, "image"); // base64
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
        // Stability has no dedicated health endpoint — try listing account balance
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
    DiffusionGenResult GenerateImage(const DiffusionGenRequest& req, std::string* error) {
        DiffusionGenResult result;

        // Determine endpoint based on model
        std::string endpoint = GetEndpointForModel(req.model);
        if (endpoint.empty()) {
            result.success = false;
            result.error = "Unknown Stability model: " + req.model;
            if (error) *error = result.error;
            return result;
        }

        // Build multipart form data
        MultipartFormData form;
        form.AddField("prompt", req.prompt);

        if (!req.aspect_ratio.empty()) {
            form.AddField("aspect_ratio", req.aspect_ratio);
        }
        if (!req.output_format.empty()) {
            form.AddField("output_format", req.output_format);
        } else {
            form.AddField("output_format", "png");
        }
        if (!req.negative_prompt.empty()) {
            form.AddField("negative_prompt", req.negative_prompt);
        }
        if (!req.style_preset.empty()) {
            form.AddField("style_preset", req.style_preset);
        }
        if (req.seed > 0) {
            form.AddField("seed", std::to_string(req.seed));
        }

        // Image-to-image support
        if (!req.input_image_path.empty()) {
            std::string ct = "image/png";
            if (req.input_image_path.size() > 4) {
                std::string ext = req.input_image_path.substr(req.input_image_path.size() - 4);
                if (ext == ".jpg" || ext == "jpeg") ct = "image/jpeg";
                else if (ext == ".webp") ct = "image/webp";
            }
            form.AddFileFromPath("image", req.input_image_path, ct);
            if (req.strength > 0.0) {
                form.AddField("strength", std::to_string(req.strength));
            }
        }

        // For SD 3.5, model field selects the variant
        if (req.model.find("sd3.5") != std::string::npos) {
            form.AddField("model", req.model);
        }

        std::string body = form.Build();

        HttpClient::Request httpReq;
        httpReq.method = "POST";
        httpReq.url = m_config.base_url + endpoint;
        httpReq.headers["Authorization"] = "Bearer " + m_config.api_key;
        httpReq.headers["Content-Type"] = form.ContentType();
        httpReq.headers["Accept"] = "application/json"; // base64 response
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

        if (data.isMember("image")) {
            result.file_data = GetString(data, "image"); // base64-encoded
        }

        result.success = true;
        return result;
    }

    std::string GetEndpointForModel(const std::string& model) const {
        if (model == "ultra" || model == "stable-image-ultra")
            return "/v2beta/stable-image/generate/ultra";
        if (model == "core" || model == "stable-image-core")
            return "/v2beta/stable-image/generate/core";
        if (model == "sd3.5-large" || model == "sd3.5-large-turbo"
            || model == "sd3.5-medium" || model == "sd3.5-flash"
            || model.rfind("sd3.5", 0) == 0)
            return "/v2beta/stable-image/generate/sd3.5";
        return "";
    }

    // Helpers
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
};

} // namespace animus::kernel

// Factory function for DiffusionTool
namespace animus::kernel {
std::unique_ptr<IDiffusionProvider> CreateStabilityProvider(
    HttpClient& client, const DiffusionProviderConfig& config) {
    return std::make_unique<StabilityProvider>(client, config);
}
} // namespace animus::kernel