# Ticket 098 Report — External Node System

## Status: Phase 1–2 complete. Auto-reconnect implemented. Remaining Phase 3 items deferred to separate tickets.

## Completed

### Server-side foundation

- **NodeManager** (`include/animus_kernel/NodeManager.h`, `src/kernel/NodeManager.cpp`)
  - Token management: `GenerateToken` (random 51-char `an_` prefixed), `ValidateToken` (FNV-1a hash), `ListTokens`, `RevokeToken`
  - `node_tokens` DB table with hash-based storage, created via `schema::CreateTable`
  - Token cache loaded at startup from DB
  - Node connection tracking: `RegisterNode`, `RemoveNode`, `ListNodes`, `GetNode`
  - Tool call forwarding: `ExecuteOnNode(nodeName, call)` — looks up connected node, calls its execute function
  - Per-agent access control: `IsNodeAllowedForAgent`, `ListNodesForAgent` (currently all-to-all, per-agent config pending)

- **ChainRunner `__node` interception** (`src/kernel/chain/ChainRunner.cpp`)
  - Parses `__node` from tool call arguments before local tool lookup
  - If present and NodeManager available: forwards full `ToolCall` to `NodeManager::ExecuteOnNode`
  - Falls through to local execution if no `__node` or no NodeManager
  - No tool schema changes — `__node` works like `__agent_id` and `__session_key`
  - `GetResultMode` re-looks up handler for local calls (handler scope fix)

- **NodeTool** (`include/animus_kernel/tools/NodeTool.h`, `src/kernel/tools/NodeTool.cpp`)
  - Agent-facing discovery tool
  - `list` action: shows connected nodes with name, status, hostname, OS, uptime, tools
  - `info` action: detailed view of a specific node
  - Tool description explains `__node` parameter for remote execution

- **WebSocket endpoint** (`src/kernel/admin/internal/AdminServerNodeWebSocket.inc`)
  - `NodeWebSocketController` at `/ws/node`
  - Auth via `Authorization: Bearer ***` header during WebSocket upgrade
  - Handles `register` message (node announces name, tools, hostname, OS)
  - Handles `tool_result` message (node returns tool execution results)
  - Handles `heartbeat` (keepalive)
  - Execute function uses promise/future pairs for async tool calls with 30-second timeout
  - Auto-removes node and fails pending calls on disconnect

- **Admin API** (`src/kernel/admin/internal/AdminServerRoutesNodes.inc`)
  - `POST /api/v1/nodes/tokens` — generate auth token (returns plaintext once)
  - `GET /api/v1/nodes/tokens` — list tokens (masked hash)
  - `DELETE /api/v1/nodes/tokens/{id}` — revoke token
  - `GET /api/v1/nodes` — list connected nodes
  - `GET /api/v1/nodes/{name}` — node details

- **AgentKernel wiring**
  - Creates `NodeManager` at startup with `IDataStore`
  - Wires to `ChainRunner::SetNodeManager`
  - Wires to `AdminServer::SetNodeManager`
  - Registers `NodeTool` in tool registry
  - Cleanup in destructor

### Node-side foundation

- **NodeDaemon** (`src/kernel/NodeDaemon.cpp`, `include/animus_kernel/NodeDaemon.h`)
  - `NodeDaemon` class with `NodeDaemonConfig` (serverUrl, token, name, allowedTools)
  - EventLoop runs in dedicated `std::thread` (same pattern as DiscordGatewayLoop/AgentMail WS)
  - Connects to server via `drogon::WebSocketClient::newWebSocketClient(url, &loop)`
  - Auth header: `Authorization: Bearer ***` on `connectToServer` request
  - Sends registration with name, hostname (via `uname()`), OS, tool list
  - Receives `tool_call` messages, executes locally via `ToolRegistry::Find`, sends `tool_result`
  - `setConnectionClosedHandler` for disconnect handling
  - Signal handling via `runEvery` polling (SIGINT/SIGTERM)
  - Heartbeat every 30 seconds
  - `RegisterLocalTools()` wires actual `ShellExecTool` and `FileTool` instances
  - URL path extraction for HTTP upgrade request
  - Public `RunNodeDaemon()` entry point called from main.cpp

