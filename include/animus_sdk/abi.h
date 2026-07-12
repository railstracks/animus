#ifndef ANIMUS_SDK_ABI_H
#define ANIMUS_SDK_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Increment this when the module ABI changes in an incompatible way.
#define ANIMUS_MODULE_API_VERSION 1u

typedef struct animus_string_view {
    const char* data;
    size_t size;
} animus_string_view;

#define ANIMUS_SV_LITERAL(str_lit) ((animus_string_view){ (str_lit), sizeof(str_lit) - 1u })

typedef enum animus_log_level_t {
    ANIMUS_LOG_DEBUG = 0,
    ANIMUS_LOG_INFO = 1,
    ANIMUS_LOG_WARN = 2,
    ANIMUS_LOG_ERROR = 3
} animus_log_level_t;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ANIMUS_SDK_ABI_H
