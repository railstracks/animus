#include "animus_kernel/lua/FilesystemScriptLoader.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

namespace animus::kernel {

FilesystemScriptLoader::FilesystemScriptLoader(std::string rootDir)
    : m_rootDir(std::move(rootDir)) {}

std::vector<DiscoveredScript>
FilesystemScriptLoader::DiscoverAll(const std::vector<std::string>& agentIds) const {
    std::vector<DiscoveredScript> result;

    // Shared scripts first
    auto shared = DiscoverShared();
    result.insert(result.end(), shared.begin(), shared.end());

    // Per-agent scripts
    for (const auto& agentId : agentIds) {
        auto scripts = DiscoverForAgent(agentId);
        // DiscoverForAgent includes shared; filter those out since we
        // already added them above.
        for (auto& s : scripts) {
            if (s.agent_id.empty()) continue;  // skip shared duplicates
            result.push_back(std::move(s));
        }
    }

    return result;
}

std::vector<DiscoveredScript>
FilesystemScriptLoader::DiscoverForAgent(const std::string& agentId) const {
    std::vector<DiscoveredScript> result;

    // Shared scripts
    auto shared = DiscoverShared();
    result.insert(result.end(), shared.begin(), shared.end());

    // Agent-specific scripts
    auto agentDir = std::filesystem::path(m_rootDir) / agentId;
    if (std::filesystem::exists(agentDir) && std::filesystem::is_directory(agentDir)) {
        auto scripts = ScanDirectory(agentDir, agentId);
        result.insert(result.end(), scripts.begin(), scripts.end());
    }

    return result;
}

std::vector<DiscoveredScript>
FilesystemScriptLoader::DiscoverShared() const {
    auto sharedDir = std::filesystem::path(m_rootDir) / "_shared";
    if (!std::filesystem::exists(sharedDir) || !std::filesystem::is_directory(sharedDir)) {
        return {};
    }
    return ScanDirectory(sharedDir, "");  // empty agent_id = shared
}

std::vector<DiscoveredScript>
FilesystemScriptLoader::ScanDirectory(
    const std::filesystem::path& dir,
    const std::string& agentId) const {

    std::vector<DiscoveredScript> result;

    try {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".lua") continue;

            try {
                auto script = ParseFile(entry.path(), agentId);
                result.push_back(std::move(script));
            } catch (const std::exception& e) {
                std::cerr << "[lua:fs] ERROR parsing '" << entry.path().string()
                          << "': " << e.what() << "\n";
            }
        }
    } catch (const std::exception& e) {
        // Directory doesn't exist or isn't readable — non-fatal
    }

    // Sort by name for deterministic load order
    std::sort(result.begin(), result.end(),
              [](const DiscoveredScript& a, const DiscoveredScript& b) {
                  return a.name < b.name;
              });

    return result;
}

DiscoveredScript
FilesystemScriptLoader::ParseFile(
    const std::filesystem::path& path,
    const std::string& agentId) const {

    DiscoveredScript script;
    script.path = path;
    script.agent_id = agentId;
    script.name = path.stem().string();  // filename without extension
    script.enabled = true;

    // Read file contents
    std::ifstream ifs(path);
    if (!ifs) {
        throw std::runtime_error("Cannot open file: " + std::string(std::strerror(errno)));
    }

    std::stringstream ss;
    ss << ifs.rdbuf();
    script.source = ss.str();

    // Parse metadata from leading comments (-- @key value)
    // Only parse from the top of the file, stopping at the first
    // non-comment, non-empty line.
    std::istringstream lineStream(script.source);
    std::string line;
    bool inMetadata = true;
    while (std::getline(lineStream, line) && inMetadata) {
        // Trim leading whitespace
        auto trimmed = line;
        auto firstNonWs = trimmed.find_first_not_of(" \t");
        if (firstNonWs == std::string::npos) continue;  // skip blank lines
        trimmed = trimmed.substr(firstNonWs);

        // Check for metadata comment
        if (trimmed.rfind("-- @", 0) == 0) {
            // Extract key-value: "-- @name Weather Tool"
            auto rest = trimmed.substr(4);  // skip "-- @"
            auto spacePos = rest.find(' ');
            if (spacePos != std::string::npos) {
                std::string key = rest.substr(0, spacePos);
                std::string value = rest.substr(spacePos + 1);

                if (key == "name") {
                    script.name = value;
                } else if (key == "description") {
                    script.description = value;
                } else if (key == "enabled") {
                    script.enabled = (value == "true" || value == "1" || value == "yes");
                }
            }
        } else if (trimmed.rfind("--", 0) == 0) {
            // Regular comment line — not metadata, but still in the header
            continue;
        } else {
            // First non-comment line — stop metadata parsing
            inMetadata = false;
        }
    }

    return script;
}

int FilesystemScriptLoader::Export(
    const std::vector<ExportEntry>& entries,
    std::string* error) const {

    // Ensure root directory exists
    try {
        std::filesystem::create_directories(m_rootDir);
    } catch (const std::exception& e) {
        if (error) *error = std::string("Cannot create root directory: ") + e.what();
        return -1;
    }

    int exported = 0;
    for (const auto& entry : entries) {
        // Determine target directory
        std::string subdir = entry.agent_id.empty() ? "_shared" : entry.agent_id;
        auto dir = std::filesystem::path(m_rootDir) / subdir;

        try {
            std::filesystem::create_directories(dir);

            // Sanitize filename: replace non-alphanumeric with underscore
            std::string filename = entry.name;
            for (auto& c : filename) {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
                    c = '_';
                }
            }
            filename += ".lua";

            auto filePath = dir / filename;

            // Don't overwrite existing files
            if (std::filesystem::exists(filePath)) {
                continue;
            }

            std::ofstream ofs(filePath);
            if (!ofs) {
                if (error) {
                    *error += "Cannot write " + filePath.string() + "; ";
                }
                continue;
            }
            ofs << entry.source;
            ofs.close();
            ++exported;
        } catch (const std::exception& e) {
            if (error) {
                *error += std::string("Error exporting '") + entry.name + "': " + e.what() + "; ";
            }
        }
    }

    return exported;
}

} // namespace animus::kernel
