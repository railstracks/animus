#include "kernel/module/ModuleManager.h"

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace animus::kernel::module {

static std::string ToString(animus_string_view sv) {
    return std::string(sv.data ? sv.data : "", sv.size);
}

static bool LooksLikeSharedLibrary(const fs::path& p) {
    if (!fs::is_regular_file(p)) {
        return false;
    }

#if defined(_WIN32)
    return p.extension() == ".dll";
#elif defined(__APPLE__)
    return p.extension() == ".dylib";
#else
    return p.extension() == ".so";
#endif
}

ModuleManager::ModuleManager() = default;

ModuleManager::~ModuleManager() {
    UnloadAll();
}

bool ModuleManager::IsAllowed(const kernel::KernelConfig& config, const std::string& moduleId) const {
    if (!config.allowModuleIds.empty()) {
        return std::find(config.allowModuleIds.begin(), config.allowModuleIds.end(), moduleId) !=
               config.allowModuleIds.end();
    }
    return config.allowAllModulesByDefault;
}

bool ModuleManager::LoadOnePath(
    const kernel::KernelConfig& config,
    const std::string& path,
    const animus_host_vtable_t* host,
    void* host_ctx,
    std::string* error) {

    std::string loadErr;
    auto mod = m_loader.LoadLibrary(path, &loadErr);
    if (!mod) {
        if (error) {
            *error = "failed to load module library '" + path + "': " + loadErr;
        }
        return false;
    }

    const std::string id = ToString(mod->descriptor->id);

    // Dedup by id.
    if (m_loadedIds.find(id) != m_loadedIds.end()) {
        return true;
    }

    if (!IsAllowed(config, id)) {
        // Not allowed; skip instantiation.
        return true;
    }

    std::string createErr;
    if (!m_loader.CreateInstance(*mod, host, host_ctx, &createErr)) {
        if (error) {
            *error = "failed to instantiate module '" + id + "': " + createErr;
        }
        return false;
    }

    m_loadedIds.insert(id);
    m_loaded.push_back(std::move(mod));

    std::cerr << "[kernel] loaded module id='" << id << "' from " << path << "\n";
    return true;
}

bool ModuleManager::LoadFromConfig(
    const kernel::KernelConfig& config,
    const animus_host_vtable_t* host,
    void* host_ctx,
    std::string* error) {

    // Explicit module paths first.
    for (const auto& p : config.modulePaths) {
        if (!LoadOnePath(config, p, host, host_ctx, error)) {
            return false;
        }
    }

    // Scan directories.
    for (const auto& dir : config.moduleDirs) {
        std::error_code ec;
        if (!fs::exists(dir, ec)) {
            continue;
        }

        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (ec) {
                break;
            }

            const fs::path p = entry.path();
            if (!LooksLikeSharedLibrary(p)) {
                continue;
            }

            if (!LoadOnePath(config, p.string(), host, host_ctx, error)) {
                return false;
            }
        }
    }

    return true;
}

void ModuleManager::UnloadAll() {
    m_loaded.clear();
    m_loadedIds.clear();
}

std::size_t ModuleManager::LoadedCount() const {
    return m_loaded.size();
}

std::vector<kernel::KernelConfig::InterfaceModuleInfo> ModuleManager::ListLoadedInterfaceModules() const {
    std::vector<kernel::KernelConfig::InterfaceModuleInfo> interfaces;
    interfaces.reserve(m_loaded.size());
    for (const auto& loaded : m_loaded) {
        if (!loaded || !loaded->descriptor) {
            continue;
        }
        const std::string moduleId = ToString(loaded->descriptor->id);
        kernel::KernelConfig::InterfaceModuleInfo info{};
        info.moduleId = moduleId;
        info.name = ToString(loaded->descriptor->name);
        if (moduleId.rfind("connector.", 0) == 0) {
            info.type = moduleId.substr(std::string("connector.").size());
        } else {
            info.type = "generic";
        }
        interfaces.push_back(std::move(info));
    }
    return interfaces;
}

} // namespace animus::kernel::module
