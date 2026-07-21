#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace animus::kernel {

class HttpClient;

// ============================================================================
// SopStore — catalogs and serves Standard Operating Procedure files
// Ticket 125: SOP System
// ============================================================================

struct SopMeta {
    std::string name;         // unique slug (from frontmatter or filename)
    std::string title;        // human-readable title
    std::string category;     // top-level grouping
    std::vector<std::string> tags;
    std::string version;      // semver string
    std::string description;  // one-line summary
};

struct SopEntry {
    SopMeta meta;
    std::string content;      // full markdown content (without frontmatter)
    std::string raw;          // full file content including frontmatter
    std::filesystem::path filepath;
};

class SopStore {
public:
    /// Construct with local sops directory and optional remote source URL.
    /// remoteUrl should point to a GitHub contents API endpoint or similar.
    SopStore(const std::filesystem::path& sopsDir,
             HttpClient* httpClient = nullptr,
             const std::string& remoteUrl = "");

    /// Scan the local directory and build the catalog. Called on startup.
    /// If a remote source is configured, fetches and caches remote SOPs first.
    void Refresh();

    /// List SOPs, optionally filtered by category. Paginated.
    std::vector<SopMeta> List(const std::string& category,
                              int page,
                              int perPage) const;

    /// Search SOPs by keyword across name, title, tags, description.
    std::vector<SopMeta> Search(const std::string& query,
                                int page,
                                int perPage) const;

    /// Get full SOP content by name (slug).
    std::optional<SopEntry> Get(const std::string& name) const;

    /// Total count of SOPs (optionally filtered by category).
    size_t Count(const std::string& category = "") const;

    /// Whether the store has any SOPs loaded.
    bool HasSops() const { return !m_entries.empty(); }

private:
    std::filesystem::path m_sopsDir;
    HttpClient* m_httpClient{nullptr};
    std::string m_remoteUrl;

    std::vector<SopEntry> m_entries;

    /// Fetch remote SOP listing and download files to local cache.
    void FetchRemoteSops();

    /// Parse YAML-like frontmatter from markdown content.
    /// Returns {frontmatter_lines, body_content}.
    std::pair<std::string, std::string> SplitFrontmatter(const std::string& raw) const;

    /// Extract a field value from frontmatter text.
    std::string GetField(const std::string& frontmatter, const std::string& key) const;

    /// Parse a YAML-like list value: [item1, item2, item3]
    std::vector<std::string> ParseList(const std::string& value) const;

    /// Generate slug from filename if frontmatter name is missing.
    std::string SlugFromFilename(const std::filesystem::path& filepath) const;
};

} // namespace animus::kernel
