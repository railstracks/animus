#pragma once

#include <json/json.h>

#include <optional>
#include <string>
#include <vector>

#include "animus_kernel/tools/ToolTypes.h"

namespace animus::kernel {

// #307: malformed tool-call arguments must never reach a persisted turn.
//
// A tool call whose `arguments` fails JSON parsing is harmless the moment the
// model emits it (the kernel logs the parse failure and the tool execution
// simply fails), but the raw malformed string used to be persisted verbatim in
// the assistant turn's tool_calls array. Every later LLM request replays that
// turn; providers validate tool-call arguments on ingestion and reject the
// whole request with HTTP 400 invalid_request_error. The session was then
// permanently bricked — no resume path, no auto-compaction (token budget far
// under threshold), manual DB surgery required.
//
// The sanitizer is the single seam both writers consult:
//   - persist side (ChainRunner::ProcessResponse) — so malformed arguments are
//     never stored in the first place;
//   - replay side (PromptAssembler::Build) — so sessions already carrying a
//     poisoned turn heal themselves on the next request: the wire copy of the
//     arguments is repaired before the request leaves the kernel.
// Both sides calling one seam keeps the persisted form identical to what the
// provider later sees, and gives the tests one function to pin.

struct ToolCallSanitizeStats {
    // Number of tool calls whose stored arguments were replaced.
    int sanitized_count{0};
    // The markers to merge into a turn's metadata ("sanitized_tool_args" map),
    // or unchanged if no call was sanitized.
    Json::Value markers{Json::objectValue};
    // Human-readable one-liners for the WARN log trail.
    std::vector<std::string> notes;
};

/// Core seam: validate/repair ONE tool call's arguments string in place.
/// Behavior:
///   - arguments parses as a JSON object -> returned unchanged (byte-identical)
///   - parses as a non-object JSON literal (e.g. "[1,2]", "5", "\"x\"")
///     or fails to parse or is empty -> arguments become "{}" (the provider-safe
///     minimal value), sanitized_count increments, and a marker is appended to
///     stats.markers["calls"][index].
/// `index` feeds the marker key so an assistant turn with multiple tool calls
/// can record per-call evidence.
/// Returns true if this call was sanitized (arguments were replaced).
/// Type-agnostic on purpose: it serves both tool-call shapes in the kernel —
/// llm::LLMToolCall (LLMProviderBase responses) and ToolCall (SessionTurn
/// tool_calls, PromptAssembler replay input) — which are distinct structs
/// with the same member shape.
bool SanitizeToolCallArgsString(std::string& arguments, int index,
                                ToolCallSanitizeStats& stats);

/// Merge #307 sanitizer markers into a turn metadata JSON string
/// ("{}"-shaped). Returns the updated metadata string; JSON errors fall back
/// to returning the input unchanged (metadata is best-effort display data and
/// must never itself become a poison vector).
std::string MergeSanitizeMarkersIntoMetadata(const std::string& metadataJson,
                                             const ToolCallSanitizeStats& stats);

/// Validate/repair every tool call in `calls` in place, writing per-call
/// markers into stats. Template in the header because the two tool-call types
/// (llm::LLMToolCall for provider responses, ToolCall for SessionTurn turns)
/// are distinct structs with the same member shape {id, name, arguments} — no
/// single non-template overload can serve both, and every call site of this
/// loop is one of those two shapes. Returns true if any call was sanitized.
template <typename CallT>
bool SanitizeToolCallArgs(std::vector<CallT>& calls, ToolCallSanitizeStats& stats) {
    bool any = false;
    for (std::size_t i = 0; i < calls.size(); ++i) {
        if (SanitizeToolCallArgsString(calls[i].arguments, static_cast<int>(i), stats)) {
            any = true;
        }
    }
    return any;
}

}  // namespace animus::kernel
