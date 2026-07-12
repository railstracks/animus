#pragma once

#include <cstddef>
#include <string_view>

namespace animus::kernel {

struct EmbeddedTemplate {
    const unsigned char* data{nullptr};
    std::size_t size{0};
};

/// Returns true if templates were embedded at compile time.
bool HasEmbeddedTemplates();

/// Look up an embedded template by relative path (e.g. "gallivanting/en.md").
/// Returns true if found and fills `out`.
bool GetEmbeddedTemplate(std::string_view path, EmbeddedTemplate* out);

} // namespace animus::kernel
