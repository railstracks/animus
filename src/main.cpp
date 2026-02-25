#include <iostream>
#include <string>
#include "animus/Jobs.h"

static void print_help(const char* argv0) {
  std::cout
    << "Animus (prototype)\n"
    << "\n"
    << "Usage:\n"
    << "  " << argv0 << " [--help] [--version] [--test-jobs]\n";
}

int main(int argc, char** argv) {
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
      // Quick job system smoke test
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

    std::cerr << "Unknown argument: " << arg << "\n\n";
    print_help(argv[0]);
    return 2;
  }

  std::cout << "Animus: hello. (kernel scaffold with job system)\n";

  // Demo: start job system and show worker count
  animus::jobs::JobSystem jobs;
  if (jobs.Start()) {
    std::cout << "Job system started with " << jobs.WorkerCount() << " workers.\n";
    jobs.Stop();
  }

  return 0;
}
