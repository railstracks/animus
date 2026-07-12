#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace animus::kernel {

struct Agent;
class SessionAccess;

// ============================================================================
// Context Block — a single contribution to the assembled system prompt
// ============================================================================

struct ContextBlock {
    std::string name;       // e.g. "identity", "session_notes", "active_memory"
    std::string content;    // The actual text content of this block
    int priority;           // Lower = earlier in assembly (identity=0, notes=50, etc.)
};

// ============================================================================
// IContextProvider — interface for anything that contributes to the preamble
// ============================================================================

class IContextProvider {
public:
    virtual ~IContextProvider() = default;

    // Human-readable name for this provider (used in admin API and logging).
    virtual std::string Name() const = 0;

    // Ordering hint — lower values assemble first.
    // Convention: 0=identity, 10-29=reserved, 30=active memory, 40=project,
    //             50=session notes, 60+=plugins.
    virtual int Priority() const = 0;

    // Produce a context block for the given agent and session.
    // Return std::nullopt if this provider has nothing to contribute
    // (e.g. no session notes exist for this session).
    virtual std::optional<ContextBlock> Provide(
        const Agent& agent,
        const SessionAccess& session) const = 0;
};

// ============================================================================
// Context Provider Registry
// ============================================================================

class ContextProviderRegistry {
public:
    // Register a provider. Takes ownership.
    void Register(std::unique_ptr<IContextProvider> provider);

    // Run all registered providers and return blocks sorted by priority
    // (ascending). Providers that return std::nullopt are skipped.
    // This is the full, uncached assembly — use for chat/chain execution.
    std::vector<ContextBlock> Assemble(
        const Agent& agent,
        const SessionAccess& session) const;

    // Cached assembly for admin preview endpoints (token-estimate, active
    // memory view). Returns the cached result if the session's turn count
    // hasn't changed since the last full Assemble for this agent+session.
    // Falls back to full Assemble (and populates cache) on miss.
    std::vector<ContextBlock> AssembleCached(
        const Agent& agent,
        const SessionAccess& session);

    // List registered provider names and priorities (for admin API).
    struct ProviderInfo {
        std::string name;
        int priority;
    };
    std::vector<ProviderInfo> ListProviders() const;

private:
    std::vector<std::unique_ptr<IContextProvider>> m_providers;

    // Cache for admin preview endpoints. Key: agent_id + session_id.
    struct CacheEntry {
        std::vector<ContextBlock> blocks;
        std::size_t turnCount{0};
    };
    mutable std::mutex m_cacheMutex;
    mutable std::unordered_map<std::string, CacheEntry> m_cache;
};

} // namespace animus::kernel
