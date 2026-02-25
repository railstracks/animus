#include "animus_kernel/Session.h"

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

} // namespace animus::kernel
