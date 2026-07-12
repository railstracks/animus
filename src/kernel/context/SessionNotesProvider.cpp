#include "animus_kernel/context/SessionNotesProvider.h"
#include "animus_kernel/AgentStore.h"
#include "animus_kernel/Session.h"
#include "animus_kernel/SessionNotesStore.h"

namespace animus::kernel {

SessionNotesProvider::SessionNotesProvider(const SessionNotesStore* store)
    : m_store(store) {}

std::optional<ContextBlock> SessionNotesProvider::Provide(
        const Agent& agent,
        const SessionAccess& session) const {
    if (!m_store) return std::nullopt;
    if (agent.id.empty()) return std::nullopt;

    const std::string sessionKey = session.Key().ToString();
    auto notes = m_store->ListForSession(sessionKey, agent.id);
    if (notes.empty()) return std::nullopt;

    std::string content;
    for (const auto& note : notes) {
        content += "- " + note.bullet + "\n";
    }

    ContextBlock block;
    block.name = "Session Notes";
    block.content = content;
    block.priority = 50;
    return block;
}

} // namespace animus::kernel
