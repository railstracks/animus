#include "animus_kernel/SopStore.h"
#include "animus_kernel/tools/HttpClient.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <json/json.h>

namespace animus::kernel {

SopStore::SopStore(const std::filesystem::path& cacheDir,
                   HttpClient* httpClient,
                   const std::vector<std::string>& serverUrls)
    : m_cacheDir(cacheDir)
    , m_httpClient(httpClient)
    , m_serverUrls(serverUrls) {}

void SopStore::EnsureCacheDir() {
    if (!m_cacheDir.empty()) {
        std::filesystem::create_directories(m_cacheDir);
    }
}

void SopStore::Refresh() {
    m_entries.clear();
    EnsureCacheDir();

    // Fetch from all configured servers
    if (m_httpClient) {
        for (const auto& serverUrl : m_serverUrls) {
            FetchFromServer(serverUrl);
        }
    }

    // Also load any locally cached SOPs that weren't fetched remotely
    LoadCachedSops();

    // Sort by name
    std::sort(m_entries.begin(), m_entries.end(),
              [](const SopEntry& a, const SopEntry& b) {
                  return a.meta.name < b.meta.name;
              });

    std::cerr << "[sop] Loaded " << m_entries.size() << " SOPs from "
              << m_serverUrls.size() << " server(s)" << std::endl;
}

void SopStore::FetchFromServer(const std::string& serverUrl) {
    if (!m_httpClient) return;

    std::string url = serverUrl;
    // Ensure no trailing slash before appending path
    if (!url.empty() && url.back() == '/') url.pop_back();
    url += "/api/v1/sops";

    std::cerr << "[sop] Fetching SOP listing from " << url << std::endl;

    HttpClient::Request req;
    req.method = "GET";
    req.url = url;
    req.headers["Accept"] = "application/json";
    req.headers["User-Agent"] = "Animus-SopStore";
    req.timeout_seconds = 15;

    auto resp = m_httpClient->Execute(req);
    if (resp.status_code != 200) {
        std::cerr << "[sop] Failed to fetch from " << serverUrl
                  << ": HTTP " << resp.status_code
                  << " — " << resp.error << std::endl;
        return;
    }

    auto infos = ParseSopListing(resp.body);
    std::cerr << "[sop] Got " << infos.size() << " SOPs from " << serverUrl << std::endl;

    for (const auto& info : infos) {
        // Check if we already have this SOP (from another server or cache)
        bool dup = false;
        for (const auto& e : m_entries) {
            if (e.meta.name == info.name) { dup = true; break; }
        }
        if (dup) continue;

        // Check local cache first
        auto cachePath = m_cacheDir / (info.name + ".md");
        std::string cachedContent;
        bool hasCache = false;
        if (std::filesystem::exists(cachePath)) {
            std::ifstream ifs(cachePath);
            if (ifs) {
                std::stringstream ss;
                ss << ifs.rdbuf();
                cachedContent = ss.str();
                hasCache = true;
            }
        }

        SopEntry entry;
        entry.meta.name = info.name;
        entry.meta.title = info.title.empty() ? info.name : info.title;
        entry.meta.category = info.category;
        entry.meta.version = info.version.empty() ? "1.0.0" : info.version;
        entry.meta.description = info.description;
        entry.meta.tags = info.tags;
        entry.meta.source_server = serverUrl;
        entry.filepath = cachePath;

        if (hasCache) {
            entry.content = cachedContent;
        } else {
            // Fetch full content from server
            auto fetched = FetchSopContent(serverUrl, info.name);
            if (fetched) {
                entry.content = fetched->content;
                // Cache to local file
                std::ofstream ofs(cachePath);
                if (ofs) ofs << entry.content;
            } else {
                // Skip SOPs we can't fetch content for
                continue;
            }
        }

        m_entries.push_back(std::move(entry));
    }
}

std::vector<SopStore::RemoteSopInfo> SopStore::ParseSopListing(const std::string& jsonBody) const {
    std::vector<RemoteSopInfo> result;
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::istringstream stream(jsonBody);
    std::string errors;

    if (!Json::parseFromStream(builder, stream, &root, &errors)) {
        std::cerr << "[sop] Failed to parse listing JSON: " << errors << std::endl;
        return result;
    }

    // animus-sop API returns { "sops": [...] }
    const auto& sopsArray = root.get("sops", Json::Value(Json::arrayValue));
    if (!sopsArray.isArray()) {
        std::cerr << "[sop] Listing response has no 'sops' array" << std::endl;
        return result;
    }

    for (const auto& item : sopsArray) {
        RemoteSopInfo info;
        info.name = item.get("name", "").asString();
        info.title = item.get("title", "").asString();
        info.version = item.get("version", "").asString();
        info.description = item.get("description", "").asString();
        info.category = item.get("category", "").asString();

        const auto& tags = item.get("tags", Json::Value(Json::arrayValue));
        if (tags.isArray()) {
            for (const auto& tag : tags) {
                info.tags.push_back(tag.asString());
            }
        }

        if (!info.name.empty()) {
            result.push_back(std::move(info));
        }
    }

    return result;
}

std::optional<SopEntry> SopStore::FetchSopContent(const std::string& serverUrl,
                                                   const std::string& name) {
    if (!m_httpClient) return std::nullopt;

    std::string url = serverUrl;
    if (!url.empty() && url.back() == '/') url.pop_back();
    url += "/api/v1/sops/" + name;

    HttpClient::Request req;
    req.method = "GET";
    req.url = url;
    req.headers["Accept"] = "application/json";
    req.headers["User-Agent"] = "Animus-SopStore";
    req.timeout_seconds = 15;

    auto resp = m_httpClient->Execute(req);
    if (resp.status_code != 200) {
        std::cerr << "[sop] Failed to fetch content for '" << name
                  << "' from " << serverUrl
                  << ": HTTP " << resp.status_code << std::endl;
        return std::nullopt;
    }

    return ParseSopDetail(resp.body, serverUrl);
}

std::optional<SopEntry> SopStore::ParseSopDetail(const std::string& jsonBody,
                                                  const std::string& serverUrl) const {
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::istringstream stream(jsonBody);
    std::string errors;

    if (!Json::parseFromStream(builder, stream, &root, &errors)) {
        std::cerr << "[sop] Failed to parse SOP detail JSON: " << errors << std::endl;
        return std::nullopt;
    }

    SopEntry entry;
    entry.meta.name = root.get("name", "").asString();
    entry.meta.title = root.get("title", "").asString();
    entry.meta.category = root.get("category", "").asString();
    entry.meta.version = root.get("version", "1.0.0").asString();
    entry.meta.description = root.get("description", "").asString();
    entry.meta.source_server = serverUrl;
    entry.content = root.get("content", "").asString();

    const auto& tags = root.get("tags", Json::Value(Json::arrayValue));
    if (tags.isArray()) {
        for (const auto& tag : tags) {
            entry.meta.tags.push_back(tag.asString());
        }
    }

    if (entry.meta.name.empty() || entry.content.empty()) {
        return std::nullopt;
    }

    return entry;
}

void SopStore::LoadCachedSops() {
    if (m_cacheDir.empty() || !std::filesystem::exists(m_cacheDir)) return;

    for (const auto& entry : std::filesystem::directory_iterator(m_cacheDir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".md") continue;

        std::string slug = entry.path().stem().string();

        // Skip if already loaded from a server
        bool dup = false;
        for (const auto& e : m_entries) {
            if (e.meta.name == slug) { dup = true; break; }
        }
        if (dup) continue;

        std::ifstream ifs(entry.path());
        if (!ifs) continue;

        std::stringstream ss;
        ss << ifs.rdbuf();
        std::string content = ss.str();

        SopEntry sop;
        sop.filepath = entry.path();
        sop.content = content;
        sop.meta.name = slug;
        sop.meta.title = slug;
        sop.meta.version = "1.0.0";
        sop.meta.source_server = "local";
        m_entries.push_back(std::move(sop));
    }
}

std::vector<SopMeta> SopStore::List(const std::string& category,
                                     int page, int perPage) const {
    std::vector<SopMeta> filtered;
    for (const auto& e : m_entries) {
        if (category.empty() || e.meta.category == category) {
            filtered.push_back(e.meta);
        }
    }

    int start = (page - 1) * perPage;
    if (start < 0) start = 0;
    if (start >= static_cast<int>(filtered.size())) return {};

    int end = std::min(start + perPage, static_cast<int>(filtered.size()));
    return std::vector<SopMeta>(filtered.begin() + start, filtered.begin() + end);
}

std::vector<SopMeta> SopStore::Search(const std::string& query,
                                       int page, int perPage) const {
    std::string q = query;
    std::transform(q.begin(), q.end(), q.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    struct Match {
        SopMeta meta;
        int score;
    };

    std::vector<Match> matches;
    for (const auto& e : m_entries) {
        int score = 0;
        std::string name = e.meta.name;
        std::string title = e.meta.title;
        std::string desc = e.meta.description;
        std::string cat = e.meta.category;
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        std::transform(title.begin(), title.end(), title.begin(), ::tolower);
        std::transform(desc.begin(), desc.end(), desc.begin(), ::tolower);
        std::transform(cat.begin(), cat.end(), cat.begin(), ::tolower);

        if (name.find(q) != std::string::npos) score += 10;
        if (title.find(q) != std::string::npos) score += 8;
        for (const auto& tag : e.meta.tags) {
            std::string t = tag;
            std::transform(t.begin(), t.end(), t.begin(), ::tolower);
            if (t.find(q) != std::string::npos) score += 5;
        }
        if (cat.find(q) != std::string::npos) score += 4;
        if (desc.find(q) != std::string::npos) score += 3;

        if (score > 0) {
            matches.push_back({e.meta, score});
        }
    }

    std::sort(matches.begin(), matches.end(),
              [](const Match& a, const Match& b) { return a.score > b.score; });

    int start = (page - 1) * perPage;
    if (start < 0) start = 0;
    if (start >= static_cast<int>(matches.size())) return {};

    int end = std::min(start + perPage, static_cast<int>(matches.size()));
    std::vector<SopMeta> result;
    for (int i = start; i < end; ++i) {
        result.push_back(matches[i].meta);
    }
    return result;
}

std::optional<SopEntry> SopStore::Get(const std::string& name) const {
    for (const auto& e : m_entries) {
        if (e.meta.name == name) return e;
    }
    return std::nullopt;
}

size_t SopStore::Count(const std::string& category) const {
    if (category.empty()) return m_entries.size();
    return std::count_if(m_entries.begin(), m_entries.end(),
                         [&](const SopEntry& e) { return e.meta.category == category; });
}

} // namespace animus::kernel