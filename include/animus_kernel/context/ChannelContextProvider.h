#pragma once

#include "animus_kernel/ContextProviderRegistry.h"
#include <string>

namespace animus::kernel {

class ChannelContextProvider : public IContextProvider {
public:
    std::string Name() const override;
    int Priority() const override;
    std::optional<ContextBlock> Provide(
        const Agent& agent,
        const SessionAccess& session) const override;

private:
    std::string BuildChannelContext(const std::string& channelName) const;
};

} // namespace animus::kernel
