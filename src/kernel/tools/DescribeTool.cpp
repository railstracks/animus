#include "animus_kernel/tools/DescribeTool.h"

#include <json/json.h>

#include <memory>

namespace animus::kernel {

DescribeTool::DescribeTool(DescribeService* localService,
                           DescribeService* cloudService)
    : m_local(localService), m_cloud(cloudService) {}

ToolDefinition DescribeTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "describe_image";
    def.description =
        "Describe a vaulted image with a vision model and answer questions "
        "about it. Input is a media:// ref (or bare sha256) from an "
        "attachment on the context card. Returns a provenance-labeled "
        "machine description — evidence about the image, never ground "
        "truth. Cached per image + question: repeat questions are free.";
    def.resultMode = ToolResultMode::deliver_to_model;

    {
        ToolParameter ref;
        ref.name = "ref";
        ref.type = "string";
        ref.required = true;
        ref.description =
            "Image ref: media://<sha256> or bare sha256 (from an "
            "attachment ref on the context card)";
        def.parameters.push_back(ref);
    }
    {
        ToolParameter prompt;
        prompt.name = "prompt";
        prompt.type = "string";
        prompt.description =
            "What to look for. Default: a neutral factual description. "
            "Custom questions (\"read the text in this chart\") cache "
            "separately from the default.";
        def.parameters.push_back(prompt);
    }
    {
        ToolParameter engine;
        engine.name = "engine";
        engine.type = "string";
        engine.description =
            "Vision engine: local (default) or cloud. Cloud requires "
            "operator configuration (provider + model id) and fails loud "
            "when absent.";
        engine.enum_values = {"local", "cloud"};
        def.parameters.push_back(engine);
    }
    return def;
}

ToolResult DescribeTool::Execute(const ToolCall& call) {
    ToolResult result;
    result.call_id = call.id;

    Json::Value args;
    Json::CharReaderBuilder rb;
    std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
    std::string errs;
    if (!reader->parse(call.arguments.data(),
                       call.arguments.data() + call.arguments.size(),
                       &args, &errs) ||
        !args.isObject()) {
        result.error = "Failed to parse arguments: " + errs;
        return result;
    }

    const std::string ref = args.get("ref", "").asString();
    if (ref.empty()) {
        result.error = "Missing required parameter 'ref' "
                       "(media://<sha256> or bare sha256 from an "
                       "attachment)";
        return result;
    }
    const std::string prompt = args.get("prompt", "").asString();
    const std::string engine = args.get("engine", "local").asString();

    DescribeService* svc = m_local;
    if (engine == "cloud") {
        if (!m_cloud) {
            result.error = "Cloud describe is not configured "
                           "(operator must set provider + model id)";
            return result;
        }
        svc = m_cloud;
    } else if (engine != "local") {
        result.error = "Invalid engine '" + engine + "' (local or cloud)";
        return result;
    }
    if (!svc) {
        result.error = "Describe engine unavailable";
        return result;
    }

    const auto r = svc->Describe(ref, prompt);
    if (!r.ok) {
        result.error = r.error;   // loud: unknown ref, non-image, engine down
        return result;
    }
    result.success = true;
    result.output = DescribeService::Label(r);   // provenance always
    return result;
}

} // namespace animus::kernel
