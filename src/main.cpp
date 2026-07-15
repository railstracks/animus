#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <sstream>
#include <thread>
#include <vector>

#ifdef __linux__
#include <sys/stat.h>
#endif

#ifdef __linux__
#include <execinfo.h>
#include <unistd.h>
#endif

#include "animus/Jobs.h"
#include "animus_kernel/AgentKernel.h"
#include "animus_kernel/DatabaseConfig.h"
#include "animus_kernel/NodeDaemon.h"

namespace {

std::atomic<bool> g_stopRequested{false};

// Returns a human-readable timestamp for log headers.
static std::string LogTimestamp() {
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
  return buf;
}

void HandleSignal(int /*signalNumber*/) {
  g_stopRequested.store(true);
}

void HandleCrash(int sig) {
  std::cerr << "\n=== FATAL: Signal " << sig << " (";
  switch (sig) {
    case SIGSEGV: std::cerr << "SIGSEGV"; break;
    case SIGABRT: std::cerr << "SIGABRT"; break;
    case SIGFPE:  std::cerr << "SIGFPE";  break;
    default:      std::cerr << "unknown"; break;
  }
  std::cerr << ") ===" << std::endl;

#ifdef __linux__
  void* buf[32];
  int n = backtrace(buf, 32);
  std::cerr << "Backtrace:" << std::endl;
  backtrace_symbols_fd(buf, n, STDERR_FILENO);
#endif
  std::_Exit(128 + sig);
}

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
    << "  --deny-all-modules\n"
    << "\n"
    << "Admin server:\n"
    << "  --run-kernel\n"
    << "  --daemon\n"
    << "  --admin-host <host>\n"
    << "  --admin-port <port>\n"
    << "  --disable-admin\n"
    << "\n"
    << "Storage backend:\n"
    << "  --db-config <path>\n"
    << "  --storage-backend <sqlite|postgresql>\n"
    << "  --sqlite-path <path>\n"
    << "  --pg-host <host>\n"
    << "  --pg-port <port>\n"
    << "  --pg-database <name>\n"
    << "  --pg-username <user>\n"
    << "  --pg-password <pass>\n"
    << "  --pg-pool-size <n>\n"
    << "\n"
    << "Paths:\n"
    << "  --config-dir <path>              Directory for configuration files\n"
    << "  --data-dir <path>                Directory for runtime state\n"
    << "  --prompt-log-level <level>       Prompt logging: none, default, full (default: default)\n"
    << "  --log-file <path>                Redirect stderr+stdout to file (also: ANIMUS_LOG_FILE env)\n"
    << "\n"
    << "Node mode:\n"
    << "  --node                          Run as external node (no LLM/scheduler/channels)\n"
    << "  --server-url <url>              Central server WebSocket URL (e.g. ws://host:port/ws/node)\n"
    << "  --token <token>                 Auth token for server connection\n"
    << "  --node-name <name>              Name to register as\n"
    << "  --node-tools <tool1,tool2,...>  Comma-separated tool allowlist\n";
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

// ============================================================================
// Path Resolution
//
// Resolves config-dir and data-dir using this priority:
// 1. Explicit --config-dir / --data-dir flags
// 2. Legacy mode: if CWD/config/db.json exists, use CWD/config and CWD/state
// 3. Platform defaults: $HOME/.animus/{config,data} (Linux),
//    ~/Library/Application Support/Animus/{config,data} (macOS),
//    %APPDATA%/Animus/{config,data} (Windows)
// ============================================================================

static std::string GetHomeDir() {
#ifdef _WIN32
  const char* appdata = std::getenv("APPDATA");
  if (appdata) return appdata;
#else
  const char* home = std::getenv("HOME");
  if (home) return home;
#endif
  return ".";  // fallback
}

static std::pair<std::filesystem::path, std::filesystem::path>
GetPlatformDefaultPaths() {
  std::string home = GetHomeDir();
  if (home == ".") {
    // No HOME — fall back to CWD-relative
    return {"config", "state"};
  }

#ifdef _WIN32
  std::filesystem::path base = std::filesystem::path(home) / "Animus";
#elif defined(__APPLE__)
  std::filesystem::path base = std::filesystem::path(home) / "Library" / "Application Support" / "Animus";
#else
  std::filesystem::path base = std::filesystem::path(home) / ".animus";
#endif
  return {base / "config", base / "data"};
}

static void ResolvePaths(animus::kernel::KernelConfig& cfg,
                         const std::string& configDirFlag,
                         const std::string& dataDirFlag) {
  namespace fs = std::filesystem;

  bool hasConfigFlag = !configDirFlag.empty();
  bool hasDataFlag = !dataDirFlag.empty();

  // Check for legacy mode: config/db.json exists in CWD
  bool legacyMode = false;
  if (!hasConfigFlag && !hasDataFlag) {
    if (fs::exists(fs::current_path() / "config" / "db.json")) {
      legacyMode = true;
    }
  }

  if (legacyMode) {
    cfg.configDir = fs::current_path() / "config";
    cfg.dataDir = fs::current_path() / "state";
    cfg.legacyPaths = true;
  } else {
    auto [defaultConfig, defaultData] = GetPlatformDefaultPaths();
    cfg.configDir = hasConfigFlag ? fs::path(configDirFlag) : defaultConfig;
    cfg.dataDir = hasDataFlag ? fs::path(dataDirFlag) : defaultData;
    cfg.legacyPaths = false;
  }

  // Create data directory structure if it doesn't exist
  std::error_code ec;
  fs::create_directories(cfg.dataDir, ec);
  fs::create_directories(cfg.dataDir / "lua" / "_shared", ec);
  fs::create_directories(cfg.dataDir / "log", ec);
  fs::create_directories(cfg.dataDir / "run", ec);
  fs::create_directories(cfg.dataDir / "memory", ec);

  // Create config directory (but don't populate — user provides config)
  if (hasConfigFlag) {
    fs::create_directories(cfg.configDir, ec);
  }

  // Resolve all derived paths
  // config-dir paths
  cfg.providerStorage.providersFilePath = (cfg.configDir / "providers.json").string();
  cfg.providerStorage.authFilePath = (cfg.configDir / "auth.json").string();
  cfg.providerStorage.modelContextSizesPath = (cfg.configDir / "model_context_sizes.json").string();

  // data-dir paths
  if (cfg.sqlite_path.empty()) {
    cfg.sqlite_path = (cfg.dataDir / "memory.db").string();
  }
  cfg.interfaceStorage.filePath = (cfg.dataDir / "interfaces.json").string();
  cfg.agentStorage.filePath = (cfg.dataDir / "agent_config.json").string();
  cfg.memory.filePath = (cfg.dataDir / "memory" / "observations.json").string();
  cfg.constitutionStorage.filePath = (cfg.dataDir / "constitution.json").string();
  cfg.lua_script_dir = (cfg.dataDir / "lua").string();

  if (cfg.embedding_model_path.empty()) {
    cfg.embedding_model_path = (cfg.dataDir / "models" / "embeddinggemma-300m-qat-Q8_0.gguf").string();
  }
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

// Parse a single CLI argument into KernelConfig.
// Returns true if the argument was consumed (including any value argument),
// false if it was unrecognized.
static bool ParseArg(int argc, char** argv, int& i,
                     animus::kernel::KernelConfig& cfg,
                     std::string& dbConfigPath,
                     std::string& configDirFlag,
                     std::string& dataDirFlag,
                     std::string& promptLogLevelFlag,
                     std::string& logFilePath,
                     bool& wantsKernel, bool& runKernel, bool& daemonMode,
                     bool& nodeMode, std::string& nodeServerUrl,
                     std::string& nodeToken, std::string& nodeName,
                     std::string& nodeTools) {
  const std::string arg = argv[i];

  if (arg == "--help" || arg == "-h") {
    print_help(argv[0]);
    std::exit(0);
  }
  if (arg == "--version" || arg == "-v") {
    std::cout << "animus 0.0.0\n";
    std::exit(0);
  }

  if (arg == "--test-jobs") {
    // Handled in main before kernel path
    return true;
  }

  if (arg == "--load-module") {
    if (i + 1 >= argc) {
      std::cerr << "--load-module requires a path\n";
      std::exit(2);
    }
    cfg.modulePaths.push_back(argv[++i]);
    wantsKernel = true;
    return true;
  }

  if (arg == "--load-hello-module") {
    const std::string base = "animus_module_example_hello";
    const std::string filename = SharedLibraryFilenameForBase(base);
    cfg.modulePaths.push_back(std::string("dist/modules/") + filename);
    wantsKernel = true;
    return true;
  }

  if (arg == "--module-dir") {
    if (i + 1 >= argc) {
      std::cerr << "--module-dir requires a directory path\n";
      std::exit(2);
    }
    cfg.moduleDirs.push_back(argv[++i]);
    wantsKernel = true;
    return true;
  }

  if (arg == "--allow-module") {
    if (i + 1 >= argc) {
      std::cerr << "--allow-module requires a module id\n";
      std::exit(2);
    }
    cfg.allowModuleIds.push_back(argv[++i]);
    cfg.allowAllModulesByDefault = false;
    wantsKernel = true;
    return true;
  }

  if (arg == "--allow-all-modules") {
    cfg.allowAllModulesByDefault = true;
    wantsKernel = true;
    return true;
  }

  if (arg == "--deny-all-modules") {
    cfg.allowAllModulesByDefault = false;
    wantsKernel = true;
    return true;
  }

  if (arg == "--run-kernel") {
    runKernel = true;
    wantsKernel = true;
    return true;
  }

  if (arg == "--daemon") {
    daemonMode = true;
    wantsKernel = true;
    return true;
  }

  if (arg == "--admin-host") {
    if (i + 1 >= argc) {
      std::cerr << "--admin-host requires a host value\n";
      std::exit(2);
    }
    cfg.admin.host = argv[++i];
    wantsKernel = true;
    return true;
  }

  if (arg == "--admin-port") {
    if (i + 1 >= argc) {
      std::cerr << "--admin-port requires a port value\n";
      std::exit(2);
    }
    const std::string value = argv[++i];
    std::size_t parsed = 0;
    unsigned long port = 0;
    try {
      port = std::stoul(value, &parsed);
    } catch (...) {
      std::cerr << "--admin-port must be a number between 1 and 65535\n";
      std::exit(2);
    }
    if (parsed != value.size() || port == 0UL
        || port > std::numeric_limits<std::uint16_t>::max()) {
      std::cerr << "--admin-port must be a number between 1 and 65535\n";
      std::exit(2);
    }
    cfg.admin.port = static_cast<std::uint16_t>(port);
    wantsKernel = true;
    return true;
  }

  if (arg == "--disable-admin") {
    cfg.admin.enabled = false;
    wantsKernel = true;
    return true;
  }

  // Storage backend configuration
  if (arg == "--storage-backend") {
    if (i + 1 >= argc) {
      std::cerr << "--storage-backend requires a value (sqlite or postgresql)\n";
      std::exit(2);
    }
    cfg.storage_backend = argv[++i];
    wantsKernel = true;
    return true;
  }
  if (arg == "--sqlite-path") {
    if (i + 1 >= argc) {
      std::cerr << "--sqlite-path requires a file path\n";
      std::exit(2);
    }
    cfg.sqlite_path = argv[++i];
    wantsKernel = true;
    return true;
  }
  if (arg == "--pg-host") {
    if (i + 1 >= argc) { std::cerr << "--pg-host requires a value\n"; std::exit(2); }
    cfg.pg_host = argv[++i];
    wantsKernel = true;
    return true;
  }
  if (arg == "--pg-port") {
    if (i + 1 >= argc) { std::cerr << "--pg-port requires a number\n"; std::exit(2); }
    try { cfg.pg_port = std::stoi(argv[++i]); } catch (...) { std::cerr << "--pg-port must be a number\n"; std::exit(2); }
    wantsKernel = true;
    return true;
  }
  if (arg == "--pg-database") {
    if (i + 1 >= argc) { std::cerr << "--pg-database requires a value\n"; std::exit(2); }
    cfg.pg_database = argv[++i];
    wantsKernel = true;
    return true;
  }
  if (arg == "--pg-username") {
    if (i + 1 >= argc) { std::cerr << "--pg-username requires a value\n"; std::exit(2); }
    cfg.pg_username = argv[++i];
    wantsKernel = true;
    return true;
  }
  if (arg == "--pg-password") {
    if (i + 1 >= argc) { std::cerr << "--pg-password requires a value\n"; std::exit(2); }
    cfg.pg_password = argv[++i];
    wantsKernel = true;
    return true;
  }
  if (arg == "--pg-pool-size") {
    if (i + 1 >= argc) { std::cerr << "--pg-pool-size requires a number\n"; std::exit(2); }
    try { cfg.pg_pool_size = std::stoi(argv[++i]); } catch (...) { std::cerr << "--pg-pool-size must be a number\n"; std::exit(2); }
    wantsKernel = true;
    return true;
  }

  // Path configuration
  if (arg == "--config-dir") {
    if (i + 1 >= argc) { std::cerr << "--config-dir requires a directory path\n"; std::exit(2); }
    configDirFlag = argv[++i];
    return true;
  }
  if (arg == "--data-dir") {
    if (i + 1 >= argc) { std::cerr << "--data-dir requires a directory path\n"; std::exit(2); }
    dataDirFlag = argv[++i];
    return true;
  }
  if (arg == "--prompt-log-level") {
    if (i + 1 >= argc) { std::cerr << "--prompt-log-level requires a value (none, default, full)\n"; std::exit(2); }
    promptLogLevelFlag = argv[++i];
    return true;
  }

  if (arg == "--log-file") {
    if (i + 1 >= argc) { std::cerr << "--log-file requires a path\n"; std::exit(2); }
    logFilePath = argv[++i];
    return true;
  }

  // Database config file
  if (arg == "--db-config") {
    if (i + 1 >= argc) { std::cerr << "--db-config requires a file path\n"; std::exit(2); }
    dbConfigPath = argv[++i];
    wantsKernel = true;
    return true;
  }

  // Node mode arguments
  if (arg == "--node") {
    nodeMode = true;
    return true;
  }
  if (arg == "--server-url") {
    if (i + 1 >= argc) { std::cerr << "--server-url requires a value\n"; std::exit(2); }
    nodeServerUrl = argv[++i];
    return true;
  }
  if (arg == "--token") {
    if (i + 1 >= argc) { std::cerr << "--token requires a value\n"; std::exit(2); }
    nodeToken = argv[++i];
    return true;
  }
  if (arg == "--node-name") {
    if (i + 1 >= argc) { std::cerr << "--node-name requires a value\n"; std::exit(2); }
    nodeName = argv[++i];
    return true;
  }
  if (arg == "--node-tools") {
    if (i + 1 >= argc) { std::cerr << "--node-tools requires a value\n"; std::exit(2); }
    nodeTools = argv[++i];
    return true;
  }
  return false;  // unrecognized
}

// ============================================================================
// Embedding model auto-download
// ============================================================================
//
// If the embedding model file doesn't exist at startup, and ANIMUS_SKIP_MODEL_DOWNLOAD
// is not set, download it from HuggingFace. This makes Docker zero-config: just
// run the container and the model is fetched on first boot.
//
// Model: embeddinggemma-300m-qat-Q8_0.gguf (~314 MB)
// Source: https://huggingface.co/ggml-org/embeddinggemma-300m-qat-q8_0-GGUF

static const char* kEmbeddingModelUrl =
    "https://huggingface.co/ggml-org/embeddinggemma-300m-qat-q8_0-GGUF/resolve/main/embeddinggemma-300m-qat-Q8_0.gguf";

static bool DownloadFile(const std::string& url, const std::filesystem::path& dest) {
    namespace fs = std::filesystem;

    // Create parent directory
    fs::path parent = dest.parent_path();
    if (!fs::exists(parent)) {
        fs::create_directories(parent);
    }

    // Use curl for download (available on all platforms we support)
    std::string tmpPath = dest.string() + ".tmp";

    std::ostringstream cmd;
    cmd << "curl -L -s -S --fail -o \"" << tmpPath << "\" \"" << url << "\"";

    std::cout << "[embedding] Downloading model from HuggingFace...\n";
    std::cout << "[embedding] URL: " << url << "\n";
    std::cout << "[embedding] Dest: " << dest.string() << "\n";
    std::cout.flush();

    int rc = std::system(cmd.str().c_str());
    if (rc != 0) {
        std::cerr << "[embedding] Download failed (exit code " << rc << ")\n";
        fs::remove(tmpPath);
        return false;
    }

    // Verify the file is non-empty
    if (!fs::exists(tmpPath) || fs::file_size(tmpPath) < 1024) {
        std::cerr << "[embedding] Downloaded file is too small or missing\n";
        fs::remove(tmpPath);
        return false;
    }

    // Rename temp to final
    fs::rename(tmpPath, dest);
    std::cout << "[embedding] Model downloaded (" << (fs::file_size(dest) / (1024*1024)) << " MB)\n";
    return true;
}

static void EnsureEmbeddingModel(const animus::kernel::KernelConfig& cfg) {
    namespace fs = std::filesystem;

    // If embeddings are disabled, skip
    if (!cfg.embedding_enabled) return;

    // If model exists, nothing to do
    if (fs::exists(cfg.embedding_model_path) && fs::file_size(cfg.embedding_model_path) > 1024) {
        return;
    }

    // Check env var opt-out
    const char* skip = std::getenv("ANIMUS_SKIP_MODEL_DOWNLOAD");
    if (skip && (std::string(skip) == "1" || std::string(skip) == "true")) {
        std::cerr << "[embedding] Model not found, but ANIMUS_SKIP_MODEL_DOWNLOAD is set — skipping download\n";
        return;
    }

    // Attempt download
    if (!DownloadFile(kEmbeddingModelUrl, cfg.embedding_model_path)) {
        std::cerr << "[embedding] Auto-download failed. Embedding service will use keyword fallback.\n";
        std::cerr << "[embedding] To manually install, download from:\n";
        std::cerr << "[embedding]   " << kEmbeddingModelUrl << "\n";
        std::cerr << "[embedding]   and place at: " << cfg.embedding_model_path << "\n";
    }
}

// Run the kernel: start, print status, wait for stop signal.
static int RunKernel(animus::kernel::KernelConfig& cfg,
                     bool daemonMode, bool runKernel) {
  if (cfg.moduleDirs.empty()) {
    cfg.moduleDirs.push_back("dist/modules");
  }

  std::cout << "=== Animus Daemon ===\n";
  std::cout << "Config: " << cfg.configDir.string() << "\n";
  std::cout << "Data:   " << cfg.dataDir.string() << "\n";
  if (cfg.legacyPaths) {
    std::cout << "Mode:   legacy (CWD-relative)\n";
  }
  std::cout << "DB:     " << cfg.storage_backend;
  if (cfg.storage_backend == "sqlite") {
    std::cout << " (" << cfg.sqlite_path << ")";
  } else {
    std::cout << " (" << cfg.pg_host << ":" << cfg.pg_port << "/" << cfg.pg_database << ")";
  }
  std::cout << "\n";
  std::cout << "Lua:    " << cfg.lua_script_dir << "\n";
  std::cout << "Admin:  " << (cfg.admin.enabled ? "enabled" : "disabled")
            << " at " << cfg.admin.host << ":" << cfg.admin.port << "\n";
  std::cout << "=====================\n";

  if (cfg.legacyPaths) {
    std::cout << "Note: Using legacy paths. Consider --config-dir / --data-dir for explicit control.\n";
  }

  // Ensure embedding model is available (downloads from HuggingFace if missing)
  EnsureEmbeddingModel(cfg);

  animus::kernel::AgentKernel kernel;
  std::string err;
  if (!kernel.Start(cfg, &err)) {
    std::cerr << "Kernel start failed: " << err << "\n";
    return 1;
  }

  std::cout << "Kernel started. Loaded modules from configured paths/dirs.\n";

  if (daemonMode) {
    std::cout
      << "Daemon mode active. Admin server: "
      << (cfg.admin.enabled ? "enabled" : "disabled")
      << " at " << cfg.admin.host << ":" << cfg.admin.port << "\n";
    std::cout << "Send SIGTERM or SIGINT to stop.\n";

    g_stopRequested.store(false);
    std::signal(SIGINT, &HandleSignal);
    std::signal(SIGTERM, &HandleSignal);
    std::signal(SIGSEGV, &HandleCrash);
    std::signal(SIGABRT, &HandleCrash);
    while (!g_stopRequested.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  } else if (runKernel) {
    std::cout
      << "Admin server: "
      << (cfg.admin.enabled ? "enabled" : "disabled")
      << " at " << cfg.admin.host << ":" << cfg.admin.port << "\n";
    std::cout << "Press ENTER to stop kernel.\n";
    std::string ignored;
    std::getline(std::cin, ignored);
  }

  kernel.Stop();
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  bool testJobs = false;
  bool wantsKernel = false;
  bool runKernel = false;
  bool daemonMode = false;
  bool nodeMode = false;
  std::string dbConfigPath;
  std::string configDirFlag;
  std::string dataDirFlag;
  std::string promptLogLevelFlag;
  std::string logFilePath;
  std::string nodeServerUrl;
  std::string nodeToken;
  std::string nodeName;
  std::string nodeTools;

  animus::kernel::KernelConfig cfg;

  for (int i = 1; i < argc; i++) {
    const std::string arg = argv[i];

    // --test-jobs is handled separately (doesn't start kernel)
    if (arg == "--test-jobs") {
      testJobs = true;
      continue;
    }

    if (!ParseArg(argc, argv, i, cfg, dbConfigPath,
                   configDirFlag, dataDirFlag, promptLogLevelFlag,
                   logFilePath,
                   wantsKernel, runKernel, daemonMode,
                   nodeMode, nodeServerUrl, nodeToken, nodeName, nodeTools)) {
      std::cerr << "Unknown argument: " << arg << "\n\n";
      print_help(argv[0]);
      return 2;
    }
  }

  // Resolve config-dir and data-dir paths
  ResolvePaths(cfg, configDirFlag, dataDirFlag);

  // Redirect stderr+stdout to log file if requested.
  // Priority: --log-file flag > ANIMUS_LOG_FILE env > none.
  // If the path is relative, it is resolved against the data dir.
  if (logFilePath.empty()) {
    const char* envLog = std::getenv("ANIMUS_LOG_FILE");
    if (envLog && *envLog) {
      logFilePath = envLog;
    }
  }
  if (!logFilePath.empty()) {
    fs::path logPath(logFilePath);
    if (logPath.is_relative()) {
      logPath = cfg.dataDir / "log" / logPath;
    }
    fs::create_directories(logPath.parent_path(), ec);
    FILE* f = std::freopen(logPath.string().c_str(), "a", stderr);
    if (f) {
      std::cerr << "--- Log started at " << LogTimestamp() << " ---" << std::endl;
      // Also duplicate stdout to the same file
      std::freopen(logPath.string().c_str(), "a", stdout);
      std::cout << "--- stdout redirected to log ---" << std::endl;
    }
  }

  // Apply prompt log level override
  if (!promptLogLevelFlag.empty()) {
    cfg.promptLogLevel = promptLogLevelFlag;
  }

  // Load database config from file if specified (or try config-dir default)
  animus::kernel::DatabaseConfig dbConfig;
  std::string configPath = dbConfigPath.empty()
      ? (cfg.configDir / "db.json").string()
      : dbConfigPath;
  if (dbConfig.LoadFromFile(configPath)) {
    dbConfig.ApplyTo(cfg);
  }
  // If no db.json exists, SQLite with data-dir/memory.db is used (KernelConfig defaults).

  if (testJobs) {
    return RunJobsSmokeTest();
  }

  if (nodeMode) {
    // Run as external node daemon
    if (nodeServerUrl.empty() || nodeToken.empty() || nodeName.empty()) {
      std::cerr << "--node requires --server-url, --token, and --node-name\n";
      return 2;
    }
    std::vector<std::string> toolsList;
    if (!nodeTools.empty()) {
      std::stringstream ss(nodeTools);
      std::string tool;
      while (std::getline(ss, tool, ',')) toolsList.push_back(tool);
    }
    animus::kernel::NodeDaemonConfig nodeCfg;
    nodeCfg.serverUrl = nodeServerUrl;
    nodeCfg.token = nodeToken;
    nodeCfg.name = nodeName;
    nodeCfg.allowedTools = toolsList;

    return animus::kernel::RunNodeDaemon(nodeCfg);
  }
  if (wantsKernel) {
    return RunKernel(cfg, daemonMode, runKernel);
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