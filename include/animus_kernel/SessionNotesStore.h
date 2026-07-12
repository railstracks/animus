#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "animus_kernel/IDataStore.h"

namespace animus::kernel {

// ============================================================================
// SessionNote — a single persistent bullet point attached to a session
// ============================================================================

struct SessionNote {
    int64_t id{0};
    std::string session_key;
    std::string agent_id;
    std::string bullet;
    int sort_order{0};
    int64_t created_at_unix_ms{0};
    int64_t updated_at_unix_ms{0};
};

// ============================================================================
// SessionNotesStore — CRUD for session notes backed by SQLite
// ============================================================================

class SessionNotesStore {
public:
    explicit SessionNotesStore(IDataStore* store);

    void EnsureSchema();

    // Create a new note. Returns the created note with id and timestamps populated.
    SessionNote Create(const std::string& sessionKey,
                       const std::string& agentId,
                       const std::string& bullet,
                       int sortOrder);

    // List all notes for a session, ordered by sort_order.
    std::vector<SessionNote> ListForSession(const std::string& sessionKey,
                                             const std::string& agentId) const;

    // Get a single note by id. Returns nullopt if not found or wrong agent.
    std::optional<SessionNote> GetById(int64_t noteId, const std::string& agentId) const;

    // Update bullet text. Returns true on success.
    bool Update(int64_t noteId, const std::string& agentId, const std::string& newBullet);

    // Delete a note. Returns true on success.
    bool Delete(int64_t noteId, const std::string& agentId);

    // Reorder notes. Takes an ordered array of note IDs. Returns true on success.
    bool Reorder(const std::vector<int64_t>& noteIds, const std::string& agentId);

    // Count notes for a session.
    int64_t CountForSession(const std::string& sessionKey, const std::string& agentId) const;

    // Get the next sort order value for a session (max + 1).
    int NextSortOrder(const std::string& sessionKey, const std::string& agentId) const;

    static constexpr int kMaxNotesPerSession = 20;

private:
    static int64_t NowUnixMs();
    IDataStore* m_store;
};

} // namespace animus::kernel
