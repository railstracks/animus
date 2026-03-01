#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace animus::kernel {

using SessionId = std::uint64_t;
using SessionTurnId = std::uint64_t;

struct SessionKey {
    // The connector namespace, e.g. "slack".
    std::string connector;

    // The logical conversation identifier within that connector.
    // Example: Slack channel id, WhatsApp chat id.
    std::string conversation_id;

    // Optional thread/subcontext identifier.
    // Example: Slack thread_ts.
    std::string thread_id;

    std::string ToString() const;
};

struct SessionTurn {
    SessionTurnId turn_id{0};
    std::string role;     // e.g. "user" / "assistant" / "system"
    std::string content;
    std::uint64_t unix_ms{0};
    bool is_summary{false};
    std::vector<SessionTurnId> compacted_from;
};

enum class SessionAccessMode {
    ReadWrite,
    ReadOnly
};

// Session groups multiple ChainInstances (activations) under a long-lived context.
class Session {
public:
    Session(SessionId id, SessionKey key);

    SessionId Id() const;
    const SessionKey& Key() const;

    void AddTurn(SessionTurn turn);
    const std::vector<SessionTurn>& Turns() const;
    void TrimOldestTurns(std::size_t count);

    // Rolling summary / compaction output (optional).
    void SetCompactionSummary(SessionTurn summary_turn);
    const SessionTurn* GetCompactionSummary() const;
    void SetSummary(std::string summary);
    const std::string& Summary() const;

private:
    SessionId m_id{0};
    SessionKey m_key{};
    SessionTurnId m_nextTurnId{1};
    std::vector<SessionTurn> m_turns;
    std::optional<SessionTurn> m_compactionSummary;
    std::string m_summary;
};

class SessionAccess {
public:
    SessionAccess() = default;
    SessionAccess(std::shared_ptr<Session> session, SessionAccessMode mode);

    SessionAccessMode Mode() const;
    explicit operator bool() const;

    SessionId Id() const;
    const SessionKey& Key() const;
    const std::vector<SessionTurn>& Turns() const;
    const SessionTurn* GetCompactionSummary() const;
    const std::string& Summary() const;

    void AddTurn(SessionTurn turn);
    void TrimOldestTurns(std::size_t count);
    void SetCompactionSummary(SessionTurn summary_turn);
    void SetSummary(std::string summary);

private:
    Session& RequireSession() const;
    void EnsureWritable(const char* action) const;

    std::shared_ptr<Session> m_session;
    SessionAccessMode m_mode{SessionAccessMode::ReadOnly};
};

} // namespace animus::kernel
