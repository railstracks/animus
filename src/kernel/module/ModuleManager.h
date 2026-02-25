#pragma once

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "animus_kernel/KernelConfig.h"
#include "kernel/module/ModuleLoader.h"

namespace animus::kernel::module {

class ModuleManager {
public:
    ModuleManager();
    ~ModuleManager();

    ModuleManager(const ModuleManager&) = delete;
    ModuleManager& operator=(const ModuleManager&) = delete;

    bool LoadFromConfig(
        const kernel::KernelConfig& config,
        const animus_host_vtable_t* host,
        void* host_ctx,
        std::string* error);

    void UnloadAll();

    std::size_t LoadedCount() const;

private:
    bool IsAllowed(const kernel::KernelConfig& config, const std::string& moduleId) const;

    bool LoadOnePath(
        const kernel::KernelConfig& config,
        const std::string& path,
        const animus_host_vtable_t* host,
        void* host_ctx,
        std::string* error);

private:
    ModuleLoader m_loader;
    std::vector<std::unique_ptr<LoadedModule>> m_loaded;
    std::unordered_set<std::string> m_loadedIds;
};

} // namespace animus::kernel::module
