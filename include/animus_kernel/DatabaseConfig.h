#pragma once

#include "animus_kernel/KernelConfig.h"
#include <string>

namespace animus::kernel {

struct DatabaseConfig {
    std::string backend{"sqlite"};  // "sqlite" or "postgresql"

    // SQLite settings
    std::string sqlitePath{"state/memory.db"};

    // PostgreSQL settings
    std::string pgHost{"localhost"};
    int pgPort{5432};
    std::string pgDatabase{"animus"};
    std::string pgUsername{"animus"};
    std::string pgPassword;
    int pgPoolSize{10};

    /// Load from a JSON file. Returns true on success, false if file not found.
    /// Missing fields use defaults. CLI args override loaded values.
    bool LoadFromFile(const std::string& path);

    /// Apply loaded config to KernelConfig fields.
    /// Only overwrites fields that haven't been explicitly set via CLI.
    void ApplyTo(KernelConfig& config) const;
};

} // namespace animus::kernel