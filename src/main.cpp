#include <atomic>
#include <iostream>
#include <string>
#include <vector>

#include "animus/Jobs.h"
#include "kernel/module/ModuleLoader.h"

static void print_help(const char* argv0) {
  std::cout
    << "Animus (prototype)\n"
    << "\n"
    << "Usage:\n"
    << "  " << argv0 << " [--help] [--version]\n"
    << "\n"
    << "Job system:\n"
    << "  --test-jobs\n"
    << "\n"
    << "Modules (DLL/.so):\n"
    << "  --load-module <path>\n"
    << "  --load-hello-module   (loads dist/modules/<hello module>)\n";
}

static std::string SharedLibraryFilenameForBase(const std::string& baseName) {
#if defined(_WIN32)
  return baseName + ".dll";
#elif defined(__APPLE__)
  return std::string("lib") + baseName + ".dylib";
#else
  return std::string("lib") + baseName + ".so";
#endif
}

static void HostLog(void* /*host_ctx*/, animus_log_level_t level, animus_string_view message) {
  const char* lvl = "INFO";
  switch (level) {
    case ANIMUS_LOG_DEBUG: lvl = "DEBUG"; break;
    case ANIMUS_LOG_INFO: lvl = "INFO"; break;
    case ANIMUS_LOG_WARN: lvl = "WARN"; break;
    case ANIMUS_LOG_ERROR: lvl = "ERROR"; break;
    default: break;
  }

  std::cerr << "[module:" << lvl << "] "
            << std::string(message.data ? message.data : "", message.size)
            << "\n";
}

static int RunJobsSmokeTest() {
  animus::jobs::JobSystem jobs;
  if (!jobs.Start()) {
    std::cerr << "Failed to start job system\n";
    return 1;
  }

  std::atomic<int> counter{0};
  for (int j = 0; j < 10; ++j) {
    jobs.Enqueue([&counter] { counter.fetch_add(1); });
  }

  jobs.WaitIdle();
  jobs.Stop();

  std::cout << "Job system test complete. Counter: " << counter.load() << "\n";
  return counter.load() == 10 ? 0 : 1;
}

static int LoadModules(const std::vector<std::string>& modulePaths) {
  animus_host_vtable_t host{};
  host.struct_size = sizeof(animus_host_vtable_t);
  host.log = &HostLog;

  animus::kernel::module::ModuleLoader loader;
  std::vector<std::unique_ptr<animus::kernel::module::LoadedModule>> loaded;

  for (const auto& path : modulePaths) {
    std::string err;
    auto mod = loader.Load(path, &host, nullptr, &err);
    if (!mod) {
      std::cerr << "Failed to load module: " << path << "\n";
      std::cerr << "  error: " << err << "\n";
      return 1;
    }

    const auto* d = mod->descriptor;
    std::cout << "Loaded module id='"
              << std::string(d->id.data, d->id.size)
              << "' from " << path << "\n";

    loaded.push_back(std::move(mod));
  }

  std::cout << "Module load complete. Loaded " << loaded.size() << " module(s).\n";
  return 0;
}

int main(int argc, char** argv) {
  bool testJobs = false;
  std::vector<std::string> modulesToLoad;

  for (int i = 1; i < argc; i++) {
    const std::string arg = argv[i];

    if (arg == "--help" || arg == "-h") {
      print_help(argv[0]);
      return 0;
    }
    if (arg == "--version" || arg == "-v") {
      std::cout << "animus 0.0.0\n";
      return 0;
    }

    if (arg == "--test-jobs") {
      testJobs = true;
      continue;
    }

    if (arg == "--load-module") {
      if (i + 1 >= argc) {
        std::cerr << "--load-module requires a path\n";
        return 2;
      }
      modulesToLoad.push_back(argv[++i]);
      continue;
    }

    if (arg == "--load-hello-module") {
      const std::string base = "animus_module_example_hello";
      const std::string filename = SharedLibraryFilenameForBase(base);
      modulesToLoad.push_back(std::string("dist/modules/") + filename);
      continue;
    }

    std::cerr << "Unknown argument: " << arg << "\n\n";
    print_help(argv[0]);
    return 2;
  }

  if (testJobs) {
    return RunJobsSmokeTest();
  }

  if (!modulesToLoad.empty()) {
    return LoadModules(modulesToLoad);
  }

  std::cout << "Animus: hello. (kernel scaffold with job system + module loader)\n";

  // Demo: start job system and show worker count
  animus::jobs::JobSystem jobs;
  if (jobs.Start()) {
    std::cout << "Job system started with " << jobs.WorkerCount() << " workers.\n";
    jobs.Stop();
  }

  return 0;
}
