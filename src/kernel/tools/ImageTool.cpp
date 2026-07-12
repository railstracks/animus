#include "animus_kernel/tools/ImageTool.h"
#include "animus_kernel/tools/ImageGenProvider.h"
#include "animus_kernel/tools/ImageResize.h"
#include "animus_kernel/ChainRunner.h"
#include "animus_kernel/AgentStore.h"
#include "animus_kernel/llm/ILLMProvider.h"
#include "animus_kernel/llm/LLMProviderBase.h"

#include <json/json.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace animus::kernel {

// ============================================================================
// Image Generation Provider Implementations
// ============================================================================
// Defined here (not in separate headers) because they're only used by ImageTool.
// ============================================================================

class OllamaImageGenProvider : public IImageGenProvider {
public:
    explicit OllamaImageGenProvider(HttpClient& client,
                                     const ImageGenProviderConfig& cfg)
        : m_client(client), m_config(cfg) {}

    std::string Name() const override { return "ollama"; }
    std::vector<std::string> ListModels() const override {
        return {"x/z-image-turbo", "x/flux2-klein"};
    }

    ImageGenResult Generate(const ImageGenRequest& req,
                             std::string* error) override {
        ImageGenResult result;
        std::string url = m_config.base_url;
        if (url.size() >= 3 && url.substr(url.size() - 3) == "/v1")
            url = url.substr(0, url.size() - 3);
        url += "/api/generate";

        Json::Value body;
        body["model"] = req.model.empty() ? m_config.default_model : req.model;
        body["prompt"] = req.prompt;
        body["stream"] = false;
        if (req.width > 0) body["width"] = req.width;
        if (req.height > 0) body["height"] = req.height;
        if (req.steps > 0) body["steps"] = req.steps;

        Json::StreamWriterBuilder writer;
        std::string bodyStr = Json::writeString(writer, body);

        HttpClient::Request httpReq;
        httpReq.method = "POST";
        httpReq.url = url;
        httpReq.body = bodyStr;
        httpReq.headers["Content-Type"] = "application/json";
        httpReq.timeout_seconds = 120;

        auto resp = m_client.Execute(httpReq);
        if (resp.status_code != 200) {
            if (error) *error = "Ollama image gen failed (HTTP " +
                std::to_string(resp.status_code) + "): " + resp.body;
            result.error = error ? *error : resp.error;
            return result;
        }

        Json::Value root;
        Json::CharReaderBuilder reader;
        std::istringstream stream(resp.body);
        std::string parseErrors;
        if (!Json::parseFromStream(reader, stream, &root, &parseErrors)) {
            auto lastNL = resp.body.rfind('\n');
            std::string lastLine = (lastNL != std::string::npos)
                ? resp.body.substr(lastNL + 1) : resp.body;
            std::istringstream lastStream(lastLine);
            if (!Json::parseFromStream(reader, lastStream, &root, &parseErrors)) {
                if (error) *error = "Failed to parse Ollama response: " + parseErrors;
                result.error = *error;
                return result;
            }
        }

        std::string imageB64 = root.get("image", "").asString();
        if (imageB64.empty()) {
            if (error) *error = "No 'image' field in Ollama response";
            result.error = *error;
            return result;
        }
        result.success = true;
        result.image_data = imageB64;
        result.content_type = "image/png";
        return result;
    }

private:
    HttpClient& m_client;
    ImageGenProviderConfig m_config;
};

class ZaiImageGenProvider : public IImageGenProvider {
public:
    explicit ZaiImageGenProvider(HttpClient& client,
                                  const ImageGenProviderConfig& cfg)
        : m_client(client), m_config(cfg) {}

    std::string Name() const override { return "zai"; }
    std::vector<std::string> ListModels() const override {
        return {"glm-image", "cogview-4-250304"};
    }

