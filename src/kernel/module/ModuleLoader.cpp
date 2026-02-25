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

static bool DescriptorLooksSane(const animus_module_descriptor_t* d) {
    if (!d) {
        return false;
    }
    if (d->struct_size < sizeof(animus_module_descriptor_t)) {
        // We currently require the full size; we can relax this later.
        return false;
    }
    if (!d->id.data || d->id.size == 0) {
        return false;
    }
    return true;
}

std::unique_ptr<LoadedModule> ModuleLoader::Load(
    const std::string& path,
    const animus_host_vtable_t* host,
    void* host_ctx,
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

    // Instantiate module.
    animus_module_handle_t h = createFn(host, host_ctx);
    if (!h) {
        if (error) {
            *error = "module create returned null";
        }
        return nullptr;
    }

    out->descriptor = d;
    out->handle = h;
    out->destroy = destroyFn;

    return out;
}

} // namespace animus::kernel::module
