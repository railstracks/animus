#include "kernel/module/DynamicLibrary.h"

#if defined(_WIN32)
  #define NOMINMAX
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif

namespace animus::kernel::module {

DynamicLibrary::DynamicLibrary() : m_handle(nullptr), m_path() {}

DynamicLibrary::~DynamicLibrary() {
    Close();
}

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
    : m_handle(other.m_handle), m_path(std::move(other.m_path)) {
    other.m_handle = nullptr;
}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    Close();
    m_handle = other.m_handle;
    m_path = std::move(other.m_path);
    other.m_handle = nullptr;
    return *this;
}

bool DynamicLibrary::Open(const std::string& path, std::string* error) {
    Close();

#if defined(_WIN32)
    HMODULE h = LoadLibraryA(path.c_str());
    if (!h) {
        if (error) {
            *error = "LoadLibrary failed";
        }
        return false;
    }
    m_handle = reinterpret_cast<void*>(h);
#else
    void* h = dlopen(path.c_str(), RTLD_NOW);
    if (!h) {
        if (error) {
            const char* e = dlerror();
            *error = e ? e : "dlopen failed";
        }
        return false;
    }
    m_handle = h;
#endif

    m_path = path;
    return true;
}

void DynamicLibrary::Close() {
    if (!m_handle) {
        return;
    }

#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(m_handle));
#else
    dlclose(m_handle);
#endif

    m_handle = nullptr;
    m_path.clear();
}

void* DynamicLibrary::GetSymbol(const char* symbol, std::string* error) const {
    if (!m_handle) {
        if (error) {
            *error = "library not open";
        }
        return nullptr;
    }

#if defined(_WIN32)
    FARPROC proc = GetProcAddress(reinterpret_cast<HMODULE>(m_handle), symbol);
    if (!proc) {
        if (error) {
            *error = "GetProcAddress failed";
        }
        return nullptr;
    }
    return reinterpret_cast<void*>(proc);
#else
    dlerror();
    void* sym = dlsym(m_handle, symbol);
    const char* e = dlerror();
    if (e != nullptr) {
        if (error) {
            *error = e;
        }
        return nullptr;
    }
    return sym;
#endif
}

bool DynamicLibrary::IsOpen() const {
    return m_handle != nullptr;
}

const std::string& DynamicLibrary::Path() const {
    return m_path;
}

} // namespace animus::kernel::module
