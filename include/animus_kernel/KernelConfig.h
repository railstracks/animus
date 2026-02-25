#pragma once

#include <string>
#include <vector>

#include "animus/Jobs.h"

namespace animus::kernel {

// KernelConfig is the host-facing configuration surface for bootstrapping the kernel.
struct KernelConfig {
    // Job system
    jobs::JobSystemConfig jobSystemConfig{};

    // Module loading
    std::vector<std::string> modulePaths; // explicit module shared library paths
    std::vector<std::string> moduleDirs;  // directories to scan for modules

    // Allowlist policy
    //
    // - If allowModuleIds is non-empty: only those module ids may be instantiated.
    // - If allowModuleIds is empty: allowAllModulesByDefault decides.
    std::vector<std::string> allowModuleIds;
    bool allowAllModulesByDefault{true};
};

} // namespace animus::kernel
