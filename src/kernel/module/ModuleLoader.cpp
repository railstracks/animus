#include "kernel/module/ModuleLoader.h"

#include <cstddef>
#include <cstdint>

#include "animus_sdk/abi.h"

namespace animus::kernel::module {

LoadedModule::~LoadedModule() {
    if (handle && destroy) {
        destroy(handle);
        handle = nullptr;
    }
    // library closes itself
}

static constexpr std::size_t kMinDescriptorSize =
    offsetof(animus_module_descriptor_t, version_patch) + sizeof(uint32_t);

static constexpr std::size_t kMinHostVtableSize =
    offsetof(animus_host_vtable_t, log) + sizeof(void*);

static bool DescriptorLooksSane(const animus_module_descriptor_t* d) {
    if (!d) {
        return false;
    }

    // ABI rule: structs may be extended by appending fields.
    // Therefore, accept older (smaller) descriptors as long as the prefix we
    // rely on is present.
    if (d->struct_size < kMinDescriptorSize) {
        return false;
    }

    if (d->module_api_version == 0u) {
        return false;
    }

    if (!d->id.data || d->id.size == 0u) {
        return false;
    }

    return true;
}

std::unique_ptr<LoadedModule> ModuleLoader::LoadLibrary(
    const std::string& path,
    std::string* error) {

    auto out = std::make_unique<LoadedModule>();

    std::string openErr;
    if (!out->library.Open(path, &openErr)) {
        if (error) {
            *error = openErr;
        }
        return nullptr;
    }

    std::string symErr;
    auto getDesc = reinterpret_cast<animus_module_get_descriptor_fn>(
        out->library.GetSymbol(ANIMUS_MODULE_GET_DESCRIPTOR_SYMBOL, &symErr));
    if (!getDesc) {
        if (error) {
            *error = "missing symbol " ANIMUS_MODULE_GET_DESCRIPTOR_SYMBOL ": " + symErr;
        }
        return nullptr;
    }

    auto createFn = reinterpret_cast<animus_module_create_fn>(
        out->library.GetSymbol(ANIMUS_MODULE_CREATE_SYMBOL, &symErr));
    if (!createFn) {
        if (error) {
            *error = "missing symbol " ANIMUS_MODULE_CREATE_SYMBOL ": " + symErr;
        }
        return nullptr;
    }

    auto destroyFn = reinterpret_cast<animus_module_destroy_fn>(
        out->library.GetSymbol(ANIMUS_MODULE_DESTROY_SYMBOL, &symErr));
    if (!destroyFn) {
        if (error) {
            *error = "missing symbol " ANIMUS_MODULE_DESTROY_SYMBOL ": " + symErr;
        }
        return nullptr;
    }

    const animus_module_descriptor_t* d = getDesc();
    if (!DescriptorLooksSane(d)) {
        if (error) {
            *error = "module descriptor invalid";
        }
        return nullptr;
    }

    if (d->module_api_version != ANIMUS_MODULE_API_VERSION) {
        if (error) {
            *error = "module API version mismatch";
        }
        return nullptr;
    }

    out->descriptor = d;
    out->create = createFn;
    out->destroy = destroyFn;

    return out;
}

bool ModuleLoader::CreateInstance(
    LoadedModule& module,
    const animus_host_vtable_t* host,
    void* host_ctx,
    std::string* error) {

    if (!module.IsValid()) {
        if (error) {
            *error = "module not valid";
        }
        return false;
    }

    if (module.handle != nullptr) {
        if (error) {
            *error = "module already instantiated";
        }
        return false;
    }

    if (host && host->struct_size < kMinHostVtableSize) {
        if (error) {
            *error = "host vtable too small";
        }
        return false;
    }

    animus_module_handle_t h = module.create(host, host_ctx);
    if (!h) {
        if (error) {
            *error = "module create returned null";
        }
        return false;
    }

    module.handle = h;
    return true;
}

std::unique_ptr<LoadedModule> ModuleLoader::Load(
    const std::string& path,
    const animus_host_vtable_t* host,
    void* host_ctx,
    std::string* error) {

    auto mod = LoadLibrary(path, error);
    if (!mod) {
        return nullptr;
    }

    std::string createErr;
    if (!CreateInstance(*mod, host, host_ctx, &createErr)) {
        if (error) {
            *error = createErr;
        }
        return nullptr;
    }

    return mod;
}

} // namespace animus::kernel::module