    ImageGenResult Generate(const ImageGenRequest& req,
                             std::string* error) override {
        ImageGenResult result;
        std::string url = m_config.base_url;
        if (!url.empty() && url.back() == '/') url.pop_back();
        if (url.size() >= 4 && url.substr(url.size() - 4) == "/api")
            url += "/paas/v4/images/generations";
        else if (url.find("/paas/v4") == std::string::npos)
            url += "/paas/v4/images/generations";
        else
            url += "/images/generations";

        Json::Value body;
        body["model"] = req.model.empty() ? m_config.default_model : req.model;
        body["prompt"] = req.prompt;
        body["size"] = req.size.empty()
            ? (m_config.default_size.empty() ? "1280x1280" : m_config.default_size)
            : req.size;
        if (!req.quality.empty() && req.quality != "auto")
            body["quality"] = req.quality;

        Json::StreamWriterBuilder writer;
        std::string bodyStr = Json::writeString(writer, body);

        HttpClient::Request httpReq;
        httpReq.method = "POST";
        httpReq.url = url;
        httpReq.body = bodyStr;
        httpReq.headers["Content-Type"] = "application/json";
        httpReq.headers["Authorization"] = "Bearer " + m_config.api_key;
        httpReq.timeout_seconds = 120;

        auto resp = m_client.Execute(httpReq);
        if (resp.status_code != 200) {
            if (error) *error = "Z.ai image gen failed (HTTP " +
                std::to_string(resp.status_code) + "): " + resp.body;
            result.error = error ? *error : resp.error;
            return result;
        }

        Json::Value root;
        Json::CharReaderBuilder reader;
        std::istringstream stream(resp.body);
        std::string parseErrors;
        if (!Json::parseFromStream(reader, stream, &root, &parseErrors)) {
            if (error) *error = "Failed to parse Z.ai response: " + parseErrors;
            result.error = *error;
            return result;
        }

        const Json::Value& data = root["data"];
        if (!data.isArray() || data.empty()) {
            if (error) *error = "No image data in Z.ai response";
            result.error = *error;
            return result;
        }
        std::string imageUrl = data[0].get("url", "").asString();
        if (imageUrl.empty()) {
            if (error) *error = "No image URL in Z.ai response";
            result.error = *error;
            return result;
        }
        result.success = true;
        result.image_url = imageUrl;
        result.content_type = "image/png";
        return result;
    }

private:
    HttpClient& m_client;
    ImageGenProviderConfig m_config;
};

class OpenAIImageGenProvider : public IImageGenProvider {
public:
    explicit OpenAIImageGenProvider(HttpClient& client,
                                     const ImageGenProviderConfig& cfg)
        : m_client(client), m_config(cfg) {}

    std::string Name() const override { return "openai"; }
    std::vector<std::string> ListModels() const override {
        return {"gpt-image-2", "gpt-image-1.5", "dall-e-3"};
    }

    ImageGenResult Generate(const ImageGenRequest& req,
                             std::string* error) override {
        ImageGenResult result;
        std::string url = m_config.base_url;
        if (!url.empty() && url.back() == '/') url.pop_back();
        if (url.find("/v1") == std::string::npos)
            url += "/v1/images/generations";
        else
            url += "/images/generations";

        Json::Value body;
        body["model"] = req.model.empty() ? m_config.default_model : req.model;
        body["prompt"] = req.prompt;
        body["size"] = req.size.empty()
            ? (m_config.default_size.empty() ? "1024x1024" : m_config.default_size)
            : req.size;
        body["response_format"] = "b64_json";
        if (!req.quality.empty() && req.quality != "auto")
            body["quality"] = req.quality;

        Json::StreamWriterBuilder writer;
        std::string bodyStr = Json::writeString(writer, body);

        HttpClient::Request httpReq;
        httpReq.method = "POST";
        httpReq.url = url;
        httpReq.body = bodyStr;
        httpReq.headers["Content-Type"] = "application/json";
        httpReq.headers["Authorization"] = "Bearer " + m_config.api_key;
        httpReq.timeout_seconds = 120;

        auto resp = m_client.Execute(httpReq);
        if (resp.status_code != 200) {
            if (error) *error = "OpenAI image gen failed (HTTP " +
                std::to_string(resp.status_code) + "): " + resp.body;
            result.error = error ? *error : resp.error;
            return result;
        }

        Json::Value root;
        Json::CharReaderBuilder reader;
        std::istringstream stream(resp.body);
        std::string parseErrors;
        if (!Json::parseFromStream(reader, stream, &root, &parseErrors)) {
            if (error) *error = "Failed to parse OpenAI response: " + parseErrors;
            result.error = *error;
            return result;
        }

        const Json::Value& data = root["data"];
        if (!data.isArray() || data.empty()) {
            if (error) *error = "No image data in OpenAI response";
            result.error = *error;
            return result;
        }

        std::string b64 = data[0].get("b64_json", "").asString();
        if (!b64.empty()) {
            result.success = true;
            result.image_data = b64;
            result.content_type = "image/png";
        } else {
            std::string imageUrl = data[0].get("url", "").asString();
            if (!imageUrl.empty()) {
                result.success = true;
                result.image_url = imageUrl;
                result.content_type = "image/png";
            } else {
                if (error) *error = "No image data or URL in OpenAI response";
                result.error = *error;
                return result;
            }
        }
        return result;
    }

private:
    HttpClient& m_client;
    ImageGenProviderConfig m_config;
};