- **CLI arguments** (`src/main.cpp`)
  - `--node` — enable node mode
  - `--server-url` — WebSocket URL for central server
  - `--token` — auth token
  - `--node-name` — name to register as
  - `--node-tools` — comma-separated tool allowlist
  - Node mode branch calls `RunNodeDaemon()`

### Testing verified

- **Build**: Clean compile on workstation (Ubuntu 25.04, GCC, cmake Debug)
- **Node launch**: `animusd --node --server-url ws://localhost:8080/ws/node --token *** --node-name test-node-01 --node-tools exec,file` — connects and registers
- **Cross-machine**: Node daemon runs from Docker container (`ubuntu-tester`) connecting to host server via `ws://172.20.0.1:8080/ws/node`. Shared libraries (libdrogon, libtrantor, libjsoncpp, libllama, libggml) copied to container with `LD_LIBRARY_PATH`.
- **Server discovery**: `GET /api/v1/nodes` returns connected node with hostname, OS, tools, uptime
- **Agent `node:list`**: Agent discovers connected nodes via the node tool — returns name, hostname, OS, status, tools, uptime
- **Agent `node:info`**: Agent retrieves detailed info for a specific node
- **Agent `__node` exec (whoami)**: Agent executes `whoami` on `test-node-01` via `__node` parameter — returns `root` (exit_code=0)
- **Agent `__node` exec (touch + verify)**: Agent creates `/opt/test-file` on `test-node-01` and verifies with `ls -la` — file confirmed created, correct ownership and timestamp
- **Hostname detection**: `uname()` produces `melvin-E33011` / `Linux 6.17.0-35-generic` on host, `9b7e694f5972` (container ID) in Docker
- **Memory files in active memory**: Agent sees MEMORY FILES section with relevant excerpts from `self-knowledge.md` and other files, properly budgeted (June 25)

### Bug fixes during testing

- **EventLoop crash**: `trantor::EventLoop` in main thread FATAL-crashed because Drogon creates one during static init. Fixed by running EventLoop in `std::thread` (commit `cfd00bf`)
- **PgDataStore PGRES_COMMAND_OK**: `Execute()` returned `false` for successful INSERT/UPDATE/DELETE without result rows. Root cause of `PersistSession: Step() failed` errors. Fixed to return `true` (commit `879e45a`)
- **PgDataStore lastval()**: `SELECT lastval()` after `ON CONFLICT DO UPDATE` throws "lastval is not yet defined" because no sequence consumed. Fixed with error cleanup (commit `879e45a`)
- **PgDataStore error propagation**: `PgStatement::Execute()` stored errors in local `m_lastError` but `ErrMsg()` read from `PgDataStore::m_lastError`. Callers always got "database error". Added `SetLastError()` propagation (commit `766aa74`)

### Performance blockers — resolved (June 23–24)

All five performance blockers from the original report have been fixed:

1. **Scheduler busy-loop** (commit `54444c6`) — Racy `Changes()` check caused the scheduler to re-fire schedules in a tight loop. Fixed with proper change detection.
2. **On-the-fly embedding recomputation** (commit `2b1ae9d`) — 22 chunks with legitimate cosine similarity of 0.0 were being recomputed alongside 3 that actually lacked embeddings. Root cause: code checked `similarity == 0.0f` instead of tracking which chunks actually lacked embeddings. Added `needsEmbedding` flag to `Chunk` struct.
3. **Context assembly caching for admin preview endpoints** (commit `bb1d18c`) — Token-estimate endpoint polled every 5s, each triggering full context assembly including embedding computation. Added `AssembleCached()` method to `ContextProviderRegistry` with cache keyed on `(agent_id, session_id, turn_count)`. Admin preview endpoints use `AssembleCached`; chat handler uses `Assemble` (populates cache as side effect).
4. **LLM request body log truncation** (commit June 23) — Full 95KB request bodies no longer logged. Truncated to model name + message count + size preview.
5. **Embedding n_batch crash** (fixed June 23) — `n_batch=64` caused `GGML_ASSERT(n_ubatch >= n_tokens)` SIGABRT because `llama_encode()` requires all tokens in one micro-batch. Set `n_batch=512` with matching tokenization cap.

