#pragma once

#include "animus_kernel/tools/ToolRegistry.h"

#include <random>
#include <string>

namespace animus::kernel {

// ============================================================================
// DiceTool — RNG-based dice rolling
//
// Supports standard NdS±M notation with optional seeded rolls.
// Ticket 085.
// ============================================================================

class DiceTool : public IToolHandler {
public:
    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

private:
    struct DiceNotation {
        int count{1};      // N in NdS
        int sides{6};       // S in NdS
        int modifier{0};    // ±M
        bool valid{false};
        std::string error;
    };

    DiceNotation ParseNotation(const std::string& notation) const;
    std::uint64_t HashSeed(const std::string& seed) const;
};

} // namespace animus::kernel
