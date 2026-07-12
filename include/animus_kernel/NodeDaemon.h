#pragma once

#include <string>
#include <vector>

namespace animus::kernel {

// ============================================================================
// NodeDaemon — lightweight daemon for --node mode
//
// Connects to a central Animus server via WebSocket, receives tool calls,
// executes them locally, and returns results. Does NOT load LLM providers,
// scheduler, channels, or consolidation.
// ============================================================================

struct NodeDaemonConfig {
    std::string serverUrl;   // e.g. "ws://host:port/ws/node"
    std::string token;
    std::string name;
    std::vector<std::string> allowedTools;  // e.g. ["exec", "file"]
};

/// Run the node daemon. Blocks until SIGTERM/SIGINT.
/// Returns exit code (0 = clean shutdown, 1 = error).
int RunNodeDaemon(const NodeDaemonConfig& config);

} // namespace animus::kernel
