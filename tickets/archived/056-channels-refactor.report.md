# Ticket Report 056 — Channels Refactor: Merge Interfaces + Social

**Date:** 2026-05-22
**Status:** Phase 1 Complete
**Commits:** `6a38587`, `c90b04f`, `34e9018`, `3fed80f`, `4ff31b5`, `8c636c0`, `35ce2a1`
**Duration:** May 22, 2026

## What Was Done

### 1. ChannelState & ChannelRouter (`ChannelState.h`, `ChannelManager.h`)

**`ChannelState`** — Lightweight channel descriptor:
- name, type, enabled, connected, lastEventUnixMs, config (jsoncpp Value)

**`ChannelRouter`** — Session routing registry:
- Maps `channel_name + routing_key → session_key` with agent_id and creation timestamp
- `Register()`, `Lookup()`, `PruneExpired()` with configurable TTL
- Thread-safe (mutex-guarded unordered_map)

### 2. ChannelManager (`ChannelManager.h/.cpp`, ~1400 lines)
Unified manager replacing both `InterfaceManager` (IRC) and `SocialEventPoller` (VK/Telegram/Bluesky):

**CRUD operations** backed by SQLite (`AgentConfigStore`):
- Channel config stored as `channel.{name}.type`, `channel.{name}.enabled`, `channel.{name}.config` (JSON blob)
- `ListChannels()`, `GetChannel()`, `CreateChannel()`, `UpdateChannelConfig()`, `DeleteChannel()`, `SetChannelEnabled()`

**Connector runtimes:**
- **IRC** — Wraps existing `IrcInterfaceRuntime` with `StartIrcChannel()`/`StopIrcChannel()`
- **Telegram** — Long Poll loop (`getUpdates` → process → dispatch → auto-reply)
- **VK** — Long Poll loop (`getLongPollServer` → events → dispatch → auto-reply)

**Unified dispatch:**
- `DispatchToSession(PollerState*, routingKey, message, sessionType)` — single path for all poller-based channels
- Uses `state->agent_id` (loaded from channel config) instead of hardcoded default
- Routes through `ChannelRouter` for session lookup/creation
- Builds `ReplyTarget` for the response path

**Unified reply:**
- `SendReply(ReplyTarget, text)` — dispatches to the right connector (IRC privmsg, Telegram sendMessage, VK API call)

**Legacy migration:**
- `MigrateFromLegacy()` reads `social.*` keys → creates `channel.*` entries
- Skips already-migrated channels
- Preserves all existing config fields