### Memory file budget — resolved (June 25)

1. **Per-file cap** (commit `d55968b`) — A single large file (`self-knowledge.md` at 26KB/32 chunks) could monopolize the entire memory file budget. Added per-file cap tracking (`spentPerFile`) in `tryAddChunk`. Tier 1 per-file cap = `tier1Budget`; Tier 2 per-file cap = `tier1Budget + tier2Budget`. Ensures budget diversity across files.

2. **Session text truncation** — Session text (recent turns + keywords) could exceed the embedding model's token cap when assistant responses were long, causing `llama_tokenize` to return -1 and `Embed()` to return `nullopt`. Memory files were silently skipped. Fixed by truncating session text to last 700 chars before tokenizing.

3. **Embedding API — zero vectors** (commits `f7e8a3a`–`a083c6d`) — The deepest bug. Cached embeddings in the DB were all-zero (3072 bytes of 0x00), causing cosine similarity=0 for all chunks and MEMORY FILES section to disappear entirely. Root cause was threefold:
   - `llama_batch_get_one` doesn't set `n_seq_id` or `logits` — required by the pooling layer
   - `llama_get_embeddings_ith` returns per-token embeddings, but MEAN pooling stores per-sequence — need `llama_get_embeddings_seq(ctx, 0)`
   - `llama_encode` doesn't work for embedding-only models — `llama_decode` is correct
   
   Fix: rewrote `EmbeddingService::embed()` to match the official `examples/embedding/embedding.cpp` from llama.cpp: use `llama_batch_init` + per-token `seq_id`/`logits`/`n_seq_id`, `llama_memory_clear` before each decode, `llama_decode` (not `llama_encode`), `llama_get_embeddings_seq` for MEAN pooling. Also added zero-vector guard in `Embed()` to reject garbage output.

### Phase 2: Per-agent access control + admin UI

- **`allowed_nodes` on Agent struct** — `std::vector<string>` field, stored as TEXT JSON array in DB (default `'[]'`). Migration via `ALTER TABLE`.
- **AgentStore plumbing** — all 8 touch points updated: SELECT column list, RowToAgent parameter, INSERT column + placeholder + bind, UPDATE SET + bind, form reset, form populate, save payload.
- **NodeManager enforcement** — `IsNodeAllowedForAgent` and `ListNodesForAgent` now check `allowed_nodes`: empty = all nodes (backward compatible), non-empty = only listed nodes. `AgentStore` wired via `SetAgentStore`.
- **Admin API** — `allowed_nodes` in JSON serialization (`AdminServer.cpp`) and patch parsing (`AgentManager.cpp`).
- **NodesView** (`admin-ui/src/views/NodesView.vue`) — full admin page:
  - Connected nodes list with status, hostname, OS, tools, uptime (polls every 10s)
  - Token management: generate (with label), list, revoke
  - Copy-once token dialog with clipboard copy + confirmation guard
  - Quick-start guide card with `animusd --node` command template
- **AgentsView** — `allowed_nodes` combobox in tools tab:
  - `availableNodes` ref populated from `/api/v1/nodes` on mount
  - Multi-select with chips, closable-chips, clearable
  - Hint: "Leave empty to allow all nodes"
- **Sidebar + router + i18n** — `/nodes` route registered, `mdi-lan` icon in sidebar, `sidebar.links.nodes` key added to all 23 locale files.

### Phase 2 verification

- API: `POST /api/v1/nodes/tokens` creates token (returns plaintext once)
- API: `GET /api/v1/nodes/tokens` lists tokens with masked hashes
- API: `GET /api/v1/nodes` returns connected nodes (0 when no daemon running)
- API: `GET /api/v1/agents` includes `allowed_nodes: []` in response
- Admin UI: `npm run build` succeeds (8.54s, no errors)
- Daemon: clean restart, embedding service loads, no errors

