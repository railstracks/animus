#include <iostream>
#include <string>

static void print_help(const char* argv0) {
  std::cout
    << "Animus (prototype)\n"
    << "\n"
    << "Usage:\n"
    << "  " << argv0 << " [--help] [--version]\n";
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

    std::cerr << "Unknown argument: " << arg << "\n\n";
    print_help(argv[0]);
    return 2;
  }

  std::cout << "Animus: hello. (kernel scaffold)\n";
  return 0;
}