### 3. Admin API (`AdminServerRoutesChannels.inc`)
Full REST API replacing both `/api/v1/interfaces` and `/api/v1/agents/{id}/social`:

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/api/v1/channels` | GET | List all channels with status |
| `/api/v1/channels` | POST | Create channel |
| `/api/v1/channels/{name}` | GET | Channel detail |
| `/api/v1/channels/{name}` | PATCH | Update config (preserves secrets) |
| `/api/v1/channels/{name}` | DELETE | Delete channel |
| `/api/v1/channels/{name}/enable` | POST | Enable and start |
| `/api/v1/channels/{name}/disable` | POST | Disable and stop |

Secret masking in responses (`access_token`, `app_password`, `server_password` → `"***"`). PATCH preserves existing secrets when client sends `"***"` or empty.

### 4. Admin UI (`ChannelsView.vue`)
Single Vue view replacing both `InterfacesView.vue` and `SocialView.vue`:

**Channel table:** name | type (chip) | identity | endpoint | enabled (switch) | connected (chip) | last event | actions (edit/delete)

**Create/edit dialog** with type-specific form sections:
- **IRC:** host, port, server password, nick, username, realname, channels (multiline), agent selector, respond toggles, DM allowlist, reconnect
- **Telegram:** agent selector, bot token
- **VK:** agent selector, access token, group ID
- **Bluesky:** agent selector, handle, app password, PDS URL
- **Mastodon:** agent selector, handle, instance URL

Agent dropdown populated from `/api/v1/agents`. Name auto-generated from type (`telegram` → `telegram-2` if taken).

### 5. Agent Association for All Channel Types
Follow-up commit ensuring every channel type can bind to a specific agent:

- `PollerState` stores `agent_id` (default `"default"`)
- `StartChannel()` loads `agent_id` from channel config
- `DispatchToSession()` uses `state->agent_id` instead of hardcoded `"default"`
- All non-IRC form sections show agent dropdown
- Config blobs include `agent_id` for Telegram, VK, Bluesky, Mastodon

### 6. Legacy Cleanup (1,326 lines removed)
- Deleted `InterfacesView.vue`, `SocialView.vue`
- Deleted `AdminServerRoutesSocial.inc`
- Removed old `interfaces` i18n block (replaced by `channels` block)
- Fixed duplicate `LogsView` import in router
- Sidebar: single "Channels" entry replaces "Interfaces" + "Social"

## Architecture Decisions

### Why ChannelManager over ChannelConnector interface
The ticket spec proposed a `ChannelConnector` abstract base class. Phase 1 deliberately avoids this — the three connector types (IRC socket, Telegram long poll, VK long poll) have different enough lifecycle models that forcing them into a virtual interface would add indirection without clarity. IRC uses `IrcInterfaceRuntime` (callback-based), while VK/Telegram use thread-based pollers with backoff. The `ChannelManager` owns them as concrete types. A `ChannelConnector` interface makes sense in Phase 2 when the pattern stabilizes and plugin-loaded connectors become realistic.

### JSON config blobs vs flat keys
IRC used JSON blobs (`interfaces.json`). Social used flat key-value pairs (`social.{platform_id}.{field}`). Channels use JSON blobs stored in SQLite — one read/write per channel instead of enumerating keys. This is cleaner for complex configs like IRC (nested channels array, reconnect settings) and forward-compatible for future channel types.

### Kernel-scoped, not agent-scoped
Channels are kernel-level resources (like IRC interfaces were). Agent routing is per-channel config (`agent_id` in the config blob), not URL-scoped. This matches how IRC already worked and avoids the confusion of agent-scoped social instances.

## Build Fixes

Two GCC-specific issues resolved:

1. **jsoncpp visibility** — `ChannelManager.h` includes `json/value.h`. Downstream consumers (`AgentKernel.h`) couldn't see jsoncpp types. Fixed by making jsoncpp a `PUBLIC` dependency on `animus_kernel` target in CMakeLists.
2. **Aggregate default params** — GCC rejects default parameters on aggregate members in certain contexts. Used explicit constructors where needed.

## Files Changed

| File | Status | Purpose |
|------|--------|---------|
| `include/animus_kernel/ChannelState.h` | New | Lightweight channel state struct |
| `include/animus_kernel/ChannelManager.h` | New | ChannelRouter + ChannelManager declarations |
| `src/kernel/ChannelManager.cpp` | New | Full implementation (~1400 lines) |
| `src/kernel/admin/internal/AdminServerRoutesChannels.inc` | New | REST API routes |
| `admin-ui/src/views/ChannelsView.vue` | New | Unified admin UI |
| `src/kernel/AgentKernel.cpp` | Modified | Replaced InterfaceManager + SocialEventPoller with ChannelManager |
| `include/animus_kernel/AgentKernel.h` | Modified | ChannelManager member, removed old members |
| `src/kernel/admin/AdminServer.h` | Modified | SetChannelManager(), RegisterRoutesChannels() |
| `src/kernel/admin/AdminServerRoutes.cpp` | Modified | RegisterRoutesChannels() call |
| `src/kernel/admin/AdminServer.cpp` | Modified | Wiring in RegisterHandlersOnce() |
| `CMakeLists.txt` | Modified | Added ChannelManager.cpp, jsoncpp PUBLIC |
| `admin-ui/src/router/index.ts` | Modified | `/channels` route, fixed duplicate import |
| `admin-ui/src/components/AppSidebar.vue` | Modified | Single "Channels" sidebar entry |
| `admin-ui/src/i18n/locales/en.ts` | Modified | Full `channels.*` i18n block, removed `interfaces.*` |
| `admin-ui/src/views/InterfacesView.vue` | Deleted | Replaced by ChannelsView |
| `admin-ui/src/views/SocialView.vue` | Deleted | Replaced by ChannelsView |
| `src/kernel/admin/internal/AdminServerRoutesSocial.inc` | Deleted | Replaced by AdminServerRoutesChannels.inc |

## Acceptance Criteria

- [x] `ChannelManager` replaces both `InterfaceManager` and `SocialEventPoller`
- [x] `ChannelRouter` provides unified session routing
- [x] `/api/v1/channels` REST API (CRUD + enable/disable)
- [x] `ChannelsView.vue` merges IRC + social admin UIs
- [x] Single "Channels" sidebar entry
- [x] Agent association (`agent_id`) for all channel types
- [x] Legacy `social.*` config migration
- [x] Secret masking in API responses
- [x] Legacy files deleted (1,326 lines removed)
- [x] Clean build (C++ backend + Vue frontend + embedded SPA)
- [ ] `ChannelConnector` abstract interface — Phase 2
- [ ] Dynamic connector registration — Phase 2
- [ ] `interfaces.json` file migration (IRC-specific, for existing deployments)
- [ ] Module/connector system (shared library loading) — Phase 3

## Remaining Work (Phase 2+)

- **Connector interface** — Extract `ChannelConnector` abstract base class. Each connector type (IRC, Telegram, VK, Bluesky) becomes a self-contained class.
- **Dynamic registration** — Connectors register themselves with `ChannelManager` at startup. Plugins can add new channel types.
- **IRC file migration** — Auto-migrate `interfaces.json` → SQLite for existing deployments.
- **Module system** — Connectors loadable as shared libraries (`connector.telegram.so`, `connector.slack.so`).
