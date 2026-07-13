#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "animus_kernel/tools/ToolTypes.h"

namespace animus::kernel {

using SessionId = std::uint64_t;
using SessionTurnId = std::uint64_t;

struct SessionKey {
    std::string connector;
    std::string conversation_id;
    std::string thread_id;

    std::string ToString() const;
};

struct SessionTurn {
    SessionTurnId turn_id{0};
    std::string role;     // "user" / "assistant" / "system" / "tool"
    std::string content;
    std::uint64_t unix_ms{0};
    bool is_summary{false};
    std::vector<SessionTurnId> compacted_from;
    bool is_compacted{false};  // True if this turn has been folded into a compaction summary (kept in DB, excluded from prompts)

    // Tool call trace
    std::vector<ToolCall> tool_calls;    // Tool calls made in this turn
    std::string tool_call_id;            // For role="tool": which call this responds to
    std::string tool_name;               // For role="tool": which tool was called

    // Thinking trace (native provider thinking)
    std::string thinking_content;          // Thinking/reasoning tokens from the model (separate from content)

    // Consolidation intake processing state
    bool intake_processed{false};
    std::uint64_t intake_processed_at_unix_ms{0};

    // Token count cache (estimated, populated on read/write)
    std::size_t token_count{0};
};

enum class SessionAccessMode {
    ReadWrite,
    ReadOnly
};

class Session {
public:
    Session(SessionId id, SessionKey key);

    SessionId Id() const;
    const SessionKey& Key() const;
    std::uint64_t CreatedUnixMs() const;
    std::uint64_t LastActiveUnixMs() const;
    void SetCreatedUnixMs(std::uint64_t ms);
    void SetLastActiveUnixMs(std::uint64_t ms);
    bool IsTerminated() const;
    void MarkTerminated();

    void AddTurn(SessionTurn turn);
    const std::vector<SessionTurn>& Turns() const;
    std::vector<SessionTurn>& MutableTurns();
    void TrimOldestTurns(std::size_t count);
    void MarkTurnCompacted(SessionTurnId turnId);

    // Message count — uses override if set (for paginated listings),
    // otherwise falls back to in-memory turn count.
    std::size_t MessageCount() const;
    void SetMessageCountOverride(std::size_t count);

    void SetCompactionSummary(SessionTurn summary_turn);
    const SessionTurn* GetCompactionSummary() const;
    void SetSummary(std::string summary);
    const std::string& Summary() const;

    // Agent ownership — which agent this session belongs to.
    const std::string& AgentId() const;
    void SetAgentId(std::string agentId);

    // Provider assignment — which LLM provider this session routes to.
    const std::string& ProviderId() const;
    void SetProviderId(std::string providerId);

    // Session type — classifies the session for tool gating and routing.
    // Empty string = normal (direct chat). Other values: "consolidation", "gallivanting", etc.
    const std::string& SessionType() const;
    void SetSessionType(std::string sessionType);

private:
    static std::uint64_t NowUnixMs();

    SessionId m_id{0};
    SessionKey m_key{};
    SessionTurnId m_nextTurnId{1};
    std::uint64_t m_createdUnixMs{0};
    std::uint64_t m_lastActiveUnixMs{0};
    bool m_terminated{false};
    std::vector<SessionTurn> m_turns;
    std::optional<SessionTurn> m_compactionSummary;
    std::string m_summary;
    std::string m_agentId{"default"};
    std::string m_providerId;
    std::string m_sessionType;
    std::optional<std::size_t> m_messageCountOverride;
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
    const std::string& AgentId() const;
    const std::string& ProviderId() const;
    const std::string& SessionType() const;

    void AddTurn(SessionTurn turn);
    void TrimOldestTurns(std::size_t count);
    void MarkTurnCompacted(SessionTurnId turnId);
    void SetCompactionSummary(SessionTurn summary_turn);
    void SetSummary(std::string summary);
    void SetAgentId(std::string agentId);
    void SetProviderId(std::string providerId);
    void SetSessionType(std::string sessionType);

private:
    Session& RequireSession() const;
    void EnsureWritable(const char* action) const;

    std::shared_ptr<Session> m_session;
    SessionAccessMode m_mode{SessionAccessMode::ReadOnly};
};

} // namespace animus::kernel
