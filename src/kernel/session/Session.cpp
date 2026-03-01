#include "animus_kernel/Session.h"

#include <stdexcept>
#include <sstream>

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
}

SessionId Session::Id() const {
    return m_id;
}

const SessionKey& Session::Key() const {
    return m_key;
}

void Session::AddTurn(SessionTurn turn) {
    m_turns.push_back(std::move(turn));
}

const std::vector<SessionTurn>& Session::Turns() const {
    return m_turns;
}

void Session::SetSummary(std::string summary) {
    m_summary = std::move(summary);
}

const std::string& Session::Summary() const {
    return m_summary;
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

const std::string& SessionAccess::Summary() const {
    return RequireSession().Summary();
}

void SessionAccess::AddTurn(SessionTurn turn) {
    EnsureWritable("AddTurn");
    RequireSession().AddTurn(std::move(turn));
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
