#pragma once

#include "animus_kernel/ContextProviderRegistry.h"

namespace animus::kernel {

class AgendaStore;

// ============================================================================
// TemporalContextProvider — injects current date/time + upcoming agenda events
// ============================================================================

class TemporalContextProvider : public IContextProvider {
public:
    explicit TemporalContextProvider(AgendaStore* agendaStore);

    std::string Name() const override { return "Temporal Context"; }
    int Priority() const override { return 10; }

    std::optional<ContextBlock> Provide(
        const Agent& agent,
        const SessionAccess& session) const override;

private:
    AgendaStore* m_agendaStore;
};

} // namespace animus::kernel