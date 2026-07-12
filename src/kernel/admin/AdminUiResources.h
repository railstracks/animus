#pragma once

#include <cstddef>
#include <string_view>

namespace animus::kernel {

struct EmbeddedAdminUiAsset {
    const unsigned char* data{nullptr};
    std::size_t size{0};
};

bool HasEmbeddedAdminUiAssets();
bool GetEmbeddedAdminUiAsset(std::string_view path, EmbeddedAdminUiAsset* asset);

} // namespace animus::kernel
