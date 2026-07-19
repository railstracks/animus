#include "animus_kernel/DiffusionProvider.h"
#include "animus_kernel/tools/HttpClient.h"
#include "animus_kernel/tools/Base64.h"

#include <json/json.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <thread>

namespace animus::kernel {

// ============================================================================
// GetImgProvider — GetImg.ai diffusion API implementation
// ============================================================================

class GetImgProvider : public IDiffusionProvider {
public:
    GetImgProvider(HttpClient& client, const DiffusionProviderConfig& config)
        : m_client(client), m_config(config) {}

    // --- IDiffusionProvider ---

    DiffusionGenResult Generate(const DiffusionGenRequest& req,
                                std::string* error) override {
        if (req.type == "video") {
            return GenerateVideo(req, error);
        }
        return GenerateImage(req, error);
    }

    bool PollStatus(const std::string& generationId,
                    DiffusionGenResult& outResult,
                    std::string* error) override {
        HttpClient::Request httpReq;
        httpReq.method = "GET";
        httpReq.url = m_config.base_url + "/v2/videos/generations/" + generationId;
        httpReq.headers["Authorization"] = "Bearer " + m_config.api_key;
        httpReq.timeout_seconds = 30;

        auto resp = m_client.Execute(httpReq);
        if (resp.status_code != 200) {
            if (error) *error = "Poll failed (status " + std::to_string(resp.status_code) + "): " + resp.body;
            return false;
        }

        Json::Value data = ParseJson(resp.body);
        std::string status = GetString(data, "status");

        if (status == "completed") {
            outResult.success = true;
            outResult.async = false;  // polling done
            if (data.isMember("data") && data["data"].isArray() && data["data"].size() > 0) {
                auto& item = data["data"][0];
                outResult.media_url = GetString(item, "url");
                outResult.content_type = GetString(item, "mime_type");
                outResult.width = item.isMember("width") ? item["width"].asInt() : 0;
                outResult.height = item.isMember("height") ? item["height"].asInt() : 0;
                outResult.duration_seconds = item.isMember("duration") ? item["duration"].asInt() : 0;
            }
            return true;
        }

        if (status == "failed") {
            outResult.success = false;
            if (data.isMember("error") && data["error"].isMember("message")) {
                if (error) *error = GetString(data["error"], "message");
            } else if (error) {
                *error = "Generation failed";
            }
            return true;  // terminal state
        }

        return false;  // still pending
    }

    std::vector<DiffusionModel> ListModels() override {
        std::vector<DiffusionModel> models;
        HttpClient::Request req;
        req.method = "GET";
        req.url = m_config.base_url + "/v2/models";
        req.headers["Authorization"] = "Bearer " + m_config.api_key;
        req.timeout_seconds = 30;

        auto resp = m_client.Execute(req);
        if (resp.status_code != 200) {
            std::cerr << "[diffusion] GetImg ListModels failed (status "
                      << resp.status_code << ")" << std::endl;
            return models;
        }

        Json::Value data = ParseJson(resp.body);
        if (data.isArray()) {
            for (const auto& m : data) {
                DiffusionModel model;
                model.id = GetString(m, "id");
                model.type = GetString(m, "type");
                model.name = GetString(m, "name");
                if (!model.id.empty()) {
                    models.push_back(std::move(model));
                }
            }
        }

        return models;
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

        // Create parent directory if needed
        auto parent = std::filesystem::path(savePath).parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }

        std::ofstream file(savePath, std::ios::binary);
        if (!file) {
            if (error) *error = "Cannot open file for writing: " + savePath;
            return false;
        }
        file.write(resp.body.data(), resp.body.size());
        file.close();
        return true;
    }

    std::string Name() const override {
        return m_config.id;
    }

    bool TestConnection(std::string* error) override {
        HttpClient::Request req;
        req.method = "GET";
        req.url = m_config.base_url + "/v2/models";
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
        result.async = false;

        Json::Value body;
        body["model"] = req.model;
        body["prompt"] = req.prompt;
        if (!req.aspect_ratio.empty()) body["aspect_ratio"] = req.aspect_ratio;
        if (!req.resolution.empty()) body["resolution"] = req.resolution;
        if (!req.output_format.empty()) body["output_format"] = req.output_format;

        Json::StreamWriterBuilder wb;
        wb.settings_["indentation"] = "";
        std::string bodyStr = Json::writeString(wb, body);

        HttpClient::Request httpReq;
        httpReq.method = "POST";
        httpReq.url = m_config.base_url + "/v2/images/generations";
        httpReq.headers["Authorization"] = "Bearer " + m_config.api_key;
        httpReq.headers["Content-Type"] = "application/json";
        httpReq.body = bodyStr;
        httpReq.timeout_seconds = 120;

        auto resp = m_client.Execute(httpReq);
        if (resp.status_code != 200) {
            result.success = false;
            result.error = "Image generation failed (status " + std::to_string(resp.status_code) + "): " + resp.body;
            if (error) *error = result.error;
            return result;
        }

        Json::Value data = ParseJson(resp.body);
        std::string status = GetString(data, "status");
        if (status != "completed") {
            result.success = false;
            result.error = "Unexpected status: " + status;
            if (error) *error = result.error;
            return result;
        }

        if (data.isMember("data") && data["data"].isArray() && data["data"].size() > 0) {
            auto& item = data["data"][0];
            result.media_url = GetString(item, "url");
            result.content_type = GetString(item, "mime_type");
            result.width = item.isMember("width") ? item["width"].asInt() : 0;
            result.height = item.isMember("height") ? item["height"].asInt() : 0;
        }

        result.success = true;
        return result;
    }

    DiffusionGenResult GenerateVideo(const DiffusionGenRequest& req, std::string* error) {
        DiffusionGenResult result;
        result.async = true;

        Json::Value body;
        body["model"] = req.model;
        body["prompt"] = req.prompt;
        if (!req.aspect_ratio.empty()) body["aspect_ratio"] = req.aspect_ratio;
        if (!req.resolution.empty()) body["resolution"] = req.resolution;
        if (req.duration > 0) body["duration"] = req.duration;
        body["sound"] = req.sound;

        Json::StreamWriterBuilder wb;
        wb.settings_["indentation"] = "";
        std::string bodyStr = Json::writeString(wb, body);

        HttpClient::Request httpReq;
        httpReq.method = "POST";
        httpReq.url = m_config.base_url + "/v2/videos/generations";
        httpReq.headers["Authorization"] = "Bearer " + m_config.api_key;
        httpReq.headers["Content-Type"] = "application/json";
        httpReq.body = bodyStr;
        httpReq.timeout_seconds = 60;

        auto resp = m_client.Execute(httpReq);
        if (resp.status_code != 202) {
            result.success = false;
            result.error = "Video submission failed (status " + std::to_string(resp.status_code) + "): " + resp.body;
            if (error) *error = result.error;
            return result;
        }

        Json::Value data = ParseJson(resp.body);
        result.generation_id = GetString(data, "id");
        result.success = true;
        return result;
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

    HttpClient& m_client;
    DiffusionProviderConfig m_config;
};

} // namespace animus::kernel

// Factory function for DiffusionTool
namespace animus::kernel {
std::unique_ptr<IDiffusionProvider> CreateGetImgProvider(
    HttpClient& client, const DiffusionProviderConfig& config) {
    return std::make_unique<GetImgProvider>(client, config);
}
} // namespace animus::kernel