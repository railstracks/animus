#pragma once

#include <memory>
#include <string>

#include "animus_sdk/module_api.h"
#include "kernel/module/DynamicLibrary.h"

namespace animus::kernel::module {

struct LoadedModule {
    DynamicLibrary library;
    const animus_module_descriptor_t* descriptor{nullptr};

    animus_module_create_fn create{nullptr};
    animus_module_destroy_fn destroy{nullptr};

    animus_module_handle_t handle{nullptr};

    LoadedModule() = default;
    LoadedModule(const LoadedModule&) = delete;
    LoadedModule& operator=(const LoadedModule&) = delete;

    LoadedModule(LoadedModule&&) noexcept = default;
    LoadedModule& operator=(LoadedModule&&) noexcept = default;

    ~LoadedModule();

    bool IsValid() const { return descriptor != nullptr && create != nullptr && destroy != nullptr; }
    bool IsInstantiated() const { return handle != nullptr; }
};

class ModuleLoader {
public:
    // Loads a module shared library and validates its descriptor/symbols.
    // Does NOT instantiate the module.
    std::unique_ptr<LoadedModule> LoadLibrary(
        const std::string& path,
        std::string* error);

    // Instantiates a loaded module.
    bool CreateInstance(
        LoadedModule& module,
        const animus_host_vtable_t* host,
        void* host_ctx,
        std::string* error);

    // Convenience wrapper: LoadLibrary() + CreateInstance().
    std::unique_ptr<LoadedModule> Load(
        const std::string& path,
        const animus_host_vtable_t* host,
        void* host_ctx,
        std::string* error);
};

} // namespace animus::kernel::module
