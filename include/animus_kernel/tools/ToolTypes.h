#pragma once

#include <string>
#include <vector>

namespace animus::kernel {

// ============================================================================
// Tool Types — definitions for the tool calling system
// ============================================================================

/// How a tool's result should be delivered.
enum class ToolResultMode {
    stream_to_user,     // Result streams to user immediately (reply, notify)
    deliver_to_model,   // Result feeds back into the LLM loop (memory_recall, file_read, shell_exec)
    both                // Result streams to user AND feeds back to model (long-running commands user watches)
};

/// A parameter definition for a tool.
struct ToolParameter {
    std::string name;
    std::string type;         // "string", "integer", "boolean", "array", "object", "number"
    std::string description;
    bool required{false};
    std::string default_value_json; // Optional JSON literal default value.

    /// For enum-constrained string parameters.
    std::vector<std::string> enum_values;

    /// For object-type parameters, nested property definitions.
    std::vector<ToolParameter> properties;
};

/// Definition of a tool the LLM can invoke.
struct ToolDefinition {
    std::string name;
    std::string description;
    std::vector<ToolParameter> parameters;
    std::vector<ToolParameter> config_parameters; // Admin/runtime tool config contract.
    ToolResultMode resultMode{ToolResultMode::deliver_to_model}; // default: feed back to model

    // Session types this tool is available in. Empty = available in all session types.
    // Non-empty = only available in the listed session types.
    // Example: {"consolidation"} means only during consolidation sessions.
    std::vector<std::string> session_types;
};

/// A single tool call from an LLM response.
struct ToolCall {
    std::string id;           // unique call identifier from the LLM
    std::string name;         // tool name
    std::string arguments;    // JSON string of arguments
};

/// Result of executing a tool call.
struct ToolResult {
    std::string call_id;      // matches ToolCall::id
    bool success{false};
    std::string output;        // result content (JSON string or plain text)
    std::string error;        // error message if !success
};

} // namespace animus::kernel