// ============================================================================
// Utility functions
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

} // namespace

// ============================================================================
// Construction
// ============================================================================

ImageTool::ImageTool(HttpClient& client, Config config, ChainRunner* chainRunner, AgentStore* agentStore)
    : m_client(client), m_config(std::move(config)), m_chainRunner(chainRunner)
    , m_agentStore(agentStore) {
}

// ============================================================================
// Tool Definition
// ============================================================================

ToolDefinition ImageTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "image";
    def.description =
        "Analyze images with a vision model or generate images from text prompts. "
        "Use action='analyze' to describe/understand images, action='generate' to create images.";
    def.resultMode = ToolResultMode::deliver_to_model;

    {
        ToolParameter action;
        action.name = "action";
        action.type = "string";
        action.required = true;
        action.description = "analyze: describe/analyze images. generate: create an image from a text prompt.";
        action.enum_values = {"analyze", "generate"};
        def.parameters.push_back(action);
    }
    {
        ToolParameter images;
        images.name = "images";
        images.type = "array";
        images.description = "Image file paths or URLs to analyze (1-20 images). Required for analyze.";
        def.parameters.push_back(images);
    }
    {
        ToolParameter prompt;
        prompt.name = "prompt";
        prompt.type = "string";
        prompt.required = true;
        prompt.description = "For analyze: what to look for or describe. For generate: the image generation prompt.";
        def.parameters.push_back(prompt);
    }
    {
        ToolParameter model;
        model.name = "model";
        model.type = "string";
        model.description = "Model override. For analyze: provider/model for vision LLM. For generate: provider/model for generation.";
        def.parameters.push_back(model);
    }
    {
        ToolParameter size;
        size.name = "size";
        size.type = "string";
        size.description = "Generate only: image dimensions (e.g. '1024x1024', '1280x1280').";
        def.parameters.push_back(size);
    }
    {
        ToolParameter quality;
        quality.name = "quality";
        quality.type = "string";
        quality.description = "Generate only: quality level.";
        quality.enum_values = {"auto", "standard", "hd"};
        def.parameters.push_back(quality);
    }
    {
        ToolParameter filename;
        filename.name = "filename";
        filename.type = "string";
        filename.description = "Generate only: output filename. Default: timestamp-based name.";
        def.parameters.push_back(filename);
    }

    return def;
}

// ============================================================================
// Execute — dispatches to analyze or generate
// ============================================================================

ToolResult ImageTool::Execute(const ToolCall& call) {
    ToolResult result;
    result.call_id = call.id;

    auto args = ParseArgs(call.arguments);
    if (args.empty()) {
        result.error = "Failed to parse arguments";
        return result;
    }

    std::string action = GetStringField(args, "action");
    if (action == "analyze") {
        return ExecuteAnalyze(call);
    } else if (action == "generate") {
        return ExecuteGenerate(call);
    } else {
        result.error = "Invalid or missing 'action' parameter. Use 'analyze' or 'generate'.";
        return result;
    }
}

// ============================================================================
// Analyze — vision LLM path
// ============================================================================

