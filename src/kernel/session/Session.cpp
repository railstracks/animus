#include "animus_kernel/Session.h"

#include <chrono>
#include <sstream>
#include <stdexcept>

namespace animus::kernel {

std::string SessionKey::ToString() const {
    // Canonical string key for in-memory map keys and future Redis keys.
    // Format: connector|conversation_id|thread_id
    std::ostringstream oss;
    oss << connector << "|" << conversation_id << "|" << thread_id;
    return oss.str();
}

Session::Session(SessionId id, SessionKey key)
    : m_id(id), m_key(std::move(key)) {
    m_createdUnixMs = NowUnixMs();
    m_lastActiveUnixMs = m_createdUnixMs;
}

SessionId Session::Id() const {
    return m_id;
}

const SessionKey& Session::Key() const {
    return m_key;
}

std::uint64_t Session::CreatedUnixMs() const {
    return m_createdUnixMs;
}

std::uint64_t Session::LastActiveUnixMs() const {
    return m_lastActiveUnixMs;
}

void Session::SetCreatedUnixMs(std::uint64_t ms) {
    m_createdUnixMs = ms;
}

void Session::SetLastActiveUnixMs(std::uint64_t ms) {
    m_lastActiveUnixMs = ms;
}

bool Session::IsTerminated() const {
    return m_terminated;
}

void Session::MarkTerminated() {
    m_terminated = true;
    m_lastActiveUnixMs = NowUnixMs();
}

void Session::AddTurn(SessionTurn turn) {
    if (turn.turn_id == 0) {
        turn.turn_id = m_nextTurnId++;
    } else if (turn.turn_id >= m_nextTurnId) {
        m_nextTurnId = turn.turn_id + 1;
    }
    m_lastActiveUnixMs = (turn.unix_ms != 0) ? turn.unix_ms : NowUnixMs();
    m_turns.push_back(std::move(turn));
}

const std::vector<SessionTurn>& Session::Turns() const {
    return m_turns;
}

std::vector<SessionTurn>& Session::MutableTurns() {
    return m_turns;
}

void Session::TrimOldestTurns(std::size_t count) {
    if (count >= m_turns.size()) {
        m_turns.clear();
        m_lastActiveUnixMs = NowUnixMs();
        return;
    }

    m_turns.erase(m_turns.begin(), m_turns.begin() + static_cast<std::ptrdiff_t>(count));
    m_lastActiveUnixMs = NowUnixMs();
}

void Session::SetCompactionSummary(SessionTurn summary_turn) {
    if (summary_turn.turn_id == 0) {
        summary_turn.turn_id = m_nextTurnId++;
    } else if (summary_turn.turn_id >= m_nextTurnId) {
        m_nextTurnId = summary_turn.turn_id + 1;
    }
    summary_turn.is_summary = true;
    m_lastActiveUnixMs = (summary_turn.unix_ms != 0) ? summary_turn.unix_ms : NowUnixMs();
    m_compactionSummary = std::move(summary_turn);
}

const SessionTurn* Session::GetCompactionSummary() const {
    if (!m_compactionSummary.has_value()) {
        return nullptr;
    }
    return &m_compactionSummary.value();
}

void Session::SetSummary(std::string summary) {
    m_summary = std::move(summary);
    m_lastActiveUnixMs = NowUnixMs();
}

const std::string& Session::Summary() const {
    return m_summary;
}

const std::string& Session::AgentId() const {
    return m_agentId;
}

void Session::SetAgentId(std::string agentId) {
    m_agentId = std::move(agentId);
    m_lastActiveUnixMs = NowUnixMs();
}

const std::string& Session::ProviderId() const {
    return m_providerId;
}

void Session::SetProviderId(std::string providerId) {
    m_providerId = std::move(providerId);
    m_lastActiveUnixMs = NowUnixMs();
}

const std::string& Session::SessionType() const {
    return m_sessionType;
}

void Session::SetSessionType(std::string sessionType) {
    m_sessionType = std::move(sessionType);
}

std::uint64_t Session::NowUnixMs() {
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch());
    return static_cast<std::uint64_t>(ms.count());
}

SessionAccess::SessionAccess(std::shared_ptr<Session> session, SessionAccessMode mode)
    : m_session(std::move(session)), m_mode(mode) {
}

SessionAccessMode SessionAccess::Mode() const {
    return m_mode;
}

SessionAccess::operator bool() const {
    return static_cast<bool>(m_session);
}

SessionId SessionAccess::Id() const {
    return RequireSession().Id();
}

const SessionKey& SessionAccess::Key() const {
    return RequireSession().Key();
}

const std::vector<SessionTurn>& SessionAccess::Turns() const {
    return RequireSession().Turns();
}

const SessionTurn* SessionAccess::GetCompactionSummary() const {
    return RequireSession().GetCompactionSummary();
}

const std::string& SessionAccess::Summary() const {
    return RequireSession().Summary();
}

void SessionAccess::AddTurn(SessionTurn turn) {
    EnsureWritable("AddTurn");
    RequireSession().AddTurn(std::move(turn));
}

void SessionAccess::TrimOldestTurns(std::size_t count) {
    EnsureWritable("TrimOldestTurns");
    RequireSession().TrimOldestTurns(count);
}

void SessionAccess::SetCompactionSummary(SessionTurn summary_turn) {
    EnsureWritable("SetCompactionSummary");
    RequireSession().SetCompactionSummary(std::move(summary_turn));
}

const std::string& SessionAccess::AgentId() const {
    return RequireSession().AgentId();
}

void SessionAccess::SetAgentId(std::string agentId) {
    EnsureWritable("SetAgentId");
    RequireSession().SetAgentId(std::move(agentId));
}

const std::string& SessionAccess::ProviderId() const {
    return RequireSession().ProviderId();
}

void SessionAccess::SetProviderId(std::string providerId) {
    EnsureWritable("SetProviderId");
    RequireSession().SetProviderId(std::move(providerId));
}

const std::string& SessionAccess::SessionType() const {
    return RequireSession().SessionType();
}

void SessionAccess::SetSessionType(std::string sessionType) {
    EnsureWritable("SetSessionType");
    RequireSession().SetSessionType(std::move(sessionType));
}

void SessionAccess::SetSummary(std::string summary) {
    EnsureWritable("SetSummary");
    RequireSession().SetSummary(std::move(summary));
}

Session& SessionAccess::RequireSession() const {
    if (!m_session) {
        throw std::runtime_error("Session access violation: missing session");
    }
    return *m_session;
}

void SessionAccess::EnsureWritable(const char* action) const {
    if (m_mode == SessionAccessMode::ReadOnly) {
        throw std::runtime_error(std::string("Session access violation: ") + action
                                 + " on read-only session");
    }
}

} // namespace animus::kernel
