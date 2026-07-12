#include "kernel/admin/TemplateResources.h"

namespace animus::kernel {

bool HasEmbeddedTemplates() {
    return false;
}

bool GetEmbeddedTemplate(std::string_view, EmbeddedTemplate*) {
    return false;
}

} // namespace animus::kernel
