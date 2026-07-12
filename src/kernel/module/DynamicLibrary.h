#pragma once

#include <string>

namespace animus::kernel::module {

// Minimal cross-platform shared library wrapper.
class DynamicLibrary {
public:
    DynamicLibrary();
    ~DynamicLibrary();

    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    DynamicLibrary(DynamicLibrary&& other) noexcept;
    DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;

    bool Open(const std::string& path, std::string* error);
    void Close();

    void* GetSymbol(const char* symbol, std::string* error) const;

    bool IsOpen() const;
    const std::string& Path() const;

private:
    void* m_handle;
    std::string m_path;
};

} // namespace animus::kernel::module
