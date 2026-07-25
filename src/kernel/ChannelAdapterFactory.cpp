#include "animus_kernel/IChannelAdapter.h"
#include "animus_kernel/ChannelAdapters.h"

#include <iostream>

#include "animus_kernel/AgentConfigStore.h"
#include "animus_kernel/ChannelManager.h"

namespace animus::kernel {

bool IChannelAdapter::ValidateConfig(const std::string& type,
                                     const Json::Value& config,
                                     std::string* error) {
    return ChannelManager::ValidateConfig(type, config, error);
}

std::unique_ptr<IChannelAdapter> IChannelAdapter::Create(
    const std::string& type,
    HttpClient& httpClient,
    AgentConfigStore* configStore,
    ChannelDispatchCallback dispatch,
    ChannelLogCallback logCallback) {

    // Build the shared context
    // Note: ChannelRouter is owned by ChannelManager; adapters receive a
    // reference to it through the context. The factory doesn't own the router.
    // The caller (ChannelManager) must set ctx.router before calling Start().

    // TODO: once ChannelManager passes the router through, use it.
    // For now, these adapters need the router from ChannelManager.
    // This will be wired up when ChannelManager::StartChannel is refactored.

    (void)httpClient;
    (void)configStore;
    (void)dispatch;
    (void)logCallback;

    return nullptr;
}

} // namespace animus::kernel
