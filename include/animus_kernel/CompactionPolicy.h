#pragma once

#include <cstddef>

namespace animus::kernel {

enum class CompactionStrategy {
    Truncate,
    Summarize,
};

struct CompactionPolicy {
    std::size_t maxTurnsBeforeCompaction{50};
    std::size_t maxTokenEstimateBeforeCompaction{100000};
    CompactionStrategy compactionStrategy{CompactionStrategy::Summarize};
};

} // namespace animus::kernel
