#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace animus::kernel {

// ============================================================================
// FilesystemScriptLoader — discovers Lua scripts from the filesystem
//
// Scans a conventional directory structure for .lua files:
//   <rootDir>/<agent_id>/*.lua   — per-agent scripts
//   <rootDir>/_shared/*.lua      — shared scripts loaded for all agents
//
// Metadata is parsed from Lua comments at the top of the file:
//   -- @name Display Name
//   -- @description Optional description
//   -- @enabled true|false
//
// If metadata is absent, filename (without extension) is used as the name,
// description is empty, and enabled defaults to true.
// ============================================================================

struct DiscoveredScript {
    std::string name;           // script name (from metadata or filename)
    std::string source;         // full Lua source code
    std::string description;    // optional description
    std::string agent_id;       // owning agent, empty = shared
    bool enabled{true};
    std::filesystem::path path; // filesystem location
};

class FilesystemScriptLoader {
public:
    explicit FilesystemScriptLoader(std::string rootDir);

    // Discover all scripts: shared + per-agent for each agent in the list.
    std::vector<DiscoveredScript> DiscoverAll(
        const std::vector<std::string>& agentIds) const;

    // Discover scripts for a specific agent (including shared).
    std::vector<DiscoveredScript> DiscoverForAgent(
        const std::string& agentId) const;

    // Discover only shared scripts.
    std::vector<DiscoveredScript> DiscoverShared() const;

    // Export a list of scripts to the filesystem directory structure.
    // Creates per-agent subdirectories as needed.
    struct ExportEntry {
        std::string agent_id;
        std::string name;
        std::string source;
    };
    int Export(const std::vector<ExportEntry>& entries,
               std::string* error) const;

    const std::string& RootDir() const { return m_rootDir; }

private:
    std::vector<DiscoveredScript> ScanDirectory(
        const std::filesystem::path& dir,
        const std::string& agentId) const;

    DiscoveredScript ParseFile(
        const std::filesystem::path& path,
        const std::string& agentId) const;

    std::string m_rootDir;
};

} // namespace animus::kernel
