#include "animus_kernel/ToolCallSanitizer.h"

#include <sstream>

namespace animus::kernel {

bool SanitizeToolCallArgsString(std::string& arguments, int index,
                                ToolCallSanitizeStats& stats) {
    const std::size_t original_len = arguments.size();

    bool parses_as_object = false;
    {
        Json::Value parsed;
        Json::CharReaderBuilder rb;
        std::istringstream iss(arguments);
        std::string err;
        if (Json::parseFromStream(rb, iss, &parsed, &err) && parsed.isObject()) {
            parses_as_object = true;
        }
    }
    if (parses_as_object) {
        return false;
    }

    // Provider-safe minimal replacement. The original payload is NOT kept in
    // the turn — storing it would reintroduce the poison — only its size is.
    arguments = "{}";

    stats.sanitized_count += 1;

    if (!stats.markers.isObject()) {
        stats.markers = Json::objectValue;
    }
    Json::Value calls(Json::arrayValue);
    if (stats.markers.isMember("calls") && stats.markers["calls"].isArray()) {
        calls = stats.markers["calls"];
    }
    Json::Value marker(Json::objectValue);
    marker["index"] = index;
    marker["reason"] = "invalid_json_or_non_object";
    marker["original_len"] = static_cast<Json::UInt64>(original_len);
    calls.append(marker);
    stats.markers["calls"] = calls;

    stats.notes.push_back("tool call #" + std::to_string(index) +
                          " — malformed arguments (" + std::to_string(original_len) +
                          " chars) replaced with {}");
    return true;
}

std::string MergeSanitizeMarkersIntoMetadata(const std::string& metadataJson,
                                             const ToolCallSanitizeStats& stats) {
    if (stats.markers.empty()) {
        return metadataJson;
    }
    Json::Value metadata;
    Json::CharReaderBuilder rb;
    std::istringstream iss(metadataJson.empty() ? "{}" : metadataJson);
    std::string err;
    if (!Json::parseFromStream(rb, iss, &metadata, &err) || !metadata.isObject()) {
        // Metadata is best-effort display data — never let a bad value
        // become its own poison vector (#307 lesson applied to itself).
        return metadataJson;
    }
    metadata["sanitized_tool_args"] = stats.markers;
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, metadata);
}

}  // namespace animus::kernel
