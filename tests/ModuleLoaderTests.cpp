#include <iostream>
#include <string>

#include "animus_sdk/module_api.h"
#include "kernel/module/ModuleLoader.h"

static std::string SharedLibraryFilenameForBase(const std::string& baseName) {
#if defined(_WIN32)
  return baseName + ".dll";
#elif defined(__APPLE__)
  return std::string("lib") + baseName + ".dylib";
#else
  return std::string("lib") + baseName + ".so";
#endif
}

static void HostLog(void* /*ctx*/, animus_log_level_t level, animus_string_view msg) {
  (void)level;
  std::cerr << "[module] " << std::string(msg.data ? msg.data : "", msg.size) << "\n";
}

int main() {
  const std::string base = "animus_module_example_hello";
  const std::string filename = SharedLibraryFilenameForBase(base);
  const std::string path = std::string("dist/modules/") + filename;

  animus_host_vtable_t host{};
  host.struct_size = sizeof(animus_host_vtable_t);
  host.log = &HostLog;

  animus::kernel::module::ModuleLoader loader;
  std::string err;
  auto mod = loader.Load(path, &host, nullptr, &err);
  if (!mod) {
    std::cerr << "failed to load module: " << path << "\n";
    std::cerr << "error: " << err << "\n";
    return 1;
  }

  const std::string id(mod->descriptor->id.data, mod->descriptor->id.size);
  if (id != "example.hello") {
    std::cerr << "unexpected module id: " << id << "\n";
    return 2;
  }

  return 0;
}
