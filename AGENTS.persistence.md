# Animus — Persistence Design

This document describes how Animus stores and retrieves data, focusing on durability, performance, and isolation.

## Design Principles

1. **EventLog is append-only** — Never modify or delete; the audit trail is sacred.
2. **WorkingSet is ephemeral** — Fast, in-memory, may be evicted; survives restart only via optional snapshots.
3. **SemanticMemory is durable** — Curated facts stored persistently with versioning.
4. **Session state is serializable** — Any session can be checkpointed and restored.
5. **Secrets are never persisted in plaintext** — Use OS keychain or encrypted storage.

---

## Storage Engines

### Overview

| Storage | Engine | Purpose |
|---------|--------|---------|
| EventLog | SQLite (WAL mode) | Append-only event stream |
| WorkingSet | In-memory + optional Redis | Fast ephemeral context |
| SemanticMemory | SQLite + vector extension | Durable facts with similarity search |
| Session State | SQLite | Session checkpointing |
| Configuration | SQLite + file overlay | System and user config |
| Secrets | OS keychain / encrypted file | Credentials |

---

## EventLog

### Purpose

Immutable, append-only log of all events for:
- Audit trails
- Replay/debugging
- Compliance

### Schema

```sql
CREATE TABLE event_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    session_id TEXT,  -- NULL for system events
    event_type TEXT NOT NULL,
    payload TEXT NOT NULL,  -- JSON
    hash TEXT NOT NULL      -- SHA-256 of (timestamp + type + payload)
);

CREATE INDEX idx_event_log_timestamp ON event_log(timestamp);
CREATE INDEX idx_event_log_session ON event_log(session_id);
CREATE INDEX idx_event_log_type ON event_log(event_type);
```

### Properties

- **Append-only**: No UPDATE or DELETE operations permitted
- **Hash chain**: Each row includes a hash for tamper detection
- **WAL mode**: Write-Ahead Logging for durability without blocking reads

### Access Patterns

```cpp
class EventLog {
public:
    // Append (write-once)
    int64_t Append(const Event& event);

    // Query (read-only)
    std::vector<Event> Query(
        std::optional<SessionId> session,
        std::optional<EventType> type,
        std::optional<std::string> after,
        size_t limit = 1000);

    // Integrity check
    bool VerifyIntegrity();  // Checks hash chain

private:
    sqlite::database m_db;
};
```

### Retention Policy

- Default: keep all events (append-only never deletes)
- Optional: archive old events to cold storage
- Configurable: per-event-type TTL for non-critical events

---

## WorkingSet

### Purpose

Short-term, fast-access context for active sessions.

### Storage

**Primary: In-memory (per process)**

```cpp
class WorkingSet {
    struct Entry {
        json value;
        int priority;
        std::chrono::steady_clock::time_point lastAccessed;
        std::optional<std::chrono::system_clock::time_point> expiresAt;
    };

    std::unordered_map<std::string, Entry> m_entries;
    size_t m_maxEntries;
};
```

**Optional: Redis backend for distributed deployments**

```
Key: animus:session:{session_id}:workingset:{key}
Value: JSON
TTL: Configurable per-entry
```

### Eviction Policy

1. **Expired entries** — Removed on access or periodic sweep
2. **LRU by priority** — When budget exceeded:
   - Sort by (priority desc, lastAccessed asc)
   - Evict lowest priority, oldest accessed first

### Persistence

