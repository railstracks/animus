#pragma once

#include "animus_kernel/ContextProviderRegistry.h"

namespace animus::kernel {

// Injects the agent's identity document as the first context block (priority 0).
// This is the "who you are" layer — always present, never truncated.

class IdentityProvider : public IContextProvider {
public:
    std::string Name() const override { return "identity"; }
    int Priority() const override { return 0; }

    std::optional<ContextBlock> Provide(
        const Agent& agent,
        const SessionAccess& session) const override;
};

} // namespace animus::kernel
