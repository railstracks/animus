#ifndef ANIMUS_SDK_EXPORTS_H
#define ANIMUS_SDK_EXPORTS_H

// Cross-platform symbol visibility for shared libraries.

#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(ANIMUS_SDK_BUILDING_DLL)
    #define ANIMUS_EXPORT __declspec(dllexport)
  #else
    #define ANIMUS_EXPORT __declspec(dllimport)
  #endif
#else
  #if defined(__GNUC__)
    #define ANIMUS_EXPORT __attribute__((visibility("default")))
  #else
    #define ANIMUS_EXPORT
  #endif
#endif

#endif // ANIMUS_SDK_EXPORTS_H