### Auto-reconnect (implemented June 25)

- **Exponential backoff** — 1s → 2s → 4s → ... → 60s cap. Doubles each failure, resets on successful connection.
- **Fresh WebSocketClient per attempt** — Drogon asserts `!tcpClientPtr_` if `connectToServer` is called again on the same instance. Restructured `InitiateConnection` to create a new client + reattach handlers each time.
- **Disconnect detection** — `setConnectionClosedHandler` triggers reconnect flow (unless shutdown requested).
- **Connection failure → reconnect** — Initial connection failure also schedules reconnect (was: immediate exit).
- **Heartbeat suppression** — Only sends heartbeats when `m_registered` is true.
- **Verified end-to-end**: killed server, watched backoff escalate through 2s → 4s → 8s → 16s → 32s → 60s cap over ~2 min, restarted server, node reconnected and registered cleanly.

## Deferred to separate tickets

- File transfer between server and nodes
- Streaming tool output (for long-running commands)
- Node groups (execute on multiple nodes)
- Per-node tool configuration in admin UI
- Ollama embedding API support (interchangeable with in-process llama.cpp)

## Commits

### Node system (Phase 1)
- `a1a9121` feat: NodeManager + ChainRunner __node interception
- `d26d2b0` fix: NodeManager forward declaration in AgentKernel.h
- `fbb67d0` fix: move SetNodeManager to public section of ChainRunner
- `3b7f9a1` fix: handler scope — re-lookup for GetResultMode after node routing
- `3668051` feat: node discovery tool + admin API for token/node management
- `452a8ec` fix: remove allowedActions (not in ToolDefinition)
- `3c42671` fix: include ToolRegistry.h for IToolHandler base class
- `202dbbb` feat: node WebSocket endpoint + --node CLI args + NodeDaemon
- `acdfd57` fix: add WS_PATH_LIST for /ws/node route registration
- `0654224` fix: SystemTool.h → ShellExecTool.h in NodeDaemon
- `049972f` fix: use Drogon's connectToServer API with HttpRequest for auth header
- `af385ae` fix: newWebSocketClient API + signal handler scope
- `69c5920` fix: use connectToServer callback + setConnectionClosedHandler
- `25ea778` fix: setConnectionClosedHandler takes WebSocketClientPtr not ConnectionPtr
- `5ecfcc9` fix: pass node params to ParseArg
- `5a715df` feat(098): wire NodeDaemon — actual tool registration, event loop, hostname detection
- `cfd00bf` fix(098): run NodeDaemon EventLoop in dedicated thread
- `879e45a` fix: PgDataStore::Execute returns true for PGRES_COMMAND_OK
- `766aa74` fix: propagate PgStatement errors to PgDataStore::ErrMsg()

### Performance fixes (June 23–24)
- `54444c6` fix: scheduler busy-loop caused by racy Changes() check
- `2b1ae9d` fix: on-the-fly embedding recomputes 25 chunks unnecessarily
- `bb1d18c` feat: cache assembled context for admin preview endpoints

### Memory file budget + embedding fixes (June 25)
- `d55968b` fix: per-file cap in memory file budget enforcement
- `f7e8a3a` fix: reject zero vectors from Embed() instead of storing them
- `4c9ab54` fix: align EmbeddingService.cpp with header declarations
- `a083c6d` fix: match official llama.cpp embedding API usage (root cause of zero embeddings)

### Phase 2 (admin UI + access control)
- `f83e047` feat: per-agent allowed_nodes configuration (backend: Agent struct, DB, AgentStore, NodeManager, admin API)
- `3bd2856` fix: add allowedNodesJson param to RowToAgent declaration
- `27dc5e8` feat: NodesView admin page + allowed_nodes in agent form (frontend: NodesView, AgentsView combobox, router, sidebar, i18n)

### Auto-reconnect (Phase 3)
- `694b5d4` feat: auto-reconnect for node daemon with exponential backoff
- `766e893` fix: capture m_loop for disconnect handler in NodeDaemon
- `5a72ef6` fix: create fresh WebSocketClient per reconnect attempt
