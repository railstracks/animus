#include "animus_sdk/module_api.h"

#include <cstring>
#include <new>

namespace {

struct HelloModule {
    int dummy;
};

static const animus_module_descriptor_t kDescriptor = {
    /*struct_size=*/sizeof(animus_module_descriptor_t),
    /*module_api_version=*/ANIMUS_MODULE_API_VERSION,
    /*id=*/ANIMUS_SV_LITERAL("example.hello"),
    /*name=*/ANIMUS_SV_LITERAL("Example Hello Module"),
    /*version_major=*/0,
    /*version_minor=*/1,
    /*version_patch=*/0,
    /*capabilities=*/0u,
};

static animus_string_view Sv(const char* s) {
    if (!s) {
        return animus_string_view{nullptr, 0u};
    }
    return animus_string_view{s, std::strlen(s)};
}

static void LogIfPossible(const animus_host_vtable_t* host, void* host_ctx, animus_log_level_t lvl, const char* msg) {
    if (!host) {
        return;
    }
    if (host->struct_size < sizeof(animus_host_vtable_t)) {
        // Current version expects full vtable size.
        return;
    }
    if (!host->log) {
        return;
    }
    host->log(host_ctx, lvl, Sv(msg));
}

} // namespace

extern "C" {

ANIMUS_EXPORT const animus_module_descriptor_t* animus_module_get_descriptor() {
    return &kDescriptor;
}

ANIMUS_EXPORT animus_module_handle_t animus_module_create(
    const animus_host_vtable_t* host,
    void* host_ctx) {

    try {
        LogIfPossible(host, host_ctx, ANIMUS_LOG_INFO, "example.hello: create()");
        HelloModule* m = new (std::nothrow) HelloModule();
        if (!m) {
            LogIfPossible(host, host_ctx, ANIMUS_LOG_ERROR, "example.hello: allocation failed");
            return nullptr;
        }
        m->dummy = 42;
        return reinterpret_cast<animus_module_handle_t>(m);
    } catch (...) {
        // Never let exceptions cross the DLL boundary.
        return nullptr;
    }
}

ANIMUS_EXPORT void animus_module_destroy(animus_module_handle_t handle) {
    HelloModule* m = reinterpret_cast<HelloModule*>(handle);
    delete m;
}

} // extern "C"
