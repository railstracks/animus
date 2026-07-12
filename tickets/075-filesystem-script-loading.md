# Ticket 075 — Lua Script Loading: Filesystem-based with ~/.animus Root

**Priority:** P2 (operational friction — every script update requires manual API push)
**Dependencies:** None
**Created:** 2026-06-08

## Problem

Lua adapters are currently loaded from a SQLite `ScriptStore`. This means:

1. **Script updates require API calls** — pushing updated Lua code requires a REST API call to `/api/v1/lua/scripts/{id}`, not just editing a file
2. **No filesystem visibility** — scripts live in an opaque database, not on disk where they can be grepped, diffed, and version-controlled alongside C++ code
3. **Deployment friction** — every time a script changes (config key prefix rename, new actions), we have to write a push script or update via admin UI
4. **No git tracking** — the ScriptStore is a runtime database; changes aren't in version control unless manually synced

## Proposed Solution

### 1. Establish `~/.animus` as the daemon's root config directory

```
~/.animus/
├── config.json          # Main daemon config (currently embedded in admin UI)
├── agents/              # Per-agent configs
│   └── default.json     # Default agent configuration
├── scripts/             # Lua scripts (loaded from disk at startup)
│   ├── bluesky.lua
│   ├── discord.lua
│   ├── irc.lua
│   ├── slack.lua
│   ├── telegram.lua
│   ├── twitter.lua
│   ├── vk.lua
│   └── whatsapp.lua
├── channels/            # Channel configs (per-platform)
│   ├── discord-personal.json
│   ├── slack-workspace.json
│   └── whatsapp-personal.json
├── data/                # SQLite databases (consolidation, memory, etc.)
│   ├── animus.db
│   └── scripts.db       # Deprecated — migration source
└── logs/                # Daemon logs
```

### 2. Script loading: filesystem first, database as fallback

On startup:
1. Scan `~/.animus/scripts/*.lua` — load each file, register its adapter
2. If `~/.animus/scripts/` is empty, fall back to ScriptStore (backward compat)
3. Admin UI "save script" writes to both filesystem and database
4. Hot-reload: watch `~/.animus/scripts/` for changes (inotify/FSEvents), reload on modification

### 3. Platform-specific stub for Windows

```cpp
// Platform.h
#ifdef _WIN32
    #define ANIMUS_HOME() (std::filesystem::path(getenv("APPDATA")) / "animus")
    #define PATH_SEP "\\"
#else
    #define ANIMUS_HOME() (std::filesystem::path(getenv("HOME")) / ".animus")
    #define PATH_SEP "/"
#endif
```

First-run: create `~/.animus/` with default structure. On Windows: `%APPDATA%/animus/`.

### 4. Migration path

1. Add `~/.animus/scripts/` loading to `LoadStoredLuaScripts()` — check filesystem first
2. Add admin API endpoint to export ScriptStore → filesystem (`POST /api/v1/scripts/export`)
3. Add admin UI button to trigger migration
4. Keep ScriptStore as write-through cache (filesystem is source of truth)
5. Phase out ScriptStore dependency over time

### 5. Config directory alongside scripts

Channel configs, agent configs, and persistent data all move to `~/.animus/`. This gives a single root for:
- Backup/restore (tar `~/.animus/`)
- Debugging (inspect files directly)
- Version control (symlink `~/.animus/scripts/` → project `scripts/`)
- Multi-instance isolation (different `--home` flags)

## Acceptance Criteria

- [ ] `~/.animus/` created on first run with default structure
- [ ] Lua scripts loaded from `~/.animus/scripts/*.lua` at startup
- [ ] Existing ScriptStore loading works as fallback when filesystem is empty
- [ ] Admin API endpoint to export ScriptStore → filesystem
- [ ] Windows path stub (`%APPDATA%/animus/`)
- [ ] Hot-reload on file change (inotify/FSEvents)
- [ ] All 8 current adapters load correctly from filesystem
- [ ] Admin UI "save" writes to both filesystem and database

## Implementation Notes

- `LoadStoredLuaScripts()` in `AgentKernel.cpp` is the current entry point
- `ScriptStore` class handles SQLite CRUD — keep for admin UI but add filesystem reads
- Config migration from SQLite to JSON files is a separate ticket
- The `~/.animus/` convention matches standard XDG/Linux practice and is consistent with tools like `~/.config/`, `~/.ssh/`