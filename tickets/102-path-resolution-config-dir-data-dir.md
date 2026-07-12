# Ticket 102 — Path Resolution: --config-dir and --data-dir

## Problem

All file paths are hardcoded relative to the current working directory. The daemon expects `config/db.json`, `state/memory.db`, etc. relative to wherever it's launched from. This breaks for:
- Docker containers (WORKDIR must align with paths)
- System services / systemd units (daemon runs from `/usr/bin/`)
- Installed binaries (no concept of a project root)
- Multi-instance deployments

Additionally, runtime state files (WhatsApp auth, agent config, interface config) were tracked in git — untracked in a separate commit, but the underlying pathing problem remains.

## Goal

Introduce `--config-dir` and `--data-dir` CLI flags with sensible platform-specific defaults. Cleanly separate read-only configuration from read-write runtime state.

## Design

### Two-Directory Model

```
config-dir/          ← read-only inputs (user provides these)
  db.json            ← database backend selection
  providers.json     ← LLM provider configs
  auth.json          ← OAuth tokens

data-dir/            ← read-write state (daemon manages these)
  memory.db          ← SQLite database (default backend)
  interfaces.json    ← channel/interface configs
  agent_config.json  ← per-agent configuration store
  lua/               ← Lua scripts
    _shared/         ← shared scripts
    <agent_id>/      ← per-agent scripts
  wa-auth/           ← WhatsApp auth data
  memory/            ← memory layer state
  log/               ← daemon logs
  run/               ← PID file
```

### CLI Flags

```
--config-dir <path>   Directory for configuration files
--data-dir <path>     Directory for runtime state
```

### Default Resolution

The daemon resolves default paths using this logic:

1. **Flag provided** → use it directly
2. **Legacy mode** → if `config/db.json` exists relative to CWD, use legacy CWD-relative paths (`config/` and `state/`). This preserves backward compatibility for existing deployments.
3. **Platform default** → if no flag and no legacy config found:

| Platform | config-dir | data-dir |
|----------|-----------|----------|
| Linux | `$HOME/.animus/config` | `$HOME/.animus/data` |
| macOS | `$HOME/Library/Application Support/Animus/config` | `$HOME/Library/Application Support/Animus/data` |
| Windows | `%APPDATA%\Animus\config` | `%APPDATA%\Animus\data` |

If `$HOME` (or equivalent) is unset, fall back to CWD-relative `config/` and `state/`.

### Path Resolution in KernelConfig

All path fields in `KernelConfig` change from bare relative paths to resolved absolute paths:

```cpp
struct KernelConfig {
    // Resolved at startup from config-dir / data-dir
    std::filesystem::path configDir;
    std::filesystem::path dataDir;

    // Derived paths (set during resolution, not by user)
    // config-dir/
    std::string db_config_path;        // configDir / "db.json"
    std::string providers_file_path;   // configDir / "providers.json"
    std::string auth_file_path;        // configDir / "auth.json"

    // data-dir/
    std::string sqlite_path;           // dataDir / "memory.db"
    std::string interface_file_path;   // dataDir / "interfaces.json"
    std::string agent_config_path;     // dataDir / "agent_config.json"
    std::string lua_script_dir;        // dataDir / "lua"
    std::string embedding_model_path;  // dataDir / "models" / ...
    std::string log_dir;               // dataDir / "log"
};
```

### Resolution Flow (in main.cpp)

```
1. Parse --config-dir and --data-dir from argv
2. If neither set, check for legacy mode:
   - If CWD/config/db.json exists → legacy: configDir=CWD/config, dataDir=CWD/state
3. If still unset, resolve platform defaults:
   - Linux: $HOME/.animus/config, $HOME/.animus/data
   - macOS: ~/Library/Application Support/Animus/{config,data}
   - Windows: %APPDATA%\Animus\{config,data}
4. Create directories if they don't exist (data-dir only; config-dir must exist or be created by user)
5. Resolve all derived paths
6. Print resolved paths on startup banner
```

### Startup Banner

```
=== Animus Daemon ===
Config: /home/user/.animus/config
Data:   /home/user/.animus/data
Database: SQLite (/home/user/.animus/data/memory.db)
Lua:    /home/user/.animus/data/lua
=====================
```

### Docker Impact

The Dockerfile entrypoint becomes simpler:

```bash
exec animusd --config-dir /data/config --data-dir /data/data
```

Or with the env-var entrypoint (generates config/db.json first, then launches):

```bash
exec animusd --config-dir /data/config --data-dir /data/data
```

Volume mounts become cleaner:
- `-v animus-config:/data/config` (read-only, user provides)
- `-v animus-data:/data/data` (read-write, daemon manages)

### First-Run Behavior

On first run with platform defaults:
1. Create `$HOME/.animus/config/` (empty)
2. Create `$HOME/.animus/data/` with subdirectories (`lua/_shared/`, `log/`, `run/`)
3. No `db.json` → defaults to SQLite at `data/memory.db`
4. Log shows: "First run: using SQLite with default settings. Run the setup wizard to configure providers and agents."
5. Bundle default Lua scripts: copy built-in `_shared/*.lua` to `data/lua/_shared/` on first run if directory is empty

## Technical Implementation

### Changes to main.cpp

- Parse `--config-dir` and `--data-dir` flags
- Implement `ResolveDefaultPaths()` function
- Replace hardcoded `"config/db.json"` etc. with resolved paths
- Print startup banner with resolved paths

### Changes to KernelConfig

- Add `configDir` and `dataDir` fields
- Change default values from `"config/..."` and `"state/..."` to empty strings (resolved at startup)
- Add derived path resolution method

### Changes to ProviderManager / InterfaceStorage / etc.

- All file path consumers use the resolved paths from KernelConfig
- No change to read/write logic — just different source for the path

### Migration

- Legacy detection: if `config/db.json` exists in CWD, all paths stay CWD-relative
- Log a notice in legacy mode: "Using legacy paths (CWD-relative). Consider migrating to --config-dir / --data-dir."
- No forced migration — existing deployments keep working

## Scope

### In scope:
- `--config-dir` and `--data-dir` CLI flags
- Platform default resolution
- Legacy mode detection
- Path resolution in KernelConfig
- Updated Docker entrypoint
- Startup banner
- First-run directory creation + Lua script bootstrap

### Deferred:
- Config validation / health checks
- Migration utility (legacy → platform paths)
- Environment variable alternatives (`ANIMUS_CONFIG_DIR`, `ANIMUS_DATA_DIR`)
- Multi-instance support (named instances per data-dir)

## Priority

Medium-high — needed for clean Docker deployment and system packaging. Not a hard release blocker (Docker WORKDIR hack works), but the right time to do this is before release.

## Estimated Complexity

Small-medium. Mostly path plumbing through main.cpp and KernelConfig. No architectural changes.