- **On graceful shutdown**: Serialize to session checkpoint
- **On crash**: Lost (by design — it's ephemeral)
- **Promotion**: Selected entries promoted to SemanticMemory

---

## SemanticMemory

### Purpose

Durable, curated facts about users, projects, concepts, etc.

### Schema

```sql
CREATE TABLE semantic_memory (
    id TEXT PRIMARY KEY,  -- UUID
    user_id TEXT,  -- NULL for global facts
    entity_type TEXT NOT NULL,
    entity_name TEXT NOT NULL,
    content TEXT NOT NULL,
    embedding BLOB,  -- Float32 vector (optional, for similarity)
    confidence REAL NOT NULL DEFAULT 1.0,
    source TEXT,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    superseded_by TEXT,  -- FK to semantic_memory.id
    is_active INTEGER NOT NULL DEFAULT 1,

    FOREIGN KEY (superseded_by) REFERENCES semantic_memory(id)
);

CREATE INDEX idx_semantic_memory_user ON semantic_memory(user_id);
CREATE INDEX idx_semantic_memory_entity ON semantic_memory(entity_type, entity_name);
CREATE INDEX idx_semantic_memory_active ON semantic_memory(is_active);
```

### Vector Search (Optional)

Using SQLite with vector extension (e.g., `sqlite-vss`):

```sql
CREATE VIRTUAL TABLE semantic_memory_vectors USING vss0(
    embedding(1536)  -- Dimension matches embedding model
);
```

Or external:
- **Qdrant** (self-hosted)
- **Pinecone** (cloud)
- **pgvector** (if using PostgreSQL)

### Access Patterns

```cpp
class SemanticMemory {
public:
    // Store
    FactId Store(const Fact& fact);

    // Retrieve by ID
    std::optional<Fact> GetById(FactId id);

    // Search by entity
    std::vector<Fact> GetByEntity(
        EntityType type,
        const std::string& name,
        std::optional<UserId> user = std::nullopt);

    // Similarity search (if embeddings enabled)
    std::vector<Fact> SearchSimilar(
        const std::vector<float>& queryEmbedding,
        size_t k = 10,
        float threshold = 0.7);

    // Supersede (soft delete + replacement)
    void Supersede(FactId oldId, FactId newId);

private:
    sqlite::database m_db;
    std::unique_ptr<VectorIndex> m_vectorIndex;  // Optional
};
```

### Promotion from WorkingSet

```cpp
void PromoteWorkingSetToFacts(SessionId sessionId) {
    WorkingSet& ws = memoryManager.GetWorkingSet(sessionId);

    for (const auto& [key, entry] : ws.GetAll()) {
        if (entry.priority >= promotionThreshold) {
            Fact fact;
            fact.entityType = InferEntityType(key);
            fact.entityName = key;
            fact.content = entry.value.dump();
            fact.source = "working_set_promotion";
            fact.confidence = 0.8;  // Initial confidence

            memoryManager.StoreFact(fact);
        }
    }
}
```

---

## Session State

### Purpose

Checkpoint session state for:
- Graceful restart
- Migration between nodes
- Debugging/replay

### Schema

```sql
CREATE TABLE sessions (
    id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL,
    connector TEXT NOT NULL,
    state TEXT NOT NULL,  -- JSON-serialized SessionState
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    is_active INTEGER NOT NULL DEFAULT 1
);

CREATE TABLE session_messages (
    id TEXT PRIMARY KEY,
    session_id TEXT NOT NULL,
    role TEXT NOT NULL,
    content TEXT NOT NULL,
    created_at TEXT NOT NULL,
    token_count INTEGER,
    metadata TEXT,  -- JSON

    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE
);

CREATE INDEX idx_session_messages_session ON session_messages(session_id);
```

### Serialization

```cpp
struct SessionState {
    SessionId id;
    UserId userId;
    ConnectorType connector;
    json connectorMetadata;  // Channel IDs, thread IDs, etc.
    std::vector<Message> messages;
    WorkingSet workingSet;
    SessionStatus status;
    int contextBudget;
    int tokensUsed;
};

// Serialize to JSON for storage
json SerializeSessionState(const SessionState& state);
SessionState DeserializeSessionState(const json& j);
```

### Checkpointing

```cpp
class SessionCheckpoint {
public:
    // Save checkpoint
    void Save(SessionId sessionId);

    // Restore checkpoint
    std::optional<SessionState> Load(SessionId sessionId);

    // List available checkpoints
    std::vector<SessionId> ListCheckpoints();

    // Delete old checkpoints
    void Prune(std::chrono::hours olderThan);
};
```

---

## Configuration Storage

### System Configuration

**File-based (YAML/JSON)**: Read on startup, watched for changes.

```yaml
# /etc/animus/config.yaml
kernel:
  job_workers: auto
  default_timeout_ms: 30000

memory:
  event_log_path: /var/lib/animus/event_log.db
  semantic_memory_path: /var/lib/animus/semantic_memory.db
  working_set_max_entries: 1000

providers:
  - id: openai
    dll: /usr/lib/animus/providers/libopenai_provider.so
    config:
      api_key_env: OPENAI_API_KEY
```

**Database overlay**: Runtime-modifiable settings stored in SQLite.

```sql
CREATE TABLE config (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL,  -- JSON
    updated_at TEXT NOT NULL
);
```

---

## Secrets Management

### Principle

**Never store secrets in the database or config files.**

### Options

1. **Environment variables** — Simple, works for containers
2. **OS keychain** — macOS Keychain, Windows Credential Manager, libsecret (Linux)
3. **Encrypted file** — Age, GPG, or custom encryption
4. **Secrets manager** — HashiCorp Vault, AWS Secrets Manager, etc.

### Implementation

```cpp
class SecretsStore {
public:
    // Get secret (may prompt user or read from keychain)
    std::optional<std::string> Get(const std::string& key);

    // Set secret (stores in keychain)
    void Set(const std::string& key, const std::string& value);

    // Delete secret
    void Delete(const std::string& key);

private:
    std::unique_ptr<KeychainBackend> m_backend;
};
```

### Provider Credentials

Provider DLLs receive credentials at runtime:

```cpp
// In config
providers:
  - id: openai
    api_key_secret: openai_api_key  // Key in secrets store

// At runtime
std::string apiKey = secretsStore.Get("openai_api_key");
provider->Initialize({{"api_key", apiKey}});
```

---

## Backup and Recovery

### EventLog Backup

- **Frequency**: Continuous (WAL) + daily full backup
- **Retention**: Configurable (default: 90 days)
- **Format**: SQLite backup + WAL archive

### SemanticMemory Backup

- **Frequency**: Daily incremental, weekly full
- **Retention**: Unlimited (curated facts are valuable)

### Session Backups

- **Frequency**: On checkpoint, on graceful shutdown
- **Retention**: 7 days for inactive sessions

### Recovery Procedure

1. Restore EventLog from backup
2. Verify integrity (hash chain check)
3. Restore SemanticMemory
4. Restore active sessions from last checkpoint
5. Resume connectors

---

## Distributed Deployment

### Single-Node (Default)

All storage is local SQLite. Simple, fast, no dependencies.

### Multi-Node (Future)

For horizontal scaling:

| Component | Distributed Solution |
|-----------|----------------------|
| EventLog | PostgreSQL / TimescaleDB |
| SemanticMemory | PostgreSQL + pgvector |
| WorkingSet | Redis Cluster |
| Sessions | PostgreSQL |
| Config | etcd / Consul |

**Note**: Distributed mode is a future enhancement; initial implementation is single-node only.

---

## File Layout

```
/var/lib/animus/
├── event_log.db          # EventLog SQLite
├── event_log.db-wal      # WAL file
├── semantic_memory.db    # SemanticMemory SQLite
├── sessions.db           # Session state SQLite
├── config.db             # Runtime config SQLite
└── checkpoints/          # Session checkpoint snapshots
    ├── {session_id_1}.json
    └── {session_id_2}.json

/etc/animus/
├── config.yaml           # Main config file
└── providers/            # Provider configurations
    ├── openai.yaml
    └── mistral.yaml

/usr/lib/animus/
├── providers/            # Provider DLLs
│   ├── libopenai_provider.so
│   └── libmistral_provider.so
└── connectors/           # Connector libraries
    └── libslack_connector.so
```

---

## Performance Considerations

### EventLog

- **Write throughput**: 10K+ events/sec (WAL mode, async)
- **Read latency**: <1ms for indexed queries
- **Optimization**: Batch inserts for high-volume periods

### SemanticMemory

- **Write latency**: <5ms (single insert)
- **Read latency**: <2ms (indexed lookup)
- **Vector search**: <10ms for top-10 in 100K vectors (with index)

### WorkingSet

- **Read/Write latency**: <0.1ms (in-memory)
- **Memory budget**: ~1MB per active session

---

## Migration Strategy

### Versioning

All SQLite databases include a `schema_version` table:

```sql
CREATE TABLE schema_version (
    version INTEGER PRIMARY KEY,
    applied_at TEXT NOT NULL,
    description TEXT
);
```

### Migration Tool

```bash
animus-migrate --target 3
# Applies migrations 1 → 2 → 3
```

Migrations are idempotent and can be rolled back (for non-append-only tables).
