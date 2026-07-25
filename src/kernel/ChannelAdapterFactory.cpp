#include "animus_kernel/IChannelAdapter.h"

#include <iostream>

#include "animus_kernel/ChannelManager.h"

namespace animus::kernel {

// ============================================================================
// Static config validation — delegates to ChannelManager's implementation
// ============================================================================

bool IChannelAdapter::ValidateConfig(const std::string& type,
                                     const Json::Value& config,
                                     std::string* error) {
    return ChannelManager::ValidateConfig(type, config, error);
}

// ============================================================================
// Factory — currently returns nullptr because connector implementations
// are still inline in ChannelManager. As adapters are extracted into
// separate classes, this factory will create them directly.
//
// The interface boundary is established now so that:
//   1. New connectors can be added as IChannelAdapter implementations
//   2. ChannelManager's Start/Stop/SendReply dispatch can gradually
//      transition from if-else chains to adapter lookup
//   3. Tests can mock individual connectors
// ============================================================================

std::unique_ptr<IChannelAdapter> IChannelAdapter::Create(
    const std::string& /*type*/,
    HttpClient& /*httpClient*/,
    AgentConfigStore* /*configStore*/,
    ChannelDispatchCallback /*dispatch*/,
    ChannelLogCallback /*logCallback*/) {
    // TODO: Implement once connectors are extracted from ChannelManager.
    // For now, ChannelManager handles all connector lifecycle internally.
    return nullptr;
}

} // namespace animus::kernel
