#pragma once

#include <memory>
#include <string>

#include "animus_sdk/module_api.h"
#include "kernel/module/DynamicLibrary.h"

namespace animus::kernel::module {

struct LoadedModule {
    DynamicLibrary library;
    const animus_module_descriptor_t* descriptor{nullptr};
    animus_module_handle_t handle{nullptr};
    animus_module_destroy_fn destroy{nullptr};

    LoadedModule() = default;
    LoadedModule(const LoadedModule&) = delete;
    LoadedModule& operator=(const LoadedModule&) = delete;

    LoadedModule(LoadedModule&&) noexcept = default;
    LoadedModule& operator=(LoadedModule&&) noexcept = default;

    ~LoadedModule();

    bool IsValid() const { return descriptor != nullptr && destroy != nullptr; }
};

class ModuleLoader {
public:
    // Loads a single module from an explicit shared library path.
    //
    // On success, returns a LoadedModule that will destroy the module instance and unload
    // the library on destruction.
    std::unique_ptr<LoadedModule> Load(
        const std::string& path,
        const animus_host_vtable_t* host,
        void* host_ctx,
        std::string* error);
};

} // namespace animus::kernel::module
