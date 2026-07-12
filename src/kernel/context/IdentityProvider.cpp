#include "animus_kernel/context/IdentityProvider.h"
#include "animus_kernel/AgentStore.h"
#include "animus_kernel/Session.h"

namespace animus::kernel {

std::optional<ContextBlock> IdentityProvider::Provide(
        const Agent& agent,
        const SessionAccess& /*session*/) const {
    if (agent.identity.empty()) return std::nullopt;

    ContextBlock block;
    block.name = "Identity";
    block.content = agent.identity;
    block.priority = 0;
    return block;
}

} // namespace animus::kernel
