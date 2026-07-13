#pragma once

#include <cstddef>
#include <string>

namespace animus::kernel {

// Rough token estimator: ~3.3 chars per token for English/mixed content.
// Used for budget checks, context window gauges, and consolidation intake sizing.
// This is the single source of truth for token estimation in Animus.
struct TokenEstimate {
    static std::size_t Estimate(const std::string& text) {
        if (text.empty()) return 0;
        return text.size() / 3 + 1;
    }
};

} // namespace animus::kernel
