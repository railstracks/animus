#include "kernel/admin/AdminUiResources.h"

namespace animus::kernel {

std::string_view GetEmbeddedAdminUiResource(std::string_view path) {
  // Stub: returns empty when embedding is disabled (ANIMUS_ADMIN_UI_EMBED=OFF)
  (void)path;
  return {};
}

std::vector<std::string> GetEmbeddedAdminUiResourcePaths() {
  // Stub: no embedded resources available
  return {};
}

bool HasEmbeddedAdminUiResources() {
  return false;
}

} // namespace animus::kernel
