#pragma once

#include "animus_kernel/ContextProviderRegistry.h"

namespace animus::kernel {

class SessionNotesStore;

// Injects per-session persistent notes as a context block (priority 50).
// Notes are agent-scoped working memory for the current conversation.

class SessionNotesProvider : public IContextProvider {
public:
    explicit SessionNotesProvider(const SessionNotesStore* store);

    std::string Name() const override { return "session_notes"; }
    int Priority() const override { return 50; }

    std::optional<ContextBlock> Provide(
        const Agent& agent,
        const SessionAccess& session) const override;

private:
    const SessionNotesStore* m_store;
};

} // namespace animus::kernel