ToolResult ImageTool::ExecuteAnalyze(const ToolCall& call) {
    ToolResult result;
    result.call_id = call.id;

    auto args = ParseArgs(call.arguments);
    std::string prompt = GetStringField(args, "prompt", "Describe this image in detail.");

    std::vector<std::string> imagePaths;
    if (args.isMember("images") && args["images"].isArray()) {
        for (const auto& img : args["images"]) {
            if (img.isString()) imagePaths.push_back(img.asString());
        }
    }

    if (imagePaths.empty()) {
        result.error = "No images provided for analysis. Use the 'images' parameter.";
        return result;
    }

    if (static_cast<int>(imagePaths.size()) > m_config.analysis_max_images) {
        result.error = "Too many images. Maximum is " +
            std::to_string(m_config.analysis_max_images) + ".";
        return result;
    }

    // Load and encode images
    std::vector<EncodedImage> encoded;
    for (const auto& path : imagePaths) {
        EncodedImage enc = ImageResize::LoadAndEncode(
            path, m_config.analysis_max_dimension_px, m_config.analysis_max_bytes);
        if (enc.data_uri.empty()) {
            result.error = "Failed to load image: " + path;
            return result;
        }
        encoded.push_back(std::move(enc));
    }

    if (!m_chainRunner) {
        result.error = "LLM provider system not available for image analysis";
        return result;
    }

    // Build multi-modal LLM request
    llm::LLMRequest llmReq;
    llm::LLMMessage userMsg;
    userMsg.role = "user";

    // Add text part
    llm::ContentPart textPart;
    textPart.type = "text";
    textPart.text = prompt;
    userMsg.content_parts.push_back(textPart);

    // Add image parts
    for (const auto& enc : encoded) {
        llm::ContentPart imgPart;
        imgPart.type = "image_url";
        imgPart.image_url = enc.data_uri;
        userMsg.content_parts.push_back(imgPart);
    }

    llmReq.messages.push_back(std::move(userMsg));
    llmReq.stream = false;

    // Determine which vision model to use, in priority order:
    //   1. Explicit model override in tool call args ("provider/model_id")
    //   2. Agent's default_vision_model ("provider/model_id")
    //   3. Fallback: try all providers

    std::string visionModel = GetStringField(args, "model");

    // If no explicit override, check agent's configured vision model
    if (visionModel.empty() && m_agentStore) {
        std::string agentId;
        if (args.isMember("__agent_id") && args["__agent_id"].isString()) {
            agentId = args["__agent_id"].asString();
        }
        if (!agentId.empty()) {
            auto agent = m_agentStore->GetById(agentId);
            if (agent && !agent->default_vision_model.empty()) {
                visionModel = agent->default_vision_model;
            }
        }
    }

    // Use the specified vision model (from override or agent config)
    if (!visionModel.empty()) {
        auto slash = visionModel.find('/');
        if (slash == std::string::npos) {
            result.error = "Vision model must be in 'provider/model_id' format: " + visionModel;
            return result;
        }
        std::string providerId = visionModel.substr(0, slash);
        std::string modelId = visionModel.substr(slash + 1);
        llmReq.model = modelId;

        auto& providers = m_chainRunner->GetProviders();
        auto cfgOpt = m_chainRunner->GetConfigLookup()(providerId);
        if (!cfgOpt) {
            result.error = "Provider not found: " + providerId;
            return result;
        }
        auto providerPtr = providers.Create(*cfgOpt);
        if (!providerPtr) {
            result.error = "Failed to create provider: " + providerId;
            return result;
        }

        std::string llmError;
        auto response = providerPtr->Complete(llmReq, &llmError);
        if (!llmError.empty()) {
            result.error = "Vision LLM error: " + llmError;
            return result;
        }

        result.success = true;
        result.output = response.content;
        return result;
    }

    // Fallback: no vision model configured — try all providers
    auto& providers = m_chainRunner->GetProviders();
    auto& configLookup = m_chainRunner->GetConfigLookup();

    for (const auto& pid : providers.Available()) {
        auto cfg = configLookup(pid);
        if (!cfg) continue;
        auto providerPtr = providers.Create(*cfg);
        if (!providerPtr) continue;

        if (llmReq.model.empty()) llmReq.model = cfg->default_model;

        std::string llmError;
        auto response = providerPtr->Complete(llmReq, &llmError);
        if (!llmError.empty()) {
            std::cerr << "[image] Provider " << pid << " failed: " << llmError << std::endl;
            llmReq.model.clear();
            continue;
        }

        result.success = true;
        result.output = response.content;
        return result;
    }

    result.error = "No vision model configured for this agent. "
                   "Set a default vision model in the agent settings.";
    return result;
}

// ============================================================================
// Generate — image generation path
// ============================================================================

ToolResult ImageTool::ExecuteGenerate(const ToolCall& call) {
    ToolResult result;
    result.call_id = call.id;

    auto args = ParseArgs(call.arguments);
    std::string prompt = GetStringField(args, "prompt");
    if (prompt.empty()) {
        result.error = "Missing 'prompt' parameter for image generation";
        return result;
    }

    std::string modelField = GetStringField(args, "model");
    std::string providerName = m_config.generation_default_provider;
    std::string modelName;

    auto slash = modelField.find('/');
    if (slash != std::string::npos) {
        providerName = modelField.substr(0, slash);
        modelName = modelField.substr(slash + 1);
    } else if (!modelField.empty()) {
        modelName = modelField;
    }

    ImageGenRequest genReq;
    genReq.prompt = prompt;
    genReq.model = modelName;
    genReq.size = GetStringField(args, "size");
    genReq.quality = GetStringField(args, "quality", "auto");
    genReq.filename = GetStringField(args, "filename");

    IImageGenProvider* provider = GetGenProvider(providerName);
    if (!provider) {
        result.error = "Unknown image generation provider: " + providerName +
            ". Available: ollama, zai, openai";
        return result;
    }

    std::string genError;
    auto genResult = provider->Generate(genReq, &genError);
    if (!genResult.success) {
        result.error = genResult.error.empty() ? genError : genResult.error;
        return result;
    }

    EnsureOutputDir();

    std::string savedPath;
    if (!genResult.image_data.empty())
        savedPath = SaveBase64Image(genResult.image_data, genResult.content_type, genReq.filename);
    else if (!genResult.image_url.empty())
        savedPath = DownloadAndSaveImage(genResult.image_url, genReq.filename);

    if (savedPath.empty()) {
        result.error = "Image generated but failed to save";
        return result;
    }

    Json::Value output;
    output["status"] = "generated";
    output["provider"] = providerName;
    output["path"] = savedPath;
    if (!genResult.image_url.empty())
        output["source_url"] = genResult.image_url;

    Json::StreamWriterBuilder writer;
    result.success = true;
    result.output = Json::writeString(writer, output);
    return result;
}

