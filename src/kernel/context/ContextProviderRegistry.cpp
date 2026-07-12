#include "animus_kernel/ContextProviderRegistry.h"

#include <algorithm>
#include <iostream>
#include <string>

#include "animus_kernel/AgentStore.h"
#include "animus_kernel/Session.h"

namespace animus::kernel {

void ContextProviderRegistry::Register(std::unique_ptr<IContextProvider> provider) {
    if (!provider) return;
    m_providers.push_back(std::move(provider));
    // Keep sorted by priority for deterministic ordering.
    std::sort(m_providers.begin(), m_providers.end(),
        [](const auto& a, const auto& b) {
            return a->Priority() < b->Priority();
        });
}

std::vector<ContextBlock> ContextProviderRegistry::Assemble(
        const Agent& agent,
        const SessionAccess& session) const {
    std::vector<ContextBlock> blocks;
    blocks.reserve(m_providers.size());

    for (const auto& provider : m_providers) {
        std::cerr << "[context] Assembling provider: " << provider->Name() << " (priority=" << provider->Priority() << ")" << std::endl;
        auto block = provider->Provide(agent, session);
        std::cerr << "[context] Provider " << provider->Name() << " done" << std::endl;
        if (block) {
            blocks.push_back(std::move(*block));
        }
    }

    // Final sort by priority (providers are already sorted, but
    // individual blocks may have different priorities).
    std::sort(blocks.begin(), blocks.end(),
        [](const ContextBlock& a, const ContextBlock& b) {
            return a.priority < b.priority;
        });

    // Update cache for admin preview endpoints
    {
        std::string cacheKey = agent.id + ":" + std::to_string(session.Id());
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_cache[cacheKey] = CacheEntry{blocks, session.Turns().size()};
    }

    return blocks;
}

std::vector<ContextBlock> ContextProviderRegistry::AssembleCached(
        const Agent& agent,
        const SessionAccess& session) {
    std::string cacheKey = agent.id + ":" + std::to_string(session.Id());
    std::size_t turnCount = session.Turns().size();

    // Check cache
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto it = m_cache.find(cacheKey);
        if (it != m_cache.end() && it->second.turnCount == turnCount) {
            // Cache hit — return cached blocks without recomputing
            return it->second.blocks;
        }
    }

    // Cache miss — full assembly (also updates cache)
    std::cerr << "[context] Cache miss for key=" << cacheKey
              << " turns=" << turnCount << " — full assembly" << std::endl;
    return Assemble(agent, session);
}

std::vector<ContextProviderRegistry::ProviderInfo>
ContextProviderRegistry::ListProviders() const {
    std::vector<ProviderInfo> infos;
    infos.reserve(m_providers.size());
    for (const auto& p : m_providers) {
        infos.push_back({p->Name(), p->Priority()});
    }
    return infos;
}

} // namespace animus::kernel
