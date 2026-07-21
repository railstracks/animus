#include "animus_kernel/SopStore.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <set>

namespace animus::kernel {

SopStore::SopStore(const std::filesystem::path& sopsDir)
    : m_sopsDir(sopsDir) {}

void SopStore::Refresh() {
    m_entries.clear();

    if (m_sopsDir.empty() || !std::filesystem::exists(m_sopsDir)) {
        std::cerr << "[sop] Directory not found: " << m_sopsDir.string() << std::endl;
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(m_sopsDir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".md") continue;

        std::ifstream ifs(entry.path());
        if (!ifs) continue;

        std::stringstream ss;
        ss << ifs.rdbuf();
        std::string raw = ss.str();

        auto [frontmatter, body] = SplitFrontmatter(raw);

        SopEntry sop;
        sop.filepath = entry.path();
        sop.raw = raw;
        sop.content = body;
        sop.meta.name = GetField(frontmatter, "name");
        if (sop.meta.name.empty()) {
            sop.meta.name = SlugFromFilename(entry.path());
        }
        sop.meta.title = GetField(frontmatter, "title");
        if (sop.meta.title.empty()) sop.meta.title = sop.meta.name;
        sop.meta.category = GetField(frontmatter, "category");
        sop.meta.version = GetField(frontmatter, "version");
        if (sop.meta.version.empty()) sop.meta.version = "1.0.0";
        sop.meta.description = GetField(frontmatter, "description");
        sop.meta.tags = ParseList(GetField(frontmatter, "tags"));

        m_entries.push_back(std::move(sop));
    }

    // Sort by category, then name
    std::sort(m_entries.begin(), m_entries.end(),
              [](const SopEntry& a, const SopEntry& b) {
                  if (a.meta.category != b.meta.category)
                      return a.meta.category < b.meta.category;
                  return a.meta.name < b.meta.name;
              });

    std::cerr << "[sop] Loaded " << m_entries.size() << " SOPs from "
              << m_sopsDir.string() << std::endl;
}

std::vector<SopMeta> SopStore::List(const std::string& category,
                                     int page, int perPage) const {
    std::vector<SopMeta> filtered;
    for (const auto& e : m_entries) {
        if (category.empty() || e.meta.category == category) {
            filtered.push_back(e.meta);
        }
    }

    // Paginate
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

    // Paginate
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

// --- Private helpers ---

std::pair<std::string, std::string> SopStore::SplitFrontmatter(const std::string& raw) const {
    // Check for --- delimited frontmatter
    if (raw.size() < 4 || raw.substr(0, 3) != "---") {
        return {"", raw};
    }
    // Find closing ---
    auto end = raw.find("\n---", 3);
    if (end == std::string::npos) return {"", raw};

    std::string frontmatter = raw.substr(3, end - 3);
    // Skip past the closing --- and newline
    size_t bodyStart = end + 4;
    if (bodyStart < raw.size() && raw[bodyStart] == '\r') bodyStart++;
    if (bodyStart < raw.size() && raw[bodyStart] == '\n') bodyStart++;

    std::string body = raw.substr(bodyStart);
    return {frontmatter, body};
}

std::string SopStore::GetField(const std::string& frontmatter, const std::string& key) const {
    // Simple key: value parser for YAML-like frontmatter
    std::istringstream ss(frontmatter);
    std::string line;
    std::string searchKey = key + ":";
    bool inMultiline = false;
    std::string value;

    while (std::getline(ss, line)) {
        // Trim leading whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        std::string trimmed = line.substr(start);

        if (inMultiline) {
            // Check if this line is still indented (multiline continuation)
            if (start > 0 && (line[0] == ' ' || line[0] == '\t')) {
                // Remove leading whitespace and add to value
                std::string content = trimmed;
                // Remove trailing \r
                if (!content.empty() && content.back() == '\r') content.pop_back();
                if (!content.empty()) {
                    if (!value.empty()) value += " ";
                    value += content;
                }
                continue;
            } else {
                inMultiline = false;
            }
        }

        if (trimmed.size() >= searchKey.size() &&
            trimmed.substr(0, searchKey.size()) == searchKey) {
            value = trimmed.substr(searchKey.size());
            // Trim leading whitespace
            size_t vs = value.find_first_not_of(" \t");
            if (vs != std::string::npos) value = value.substr(vs);
            else value = "";

            // Remove quotes if present
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            } else if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'') {
                value = value.substr(1, value.size() - 2);
            }

            // Remove trailing \r
            if (!value.empty() && value.back() == '\r') value.pop_back();

            // Check for multiline (> indicator)
            if (value == ">" || value == ">-" || value == "|" || value == "|-") {
                inMultiline = true;
                value = "";
            }
        }
    }
    return value;
}

std::vector<std::string> SopStore::ParseList(const std::string& value) const {
    std::vector<std::string> result;
    // Parse [item1, item2, item3]
    std::string v = value;
    // Remove brackets
    if (!v.empty() && v.front() == '[') v = v.substr(1);
    if (!v.empty() && v.back() == ']') v.pop_back();

    std::stringstream ss(v);
    std::string item;
    while (std::getline(ss, item, ',')) {
        // Trim whitespace
        size_t s = item.find_first_not_of(" \t");
        size_t e = item.find_last_not_of(" \t\r");
        if (s != std::string::npos && e != std::string::npos) {
            item = item.substr(s, e - s + 1);
            // Remove quotes
            if (item.size() >= 2 && item.front() == '"' && item.back() == '"') {
                item = item.substr(1, item.size() - 2);
            }
            if (!item.empty()) result.push_back(item);
        }
    }
    return result;
}

std::string SopStore::SlugFromFilename(const std::filesystem::path& filepath) const {
    std::string stem = filepath.stem().string();
    return stem;
}

} // namespace animus::kernel