// ============================================================================
// Provider Management
// ============================================================================

IImageGenProvider* ImageTool::GetGenProvider(const std::string& name) {
    if (name == "ollama") {
        if (!m_ollamaGen)
            m_ollamaGen = std::make_unique<OllamaImageGenProvider>(m_client, m_config.gen_ollama);
        return m_ollamaGen.get();
    }
    if (name == "zai") {
        if (!m_zaiGen)
            m_zaiGen = std::make_unique<ZaiImageGenProvider>(m_client, m_config.gen_zai);
        return m_zaiGen.get();
    }
    if (name == "openai") {
        if (!m_openaiGen)
            m_openaiGen = std::make_unique<OpenAIImageGenProvider>(m_client, m_config.gen_openai);
        return m_openaiGen.get();
    }
    return nullptr;
}

// ============================================================================
// File Saving
// ============================================================================

void ImageTool::EnsureOutputDir() {
    std::filesystem::create_directories(m_config.generation_output_dir);
}

std::string ImageTool::SaveBase64Image(const std::string& base64_data,
                                        const std::string& content_type,
                                        const std::string& filename) {
    std::string ext = "png";
    if (content_type.find("jpeg") != std::string::npos) ext = "jpg";
    else if (content_type.find("webp") != std::string::npos) ext = "webp";

    std::string name = filename;
    if (name.empty()) {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        name = "generated_" + std::to_string(ms);
    }
    if (name.find('.') == std::string::npos)
        name += "." + ext;

    std::string fullPath = m_config.generation_output_dir + "/" + name;
    std::string decoded = Base64Decode(base64_data);

    std::ofstream file(fullPath, std::ios::binary);
    if (!file) {
        std::cerr << "[image] Failed to write: " << fullPath << std::endl;
        return {};
    }
    file.write(decoded.data(), decoded.size());
    file.close();
    return fullPath;
}

std::string ImageTool::DownloadAndSaveImage(const std::string& url,
                                             const std::string& filename) {
    HttpClient::Request req;
    req.method = "GET";
    req.url = url;
    req.timeout_seconds = 60;

    auto resp = m_client.Execute(req);
    if (resp.status_code != 200 || resp.body.empty()) {
        std::cerr << "[image] Download failed (HTTP " << resp.status_code
                  << "): " << url << std::endl;
        return {};
    }

    std::string ext = "png";
    if (resp.content_type.find("jpeg") != std::string::npos) ext = "jpg";
    else if (resp.content_type.find("webp") != std::string::npos) ext = "webp";

    std::string name = filename;
    if (name.empty()) {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        name = "generated_" + std::to_string(ms);
    }
    if (name.find('.') == std::string::npos)
        name += "." + ext;

    std::string fullPath = m_config.generation_output_dir + "/" + name;
    std::ofstream file(fullPath, std::ios::binary);
    if (!file) {
        std::cerr << "[image] Failed to write: " << fullPath << std::endl;
        return {};
    }
    file.write(resp.body.data(), resp.body.size());
    file.close();
    return fullPath;
}

// ============================================================================
// Utilities
// ============================================================================

std::string ImageTool::Base64Decode(const std::string& encoded) {
    static const int kTable[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    };

    std::string out;
    out.reserve(encoded.size() * 3 / 4);

    int val = 0, bits = 0;
    for (unsigned char c : encoded) {
        if (c == '=') break;
        int d = kTable[c];
        if (d < 0) continue;
        val = (val << 6) | d;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((val >> bits) & 0xFF);
        }
    }
    return out;
}

} // namespace animus::kernel
