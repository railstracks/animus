#include "animus_kernel/Log.h"
#include "animus_kernel/ChannelAdapters.h"
#include "animus_kernel/ChannelContext.h"

#include <iostream>

#include "animus_kernel/ChannelState.h"
#include "animus_kernel/AgentConfigStore.h"
#include "animus_kernel/ChannelManager.h"

namespace animus::kernel {

// ============================================================================
// PollerAdapterBase
// ============================================================================

bool PollerAdapterBase::Start(const ChannelState& state, std::string* error) {
    using namespace channel_detail;

    m_runtime = std::make_unique<PollerRuntime>();
    m_runtime->channel_name = state.name;
    m_runtime->channel_type = state.type;
    m_runtime->config = state.config;
    m_runtime->next_attempt = std::chrono::steady_clock::now();
    m_runtime->agent_id = GetString(state.config, "agent_id", "");

    // Restore persisted state from config store
    if (m_ctx.configStore) {
        // Telegram: restore last_update_id
        std::string stored = m_ctx.configStore->Get("",
            "channel." + state.name + ".polling.last_update_id");
        if (!stored.empty()) {
            try { m_runtime->last_update_id = std::stoll(stored); } catch (...) {}
        }
        // VK: restore long poll state
        m_runtime->lp_ts = m_ctx.configStore->Get("",
            "channel." + state.name + ".polling.ts");
        m_runtime->lp_key = m_ctx.configStore->Get("",
            "channel." + state.name + ".polling.key");
        m_runtime->lp_server = m_ctx.configStore->Get("",
            "channel." + state.name + ".polling.server");
        // Slack: restore latest_ts
        m_runtime->lp_ts = m_ctx.configStore->Get("",
            "channel." + state.name + ".polling.latest_ts");
        // Nextcloud: restore last_room_sync
        m_runtime->lp_ts = m_ctx.configStore->Get("",
            "channel." + state.name + ".polling.last_room_sync");
        // Email: restore last_before
        m_runtime->lp_ts = m_ctx.configStore->Get(m_runtime->agent_id,
            "channel." + state.name + ".polling.last_before");
    }

    // Subclass-specific init
    OnInit(state);

    m_runtime->active = true;
    m_stopRequested = false;
    m_runtime->thread = std::thread(&PollerAdapterBase::RunLoop, this);

    return true;
}

void PollerAdapterBase::Stop() {
    if (!m_runtime) return;
    m_stopRequested = true;
    m_runtime->active = false;
    if (m_runtime->thread.joinable()) {
        m_runtime->thread.join();
    }
}

bool PollerAdapterBase::IsConnected() const {
    return m_runtime && m_runtime->active;
}

void PollerAdapterBase::Dispatch(const std::string& routingKey,
                                  const std::string& message,
                                  const std::string& sessionType) {
    ChannelDispatch::Dispatch(m_ctx, m_runtime.get(), routingKey, message, sessionType);
}

void PollerAdapterBase::Log(const std::string& routingKey,
                             const std::string& message,
                             const std::string& sessionType) {
    ChannelDispatch::Log(m_ctx, m_runtime.get(), routingKey, message, sessionType);
}

} // namespace animus::kernel
