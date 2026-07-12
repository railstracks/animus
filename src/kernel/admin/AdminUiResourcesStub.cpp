#include "kernel/admin/AdminUiResources.h"

namespace animus::kernel {

bool HasEmbeddedAdminUiAssets() {
    return false;
}

bool GetEmbeddedAdminUiAsset(std::string_view, EmbeddedAdminUiAsset*) {
    return false;
}

} // namespace animus::kernel
