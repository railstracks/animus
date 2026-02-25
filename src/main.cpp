#include <atomic>
#include <iostream>
#include <string>
#include <vector>

#include "animus/Jobs.h"
#include "animus_kernel/AgentKernel.h"

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
    << "Kernel/modules (DLL/.so):\n"
    << "  --load-module <path>\n"
    << "  --load-hello-module\n"
    << "  --module-dir <dir>\n"
    << "  --allow-module <id>\n"
    << "  --allow-all-modules\n"
    << "  --deny-all-modules\n";
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

int main(int argc, char** argv) {
  bool testJobs = false;
  bool wantsKernel = false;

  animus::kernel::KernelConfig cfg;

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
      cfg.modulePaths.push_back(argv[++i]);
      wantsKernel = true;
      continue;
    }

    if (arg == "--load-hello-module") {
      const std::string base = "animus_module_example_hello";
      const std::string filename = SharedLibraryFilenameForBase(base);
      cfg.modulePaths.push_back(std::string("dist/modules/") + filename);
      wantsKernel = true;
      continue;
    }

    if (arg == "--module-dir") {
      if (i + 1 >= argc) {
        std::cerr << "--module-dir requires a directory path\n";
        return 2;
      }
      cfg.moduleDirs.push_back(argv[++i]);
      wantsKernel = true;
      continue;
    }

    if (arg == "--allow-module") {
      if (i + 1 >= argc) {
        std::cerr << "--allow-module requires a module id\n";
        return 2;
      }
      cfg.allowModuleIds.push_back(argv[++i]);
      cfg.allowAllModulesByDefault = false;
      wantsKernel = true;
      continue;
    }

    if (arg == "--allow-all-modules") {
      cfg.allowAllModulesByDefault = true;
      wantsKernel = true;
      continue;
    }

    if (arg == "--deny-all-modules") {
      cfg.allowAllModulesByDefault = false;
      wantsKernel = true;
      continue;
    }

    std::cerr << "Unknown argument: " << arg << "\n\n";
    print_help(argv[0]);
    return 2;
  }

  if (testJobs) {
    return RunJobsSmokeTest();
  }

  if (wantsKernel) {
    if (cfg.moduleDirs.empty()) {
      cfg.moduleDirs.push_back("dist/modules");
    }

    animus::kernel::AgentKernel kernel;
    std::string err;
    if (!kernel.Start(cfg, &err)) {
      std::cerr << "Kernel start failed: " << err << "\n";
      return 1;
    }

    std::cout << "Kernel started. Loaded modules from configured paths/dirs.\n";
    kernel.Stop();
    return 0;
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
