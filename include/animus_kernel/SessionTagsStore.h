#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "animus_kernel/IDataStore.h"

namespace animus::kernel {

// ============================================================================
// SessionTag — a keyword attached to a session for semantic retrieval
// ============================================================================

struct SessionTag {
    int64_t id{0};
    std::string session_key;
    std::string tag;
    std::string source;  // "agent" or "auto"
    int64_t created_at_unix_ms{0};
};

// ============================================================================
// SessionTagsStore — CRUD for session tags backed by SQLite
// ============================================================================

class SessionTagsStore {
public:
    explicit SessionTagsStore(IDataStore* store);

    void EnsureSchema();

    // Add a tag to a session. Returns the created tag.
    // If the tag already exists for this session+source, returns the existing one.
    SessionTag Add(const std::string& sessionKey,
                   const std::string& agentId,
                   const std::string& tag,
                   const std::string& source = "agent");

    // Remove a tag from a session. Returns true if a tag was removed.
    bool Remove(const std::string& sessionKey,
                const std::string& agentId,
                const std::string& tag);

    // List all tags for a session (both agent and auto sources).
    std::vector<SessionTag> ListForSession(const std::string& sessionKey) const;

    // List tags for a session filtered by source.
    std::vector<SessionTag> ListForSessionBySource(
        const std::string& sessionKey,
        const std::string& source) const;

    // Check if a session has a specific tag.
    bool HasTag(const std::string& sessionKey,
                const std::string& tag) const;

    // Find sessions that have any of the given tags.
    // Returns distinct session keys.
    std::vector<std::string> FindSessionsWithTags(
        const std::vector<std::string>& tags) const;

    // Remove all auto-tags for a session (e.g. on re-extraction).
    std::size_t RemoveAutoTags(const std::string& sessionKey);

    // Max tags per session per source.
    static constexpr int kMaxTagsPerSource = 10;

private:
    static int64_t NowUnixMs();
    IDataStore* m_store;
};

} // namespace animus::kernel
