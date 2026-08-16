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
// Sources SOPs from remote animus-sop registry servers (REST API).
// Local directory used as cache only.
// ============================================================================

struct SopMeta {
    std::string name;         // unique slug
    std::string title;        // human-readable title
    std::string category;     // top-level grouping (may be empty)
    std::vector<std::string> tags;
    std::string version;      // version string
    std::string description;  // one-line summary
    std::string source_server; // which server this SOP came from
};

struct SopEntry {
    SopMeta meta;
    std::string content;      // full markdown content
    std::filesystem::path filepath; // local cache path (if cached)
};

class SopStore {
public:
    /// Construct with local cache directory, HTTP client, and list of SOP server URLs.
    SopStore(const std::filesystem::path& cacheDir,
             HttpClient* httpClient,
             const std::vector<std::string>& serverUrls);

    /// Fetch SOP listings from all configured servers. Called on startup.
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

    /// Get the list of configured server URLs.
    const std::vector<std::string>& GetServerUrls() const { return m_serverUrls; }

private:
    std::filesystem::path m_cacheDir;
    HttpClient* m_httpClient{nullptr};
    std::vector<std::string> m_serverUrls;

    std::vector<SopEntry> m_entries;

    /// Fetch SOP listing from a single server via REST API.
    /// Calls GET /api/v1/sops on the server, parses JSON response.
    void FetchFromServer(const std::string& serverUrl);

    /// Fetch full SOP content from a server by name.
    /// Calls GET /api/v1/sops/:name and caches the result.
    std::optional<SopEntry> FetchSopContent(const std::string& serverUrl,
                                             const std::string& name);

    /// Load cached SOPs from local directory.
    void LoadCachedSops();

    /// Parse JSON array from /api/v1/sops response into SopMeta entries.
    /// Returns list of (name, title, version, description, tags, category) tuples.
    struct RemoteSopInfo {
        std::string name;
        std::string title;
        std::string version;
        std::string description;
        std::string category;
        std::vector<std::string> tags;
    };
    std::vector<RemoteSopInfo> ParseSopListing(const std::string& jsonBody) const;

    /// Parse a single SOP from /api/v1/sops/:name response.
    std::optional<SopEntry> ParseSopDetail(const std::string& jsonBody,
                                            const std::string& serverUrl) const;

    /// Ensure the cache directory exists.
    void EnsureCacheDir();
};

} // namespace animus::kernel