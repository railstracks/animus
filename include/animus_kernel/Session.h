#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace animus::kernel {

using SessionId = std::uint64_t;

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
    std::string role;     // e.g. "user" / "assistant" / "system"
    std::string content;
    std::uint64_t unix_ms{0};
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

    // Rolling summary / compaction output (optional).
    void SetSummary(std::string summary);
    const std::string& Summary() const;

private:
    SessionId m_id{0};
    SessionKey m_key{};
    std::vector<SessionTurn> m_turns;
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
    const std::string& Summary() const;

    void AddTurn(SessionTurn turn);
    void SetSummary(std::string summary);

private:
    Session& RequireSession() const;
    void EnsureWritable(const char* action) const;

    std::shared_ptr<Session> m_session;
    SessionAccessMode m_mode{SessionAccessMode::ReadOnly};
};

} // namespace animus::kernel
