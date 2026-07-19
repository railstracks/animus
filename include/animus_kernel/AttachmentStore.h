#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

#include "animus_kernel/IDataStore.h"

namespace animus::kernel {

// Forward declaration removed — IDataStore is now included above

// ============================================================================
// AttachmentStore — persistence for session attachments (Ticket 123)
// ============================================================================

struct Attachment {
    std::string id;             // unique within the session
    std::uint64_t turn_id{0};   // which turn this attachment belongs to
    std::string session_key;    // session key string
    std::string filename;       // display name
    std::string mime_type;      // "text/plain", "image/png", "audio/wav", etc.
    std::string filepath;       // served path (for file-backed, empty if inline)
    std::string data_b64;       // inline base64 (empty if file-backed)
    std::int64_t size_bytes{0};
    std::int64_t created_at{0};
};

class AttachmentStore {
public:
    explicit AttachmentStore(IDataStore* store);

    /// Create the DB schema if it doesn't exist
    void EnsureSchema();

    /// Save an attachment record
    bool Save(const Attachment& attachment);

    /// Get a single attachment by ID
    std::optional<Attachment> GetById(const std::string& id);

    /// List all attachments for a given turn
    std::vector<Attachment> GetForTurn(std::uint64_t turnId);

    /// List all attachments for a session (by session key)
    std::vector<Attachment> GetForSession(const std::string& sessionKey);

    /// Delete attachments for a turn (cascade on session delete)
    bool DeleteForTurn(std::uint64_t turnId);

private:
    IDataStore* m_store;
};

} // namespace animus::kernel