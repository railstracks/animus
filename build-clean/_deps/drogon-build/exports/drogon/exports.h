
#ifndef DROGON_EXPORT_H
#define DROGON_EXPORT_H

#ifdef DROGON_STATIC_DEFINE
#  define DROGON_EXPORT
#  define DROGON_NO_EXPORT
#else
#  ifndef DROGON_EXPORT
#    ifdef drogon_EXPORTS
        /* We are building this library */
#      define DROGON_EXPORT __attribute__((visibility("default")))
#    else
        /* We are using this library */
#      define DROGON_EXPORT __attribute__((visibility("default")))
#    endif
#  endif

#  ifndef DROGON_NO_EXPORT
#    define DROGON_NO_EXPORT __attribute__((visibility("hidden")))
#  endif
#endif

#ifndef DROGON_DEPRECATED
#  define DROGON_DEPRECATED __attribute__ ((__deprecated__))
#endif

#ifndef DROGON_DEPRECATED_EXPORT
#  define DROGON_DEPRECATED_EXPORT DROGON_EXPORT DROGON_DEPRECATED
#endif

#ifndef DROGON_DEPRECATED_NO_EXPORT
#  define DROGON_DEPRECATED_NO_EXPORT DROGON_NO_EXPORT DROGON_DEPRECATED
#endif

#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef DROGON_NO_DEPRECATED
#    define DROGON_NO_DEPRECATED
#  endif
#endif

#endif /* DROGON_EXPORT_H */
