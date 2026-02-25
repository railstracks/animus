# Animus — Entity Relationship Diagram

This document describes the core data entities and their relationships.

## High-Level Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              ANIMUS KERNEL                                   │
│                                                                              │
│  ┌──────────┐    manages    ┌───────────┐    uses     ┌──────────────┐      │
│  │  Config  │──────────────▶│  Kernel   │◀────────────│  JobSystem   │      │
│  └──────────┘               └───────────┘             └──────────────┘      │
│                                   │                    │                     │
│       ┌───────────────────────────┼────────────────────┘                     │
│       │                           │                                          │
│       ▼                           ▼                                          │
│  ┌──────────┐              ┌────────────┐                                    │
│  │ Memory   │              │  Session   │                                    │
│  │ System   │              │  Manager   │                                    │
│  └──────────┘              └────────────┘                                    │
│       │                           │                                          │
└───────┼───────────────────────────┼──────────────────────────────────────────┘
        │                           │
        ▼                           ▼
┌────────────────┐          ┌──────────────┐
│  Memory Lanes  │          │   Sessions   │
│  ┌──────────┐  │          │              │
│  │EventLog  │  │          │  (per-user   │
│  │WorkingSet│  │          │   context)   │
│  │Semantic  │  │          └──────────────┘
│  └──────────┘  │
└────────────────┘
```

---

## Core Entities

### Session

A session represents an isolated conversational context with the agent.

| Field | Type | Description |
|-------|------|-------------|
| `id` | UUID | Unique session identifier |
| `user_id` | UUID | Owner of the session |
| `connector` | enum | Entry point: `slack`, `web`, `cli` |
| `created_at` | timestamp | Session creation time |
| `last_active` | timestamp | Most recent activity |
| `state` | enum | `active`, `paused`, `closed` |
| `context_budget` | int | Token budget for this session |
| `metadata` | JSON | Connector-specific data (channel ID, thread ID, etc.) |

**Relationships:**
- `Session` belongs to one `User`
- `Session` has many `Messages`
- `Session` has one `WorkingMemory` snapshot
- `Session` has many `Jobs` (via JobSystem)

---

### User

Represents a human interacting with the agent.

| Field | Type | Description |
|-------|------|-------------|
| `id` | UUID | Unique user identifier |
| `external_id` | string | Connector-specific user ID (Slack user ID, etc.) |
| `display_name` | string | Human-readable name |
| `created_at` | timestamp | First seen |
| `preferences` | JSON | User-specific settings |
| `permissions` | JSON | Authorization grants |

**Relationships:**
- `User` has many `Sessions`
- `User` has one `SemanticMemory` partition

---

### Message

A single message within a session.

| Field | Type | Description |
|-------|------|-------------|
| `id` | UUID | Unique message identifier |
| `session_id` | UUID | Parent session |
| `role` | enum | `user`, `assistant`, `system`, `tool` |
| `content` | text | Message body |
| `created_at` | timestamp | When message was created |
| `metadata` | JSON | Additional context (tool call info, etc.) |
| `token_count` | int | Approximate token count |
| `redacted` | bool | Whether content was redacted |

**Relationships:**
- `Message` belongs to one `Session`
- `Message` may reference `ToolCall` records

---

### ToolCall

Records of tool/function invocations.

| Field | Type | Description |
|-------|------|-------------|
| `id` | UUID | Unique call identifier |
| `message_id` | UUID | Parent message (assistant) |
| `tool_name` | string | Name of invoked tool |
| `arguments` | JSON | Input arguments |
| `result` | JSON | Output (may be redacted) |
| `status` | enum | `pending`, `success`, `error`, `blocked` |
| `duration_ms` | int | Execution time |
| `gate_result` | enum | `allowed`, `denied`, `asked` |

**Relationships:**
- `ToolCall` belongs to one `Message`
- `ToolCall` may reference a `Hook` definition

---

## Memory Entities

### EventLog (Append-Only)

Immutable record of all events.

| Field | Type | Description |
|-------|------|-------------|
| `id` | serial | Auto-increment sequence |
| `timestamp` | timestamp | When event occurred |
| `session_id` | UUID | Associated session (nullable) |
| `event_type` | enum | `message`, `tool_call`, `gate`, `memory_write`, etc. |
| `payload` | JSON | Event-specific data |
| `hash` | string | Hash for integrity verification |

**Properties:**
- Append-only (no updates or deletes)
- Used for audit trails and replay

---

### WorkingSet (Working Memory)

Short-term active context for ongoing work.

| Field | Type | Description |
|-------|------|-------------|
| `id` | UUID | Unique entry |
| `session_id` | UUID | Associated session |
| `key` | string | Semantic key (e.g., "current_project") |
| `value` | JSON | Current value |
| `priority` | int | Promotion/demotion weight |
| `last_accessed` | timestamp | For LRU eviction |
| `expires_at` | timestamp | Optional TTL |

**Relationships:**
- `WorkingSet` belongs to one `Session`
- Promoted entries become `SemanticMemory` facts

---

### SemanticMemory (Curated Facts)

Durable, curated knowledge.

| Field | Type | Description |
|-------|------|-------------|
| `id` | UUID | Unique fact identifier |
| `user_id` | UUID | Owner (or null for global) |
| `entity_type` | enum | `person`, `project`, `concept`, `preference`, etc. |
| `entity_name` | string | Human-readable identifier |
| `content` | text | The fact itself |
| `embedding` | vector | For similarity search |
| `confidence` | float | How confident is this fact? |
| `source` | string | Where this came from |
| `created_at` | timestamp | When recorded |
| `updated_at` | timestamp | Last modification |
| `superseded_by` | UUID | If replaced by newer fact |

**Relationships:**
- `SemanticMemory` may belong to one `User` (user-scoped) or be global
- `SemanticMemory` entries can link to each other (supersession chain)

---

## Connector Entities

### Connector (Abstract)

Base definition for external interfaces.

| Field | Type | Description |
|-------|------|-------------|
| `id` | string | Connector identifier (e.g., "slack", "web") |
| `type` | enum | `messaging`, `ui`, `cli` |
| `config` | JSON | Connector-specific configuration |
| `enabled` | bool | Whether connector is active |

---

### SlackConnector (Concrete)

Slack-specific configuration.

| Field | Type | Description |
|-------|------|-------------|
| `connector_id` | string | FK to Connector |
| `bot_token` | secret | Slack bot token |
| `signing_secret` | secret | For request verification |
| `allowed_channels` | array | Channel whitelist |
| `team_id` | string | Slack workspace ID |

---

## LLM Provider Entities

### Provider

LLM provider definition.

| Field | Type | Description |
|-------|------|-------------|
| `id` | string | Provider identifier (e.g., "openai", "mistral") |
| `dll_path` | string | Path to provider DLL |
| `config` | JSON | Provider-specific configuration |
| `priority` | int | Failover ordering |
| `enabled` | bool | Whether provider is active |
| `capabilities` | JSON | Discovered capabilities |

---

### PromptCategory

Classification of prompt types.

| Field | Type | Description |
|-------|------|-------------|
| `id` | string | Category identifier (e.g., "triage", "planning") |
| `default_provider` | string | FK to Provider |
| `default_model` | string | Model override |
| `context_budget` | int | Token budget |
| `allowed_tools` | array | Tool whitelist |
| `memory_lanes` | array | Which memory lanes to query |

---

## Job System Entities

### Job

Unit of schedulable work.

| Field | Type | Description |
|-------|------|-------------|
| `id` | uint64 | Job identifier |
| `session_id` | UUID | Associated session |
| `type` | enum | `llm_call`, `tool_call`, `memory_read`, `hook` |
| `status` | enum | `pending`, `running`, `complete`, `failed`, `canceled` |
| `dependencies` | array | List of job IDs that must complete first |
| `created_at` | timestamp | When queued |
| `started_at` | timestamp | When execution began |
| `completed_at` | timestamp | When finished |

---

## Entity Relationship Summary

```
User
  │
  ├─── has many ───▶ Session
  │                     │
  │                     ├─── has many ───▶ Message
  │                     │                     │
  │                     │                     └─── has many ───▶ ToolCall
  │                     │
  │                     └─── has one ───▶ WorkingSet
  │
  └─── has one ───▶ SemanticMemory (partition)

Kernel
  │
  ├─── uses ───▶ JobSystem ───▶ Job (per Session)
  │
  ├─── manages ───▶ Provider ───▶ PromptCategory
  │
  └─── owns ───▶ Connector ───▶ SlackConnector / WebConnector / CLIConnector
```

---

## Notes

- **Isolation**: Sessions are isolated from each other; no cross-session data access without explicit action.
- **Auditability**: EventLog captures everything; never modified after write.
- **Extensibility**: New connectors and providers add rows to their respective tables; no schema changes required.
