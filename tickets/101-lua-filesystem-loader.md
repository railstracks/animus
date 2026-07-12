# Ticket 101 — Lua Script Filesystem Loader

## Problem

Lua scripts are currently stored in the `lua_scripts` database table and loaded via `ScriptStore` at daemon startup. This means:
- No git version control for user-defined tools
- No external editor workflow (vim, VS Code, etc.)
- Admin UI is the only editing path — fine for small tweaks, painful for real development
- Scripts can't be deployed alongside other config files or managed by infrastructure tooling
- No diff, no history, no review process

Scripts are code. Code belongs on the filesystem.

## Goal

Read Lua scripts from the filesystem at startup instead of (or in addition to) the database. Support a conventional directory structure where users drop `.lua` files and they're automatically discovered and loaded.

## Design

### Directory Structure

```
state/lua/<agent_id>/
  weather.lua
  reminder.lua
  notes.lua
state/lua/_shared/          # loaded for all agents
  helpers.lua
```

Or alternatively, a configurable root path in the kernel config:

```
KernelConfig {
  std::string lua_script_dir = "./state/lua";  // default
}
```

### File Format

Each `.lua` file is a standalone Lua script. The filename (without extension) becomes the script name. A optional companion metadata file can override:

```lua
-- weather.lua
-- @name Weather Tool
-- @description Get current weather for a location
-- @enabled true

local tool = require("animus.tool")
tool.register("get_weather", function(args)
    -- ...
end)
```

Metadata parsed from Lua comments (`-- @key value`) — no separate JSON/YAML needed. Falls back to filename as script name, enabled by default.

### Loading Flow

1. **Daemon startup** — after DB-based scripts are loaded, scan the filesystem directory
2. **Per-agent discovery** — for each known agent, scan `state/lua/<agent_id>/` 
3. **Shared scripts** — scan `state/lua/_shared/` for all agents
4. **Load and eval** — each `.lua` file is evaluated in the agent's Lua state
5. **Errors are non-fatal** — a script that fails to load logs the error and continues; one broken script doesn't kill the others
6. **Hot reload** (future) — file watcher detects changes and reloads scripts without daemon restart

### Priority: Filesystem over Database

If a filesystem script has the same name as a DB script for the same agent:
- **Filesystem wins** — the file overrides the DB entry
- Log a warning: "Filesystem script 'X' overrides database script"
- This allows migration: users copy scripts from DB to filesystem, the override ensures consistency

### Migration Path

Users with existing DB-stored scripts need a clean migration:

1. Add admin endpoint: `POST /api/v1/lua/export-to-filesystem` — writes all DB scripts to the filesystem directory
2. On first daemon start with filesystem loading enabled, if the script directory is empty and DB scripts exist, log a notice: "X Lua scripts found in database. Run export to migrate to filesystem."
3. DB storage remains functional (not removed) — it becomes the secondary source, filesystem is primary
4. Eventually deprecate DB storage in a future major version

### Admin UI Changes

- Lua management page shows both sources:
  - **Filesystem** — list of `.lua` files with status (loaded/error/disabled)
  - **Database** — existing script list (marked as "legacy" or "migrated")
- Filesystem scripts are read-only in the admin UI (edit the file directly)
- DB scripts remain editable in the admin UI
- "Export to Filesystem" button migrates DB scripts

## Technical Implementation

### New Component: `FilesystemScriptLoader`

```cpp
class FilesystemScriptLoader {
public:
    struct DiscoveredScript {
        std::string name;
        std::string source;
        std::string description;
        std::string agent_id;   // empty = shared
        bool enabled{true};
        std::filesystem::path path;
    };

    explicit FilesystemScriptLoader(const std::string& rootDir);

    std::vector<DiscoveredScript> DiscoverAll(const std::vector<std::string>& agentIds) const;
    std::vector<DiscoveredScript> DiscoverForAgent(const std::string& agentId) const;
    std::vector<DiscoveredScript> DiscoverShared() const;

private:
    std::filesystem::path m_rootDir;
    DiscoveredScript ParseFile(const std::filesystem::path& path,
                                const std::string& agentId) const;
};
```

### Integration in AgentKernel

`LoadStoredLuaScripts()` becomes `LoadLuaScripts()`:
1. Load DB scripts (existing path, for backward compat)
2. Load filesystem scripts (new)
3. Track source per script (DB vs filesystem) for admin UI display
4. Override: if filesystem script name matches DB script, filesystem wins

### Config

```json
{
  "lua": {
    "script_dir": "./state/lua",
    "load_from_db": true,
    "filesystem_overrides_db": true
  }
}
```

## Scope

### In scope:
- `FilesystemScriptLoader` component
- Directory scanning and metadata parsing
- Integration with existing `LoadStoredLuaScripts` flow
- Override behavior (filesystem > DB)
- Export endpoint (`POST /api/v1/lua/export-to-filesystem`)
- Admin UI: dual-source display

### Deferred:
- Hot reload (file watcher) — separate ticket
- DB storage deprecation — future major version
- Per-agent script sandboxing (directory isolation beyond convention) — security hardening

## Dependencies

- Existing `ScriptStore` (DB layer) — kept for backward compat
- Existing `LuaState` evaluation — unchanged
- `std::filesystem` — already used elsewhere in the codebase

## Priority

Medium — improves developer experience significantly but doesn't block release. Can ship as a post-release enhancement.

## Open Questions

1. **Watch vs scan** — should we implement file watching (inotify/inotify) for hot reload in this ticket, or defer? Recommend defer.
2. **Per-agent isolation** — should agents be restricted to their own directory only, or can they load from `_shared/`? Recommend: `_shared/` is readable by all, agent dirs are isolated.
3. **Subdirectories** — should we recurse into subdirectories, or only scan one level? Recommend: one level only (keep it simple, flat structure).
4. **Config file per script** — instead of Lua comment metadata, use a companion `.json` file? Recommend: Lua comments for now, companion JSON if needed later.
