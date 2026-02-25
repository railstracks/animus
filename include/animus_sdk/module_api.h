#ifndef ANIMUS_SDK_MODULE_API_H
#define ANIMUS_SDK_MODULE_API_H

#include "animus_sdk/abi.h"
#include "animus_sdk/exports.h"

#ifdef __cplusplus
extern "C" {
#endif

// Opaque module instance handle owned by the module.
typedef void* animus_module_handle_t;

// Kernel-provided host surface for modules.
//
// ABI stability rule: this struct may only be extended by appending fields.
// The `struct_size` allows modules to detect available functions.
typedef struct animus_host_vtable_t {
    uint32_t struct_size;

    void (*log)(
        void* host_ctx,
        animus_log_level_t level,
        animus_string_view message);

    // Future extension points (must be appended):
    // - registry registration
    // - config access
    // - job scheduling proxy
} animus_host_vtable_t;

// Module capabilities bitflags (future-facing; minimal for now).
typedef uint64_t animus_module_capabilities_t;

// Self-descriptor returned by the module.
//
// ABI stability rule: this struct may only be extended by appending fields.
typedef struct animus_module_descriptor_t {
    uint32_t struct_size;

    uint32_t module_api_version; // must match ANIMUS_MODULE_API_VERSION

    animus_string_view id;   // stable identifier, e.g. "connector.slack"
    animus_string_view name; // human readable

    uint32_t version_major;
    uint32_t version_minor;
    uint32_t version_patch;

    animus_module_capabilities_t capabilities;
} animus_module_descriptor_t;

// Exported function signatures.
typedef const animus_module_descriptor_t* (*animus_module_get_descriptor_fn)();
typedef animus_module_handle_t (*animus_module_create_fn)(
    const animus_host_vtable_t* host,
    void* host_ctx);
typedef void (*animus_module_destroy_fn)(animus_module_handle_t handle);

// Exported symbol names (exactly these names for dlsym/GetProcAddress).
#define ANIMUS_MODULE_GET_DESCRIPTOR_SYMBOL "animus_module_get_descriptor"
#define ANIMUS_MODULE_CREATE_SYMBOL "animus_module_create"
#define ANIMUS_MODULE_DESTROY_SYMBOL "animus_module_destroy"

// Required exports.
ANIMUS_EXPORT const animus_module_descriptor_t* animus_module_get_descriptor();
ANIMUS_EXPORT animus_module_handle_t animus_module_create(
    const animus_host_vtable_t* host,
    void* host_ctx);
ANIMUS_EXPORT void animus_module_destroy(animus_module_handle_t handle);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ANIMUS_SDK_MODULE_API_H
