#pragma once

#include "animus_kernel/ContextProviderRegistry.h"

namespace animus::kernel {

// Injects runtime environment information (host, OS, CPU, RAM, node, agent
// budget) as a context block at priority 5 — after identity (0), before
// active memory (30). All values are static for the lifetime of a chain.

class RuntimeEnvironmentProvider : public IContextProvider {
public:
    std::string Name() const override { return "runtime_environment"; }
    int Priority() const override { return 5; }

    std::optional<ContextBlock> Provide(
        const Agent& agent,
        const SessionAccess& session) const override;
};

} // namespace animus::kernel
