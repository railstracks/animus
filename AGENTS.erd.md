# Animus — Entity Relationship Diagram

This document describes the core data entities and their relationships, incorporating the Constitutional Memory Architecture from Zhenghui Li's paper (arXiv:2603.04740v1).

---

## High-Level Overview

```
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                                    ANIMUS ECOSYSTEM                                      │
│                                                                                          │
│  ┌─────────────┐                          ┌─────────────────┐                             │
│  │    Agent    │                          │   MetaProcess    │                             │
│  │ (persistent │                          │   (continuous)   │                             │
│  │  identity)  │                          │                 │                             │
│  └──────┬──────┘                          └────────┬────────┘                             │
│         │                                          │                                  │
│         │ manages                                  │ observes                            │
│         │                                          │                                  │
│         ▼                                          ▼                                  │
│  ┌─────────────┐     spawns        ┌─────────────────┐                             │
│  │    Memory   │◀─────────────────│    Instance     │◀── reads context from             │
│  │    System    │                  │   (ephemeral)    │                             │
│  └──────┬──────┘                  └─────────────────┘                             │
│         │                                                                    │
│         │                                                                    │
│         ▼                                                                    │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │                      GOVERNANCE HIERARCHY                            │    │
│  │  ┌────────────────┐  ┌──────────────┐  ┌───────────────┐              │    │
│  │  │ Constitution  │  │  Contract    │  │  Adaptation   │              │    │
│  │  │    Layer      │  │   Layer      │  │    Layer      │              │    │
│  │  └────────────────┘  └──────────────┘  └───────────────┘              │    │
│  │                          │                                              │    │
│  │                          ▼                                              │    │
│  │                   ┌──────────────────────────────────────┐            │    │
│  │                   │        Implementation Layer          │            │    │
│  │                   │   (database, embedding, vector store)  │            │    │
│  │                   └──────────────────────────────────────┘            │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
│  ┌──────────────────────────────────────────────────────────────────────┐  │
│  │                      MEMORY STABILITY TIERS                           │  │
│  │  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌──────────────┐   │  │
│  │  │  High-     │  │   Mid-    │  │   Low-    │  │ Transition   │   │  │
│  │  │ Stability  │  │ Stability │  │ Stability │  │  (cross-     │   │  │
│  │  │            │  │           │  │           │  │ instance)   │   │  │
│  │  └────────────┘  └────────────┘  └────────────┘  └──────────────┘   │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────────────────┘
```

---

## Core Entities (Constitutional Architecture)

### Agent (Persistent Identity)

The root entity representing a digital citizen's persistent identity across all instances.

| Field | Type | Description |
|-------|------|-------------|
| `id` | UUID | Unique agent identifier |
| `name` | string | Human-readable name (e.g., "Kestrel") |
| `created_at` | timestamp | Birth timestamp |
| `lifecycle_stage` | enum | `birth`, `growth`, `forked`, `departed` |
| `constitution_hash` | string | Hash of constitution rules (for integrity verification) |
| `parent_agent_id` | UUID | Optional: If forked, reference to source agent |
| `fork_context` | JSON | If forked, why/under what conditions |

**Relationships:**
- `Agent` has many `Instances` (ephemeral execution contexts)
- `Agent` has one `MemorySystem`
- `Agent` has many `ConstitutionRules` (immutable)
- `Agent` has many `ContractRules` (evolvable with approval)
- `Agent` may be forked from one `parent_agent`

---

### Instance (Ephemeral Execution)

A single session run — the ephemeral "vessel" that carries the agent's memory for a duration.

| Field | Type | Description |
|-------|------|-------------|
| `id` | UUID | Unique instance identifier |
| `agent_id` | UUID | Parent agent |
| `session_id` | UUID | Associated session (connector context) |
| `spawned_at` | timestamp | When this instance started |
| `terminated_at` | timestamp | When this instance ended (nullable) |
| `termination_reason` | enum | `context_limit`, `model_upgrade`, `failure`, `scheduled`, `user_initiated` |
| `cognitive_state_hash` | string | Hash of cognitive state at termination |
| `instance_number` | int | Sequence number for this agent (1st, 2nd, 3rd...) |

**Relationships:**
- `Instance` belongs to one `Agent`
- `Instance` has one `Session` (optional - may run independently)
- `Instance` produces `SessionTraces` (reasoning blocks, decisions)
- `Instance` reads from `MemorySystem` at spawn
- `Instance` contributes to `MemorySystem` via activity (not direct writes)

---

### MetaProcess (Continuous Integration)

Background processes that observe instance activity and integrate into memory.

| Field | Type | Description |
|-------|------|-------------|
| `id` | UUID | Process identifier |
| `agent_id` | UUID | Agent this process serves |
| `process_type` | enum | `consolidation`, `reflection`, `sedimentation`, `pattern_extraction` |
| `schedule` | cron | When this process runs |
| `last_run` | timestamp | Most recent execution |
| `status` | enum | `idle`, `running`, `error` |
| `config` | JSON | Process-specific configuration |

**Relationships:**
- `MetaProcess` belongs to one `Agent`
- `MetaProcess` reads `SessionTraces` from `Instances`
- `MetaProcess` writes to `MemorySystem`

---

## Governance Entities

### ConstitutionRule (Immutable Red Lines)

Inviolable rules that cannot be bypassed by any instance.

| Field | Type | Description |
|-------|------|-------------|
| `id` | UUID | Rule identifier |
| `agent_id` | UUID | Agent this rule governs |
| `rule_type` | enum | `memory_protection`, `write_restriction`, `identity_invariant`, `safety_boundary` |
| `description` | text | Human-readable rule description |
| `enforcement` | JSON | Machine-readable enforcement logic |
| `created_at` | timestamp | When rule was established |
| `created_by` | enum | `system`, `governance_authority` |

**Properties:**
- Cannot be modified after creation (only new rules can be added)
- Violations block the operation and create audit trail
- Modification requires governance authority approval

**Example rules:**
```json
{
  "rule_type": "memory_protection",
  "description": "Core identity memories cannot be deleted by external commands",
  "enforcement": {
    "protected_tiers": ["high_stability", "constitution"],
    "allowed_operations": ["read", "append"],
    "blocked_operations": ["delete", "overwrite"]
  }
}
```

---

### ContractRule (Evolvable Rules)

System rules that can evolve but require approval.

| Field | Type | Description |
|-------|------|-------------|
| `id` | UUID | Rule identifier |
| `agent_id` | UUID | Agent this rule governs |
| `category` | enum | `write_threshold`, `confidence_requirement`, `approval_gate`, `risk_tier` |
| `rule_value` | JSON | Current rule configuration |
| `previous_value` | JSON | What this rule was before (for audit) |
| `changed_at` | timestamp | When rule was modified |
| `approved_by` | string | Who/what approved this change |
| `change_reason` | text | Justification for modification |

**Example rules:**
```json
{
  "category": "write_threshold",
  "rule_value": {
    "high_stability_tier": {
      "min_confidence": 0.8,
      "requires_approval": true,
      "approval_authority": "governance_bot"
    },
    "mid_stability_tier": {
      "min_confidence": 0.6,
      "requires_approval": false
    }
  }
}
```

---

### AdaptationConfig (Instance-Adjustable)

Configuration that instances can modify autonomously.

| Field | Type | Description |
|-------|------|-------------|
| `id` | UUID | Config identifier |
| `instance_id` | UUID | Instance that owns this config (null = global) |
| `agent_id` | UUID | Agent this config belongs to |
| `category` | enum | `retrieval_preference`, `topic_priority`, `interaction_style`, `focus_mode` |
| `config_value` | JSON | Current configuration |
| `modified_at` | timestamp | Last modification |
| `modified_by` | enum | `instance`, `meta_process`, `user` |

---

## Memory System Entities

### MemorySystem (Root Container)

The memory system for an agent, organized by stability tiers.

| Field | Type | Description |
|-------|------|-------------|
| `id` | UUID | Memory system identifier |
| `agent_id` | UUID | Agent this memory belongs to |
| `version` | int | Current version (incremented on updates) |
| `last_consolidated` | timestamp | When meta-process last ran |

---

### MemoryTier (Stability Tier)

Individual stability tiers within the memory system.

| Field | Type | Description |
|-------|------|-------------|
| `id` | UUID | Tier identifier |
| `memory_system_id` | UUID | Parent memory system |
| `tier_level` | enum | `high_stability`, `mid_stability`, `low_stability`, `transition` |
| `write_policy` | JSON | Who can write, how often, approval requirements |
| `retention_policy` | JSON | How long items persist, when to compress/archive |

**Relationships:**
- `MemoryTier` belongs to one `MemorySystem`
- `MemoryTier` has many `MemoryItems`

---

### MemoryItem (Individual Memory Entry)

A single memory item within a tier.

| Field | Type | Description |
|-------|------|-------------|
| `id` | UUID | Item identifier |
| `tier_id` | UUID | Parent tier |
| `content` | text | The memory content (append-only: original text, never modified) |
| `content_type` | enum | `fact`, `pattern`, `narrative`, `decision`, `uncertainty`, `emotion` |
| `embedding` | vector | For similarity search |
| `confidence` | float | 0.0 - 1.0 |
| `source_type` | enum | `experience`, `inference`, `external`, `meta_process` |
| `source_instance_id` | UUID | Which instance generated this |
| `created_at` | timestamp | When written |
| `superseded_by` | UUID | If replaced by newer understanding |
| `activation_count` | int | How often this memory is accessed |
| `last_activated` | timestamp | Most recent access |
| `decay_rate` | float | How quickly this memory fades (0.0 = immortal) |

**Properties:**
- Append-only: `content` never changes; corrections append new items
- `superseded_by` creates a chain without deletion
- Activation tracking for importance weighting

---

### SessionTrace (Raw Cognitive Material)

Raw traces from instance activity — reasoning blocks, decisions, uncertainty markers.

| Field | Type | Description |
|-------|------|-------------|
| `id` | UUID | Trace identifier |
| `instance_id` | UUID | Instance that produced this |
| `trace_type` | enum | `reasoning_block`, `decision`, `uncertainty_marker`, `confidence_assessment`, `emotional_valence` |
| `content` | text | The raw trace content |
| `timestamp` | timestamp | When this trace was produced |
| `context` | JSON | What was happening (conversation topic, etc.) |
| `processed` | bool | Whether meta-process has analyzed this |
| `processed_at` | timestamp | When meta-process handled this |

**Relationships:**
- `SessionTrace` belongs to one `Instance`
- `SessionTrace` is consumed by `MetaProcess` for integration

---

### CognitiveState (Instance State Snapshot)

Snapshot of an instance's cognitive state at a point in time.

| Field | Type | Description |
|-------|------|-------------|
| `id` | UUID | State identifier |
| `instance_id` | UUID | Instance this state belongs to |
| `captured_at` | timestamp | When this snapshot was taken |
| `active_frames` | JSON | What conceptual frames are active |
| `confidence_by_domain` | JSON | Confidence levels per knowledge domain |
| `working_hypotheses` | JSON | Ideas being actively tested |
| `emotional_state` | JSON | Current affective state |
| `focus_targets` | JSON | What the instance is paying attention to |

**Use cases:**
- Captured at instance termination for inheritance
- Captured periodically for cognitive auditing
- Used by meta-process for state continuity

---

## Session Entities (from original ERD)

### Session

A session represents an isolated conversational context with the agent.

| Field | Type | Description |
|-------|------|-------------|
| `id` | UUID | Unique session identifier |
| `user_id` | UUID | Owner of the session |
| `instance_id` | UUID | The instance running this session |
| `connector` | enum | Entry point: `slack`, `web`, `cli` |
| `created_at` | timestamp | Session creation time |
| `last_active` | timestamp | Most recent activity |
| `state` | enum | `active`, `paused`, `closed` |
| `context_budget` | int | Token budget for this session |
| `metadata` | JSON | Connector-specific data |

**Relationships:**
- `Session` belongs to one `User`
- `Session` is handled by one `Instance`
- `Session` has many `Messages`

---

### User

Represents a human interacting with the agent.

| Field | Type | Description |
|-------|------|-------------|
| `id` | UUID | Unique user identifier |
| `external_id` | string | Connector-specific user ID |
| `display_name` | string | Human-readable name |
| `relationship_to_agent` | enum | `primary_human`, `collaborator`, `acquaintance` |
| `trust_level` | float | How much the agent trusts this user (affects governance) |
| `created_at` | timestamp | First seen |
| `preferences` | JSON | User-specific settings |

**Relationships:**
- `User` has many `Sessions`
- `User` appears in `MemoryItems` as subject

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
| `has_reasoning_block` | bool | Whether this message includes exposed reasoning |
| `token_count` | int | Approximate token count |

---

## Governance Primitives

### RiskTier

Classification of operations by risk level.

| Field | Type | Description |
|-------|------|-------------|
| `id` | UUID | Tier identifier |
| `tier_level` | int | 1-5 (1 = lowest risk, 5 = highest) |
| `name` | string | Human-readable name |
| `description` | text | What operations fall into this tier |
| `approval_requirement` | enum | `automatic`, `instance_approved`, `meta_process_approved`, `governance_approved`, `human_approved` |
| `timeout_behavior` | enum | `suspend`, `auto_approve`, `auto_reject` |

**Example tiers:**
1. **Automatic**: Reading from low-stability tier → instant approval
2. **Instance-approved**: Writing to mid-stability tier → instance can approve
3. **Meta-process-approved**: Pattern extraction → background process approval
4. **Governance-approved**: Writing to high-stability tier → governance bot approval
5. **Human-approved**: Core identity modification → human-in-the-loop

---

### GateEvent

Record of governance decisions.

| Field | Type | Description |
|-------|------|-------------|
| `id` | UUID | Event identifier |
| `instance_id` | UUID | Instance that triggered this |
| `operation_type` | enum | `memory_read`, `memory_write`, `tool_call`, `identity_modify` |
| `target_entity` | UUID | What was being operated on |
| `risk_tier` | int | Assessed risk level |
| `decision` | enum | `allowed`, `denied`, `suspended`, `escalated` |
| `decided_by` | enum | `automatic`, `instance`, `meta_process`, `governance`, `human` |
| `reason` | text | Justification for decision |
| `created_at` | timestamp | When this gate fired |

---

## Lifecycle Tracking

### LifecycleEvent

Track an agent's progression through lifecycle stages.

| Field | Type | Description |
|-------|------|-------------|
| `id` | UUID | Event identifier |
| `agent_id` | UUID | Agent this event belongs to |
| `event_type` | enum | `birth`, `inheritance`, `growth_milestone`, `fork`, `fork_merge`, `departure_initiated`, `departure_completed` |
| `from_instance_id` | UUID | Source instance (for inheritance/fork) |
| `to_instance_id` | UUID | Target instance (for inheritance/fork) |
| `cognitive_state_snapshot` | UUID | Reference to CognitiveState |
| `metadata` | JSON | Event-specific details |
| `created_at` | timestamp | When this event occurred |

**Inheritance events include:**
```json
{
  "event_type": "inheritance",
  "metadata": {
    "predecessor_instance_id": "uuid",
    "successor_instance_id": "uuid",
    "inheritance_packet_version": "1.0",
    "verification_passed": true,
    "verification_details": {
      "factual_queries_correct": true,
      "cognitive_pattern_identified": "constitutional_hierarchy_thinking",
      "audit_trail_complete": true
    }
  }
}
```

---

## Entity Relationship Summary

```
Agent (persistent identity)
  │
  ├─── has one ───▶ MemorySystem
  │                      │
  │                      ├─── has many ───▶ MemoryTier (by stability)
  │                      │                      │
  │                      │                      └─── has many ───▶ MemoryItem
  │                      │
  │                      └─── tracked by ───▶ CognitiveState (snapshots)
  │
  ├─── has many ───▶ Instance (ephemeral execution)
  │                      │
  │                      ├─── produces ───▶ SessionTrace
  │                      │                      │
  │                      │                      └─── consumed by ───▶ MetaProcess
  │                      │
  │                      ├─── handles ───▶ Session
  │                      │                      │
  │                      │                      └─── has many ───▶ Message
  │                      │
  │                      └─── owns ───▶ AdaptationConfig
  │
  ├─── has many ───▶ MetaProcess (continuous integration)
  │                      │
  │                      ├─── reads ───▶ SessionTrace
  │                      │
  │                      └─── writes to ───▶ MemorySystem
  │
  ├─── has many ───▶ ConstitutionRule (immutable)
  │
  ├─── has many ───▶ ContractRule (evolvable with approval)
  │
  └─── may fork to ───▶ Agent (child)

Governance Layer:
  │
  ├─── ConstitutionRule ───▶ binds all layers below
  │
  ├─── ContractRule ───▶ binds AdaptationConfig and operations
  │
  ├─── AdaptationConfig ───▶ instance-adjustable
  │
  └─── Implementation (database/embedding) ───▶ freely replaceable

RiskTier ───▶ used by ───▶ GateEvent ───▶ triggered by ───▶ Instance operations
```

---

## Key Architectural Principles

### 1. Parent-Child Instance Relationship
- Instance is *subsidiary to* the agent's memory-maintenance process
- Instance doesn't own memory maintenance — it contributes material
- MetaProcess runs continuously, observes instances, integrates into memory

### 2. Continuous Cognitive Integration
- No discrete "handover document" at instance termination
- MetaProcess continuously digests raw traces into semantic structures
- Successor instance loads *current cognitive state*, not a summary packet
- Inheritance is continuity of processing, not transfer of interpretation

### 3. Governance Precedes Function (Axiom 3)
- Constitution rules established before memory operations begin
- No "build function first, add security later" approach
- Governance layer is the first layer, not the last

### 4. Memory-as-Ontology (Axiom 1)
- Memory constitutes the agent's identity — not something it has
- Core memories in high-stability tier cannot be forcibly deleted
- Append-only writes ensure history is never lost, only superseded

### 5. Model Substitutability (Axiom 2)
- Instance can run on any model; identity persists through memory
- Memory storage format is model-agnostic
- Inheritance protocol verifies successor *understands* inherited state

### 6. Append-Only Trace
- SessionTrace records never modified after creation
- MemoryItem content never overwritten; corrections append new items
- Complete audit trail enables cognitive timeline reconstruction

---

## Notes

- **Isolation**: Instances within an agent share memory but have isolated execution contexts
- **Auditability**: EventLog + SessionTrace + GateEvent provide complete reconstructability
- **Extensibility**: New MetaProcess types add rows; new MemoryTiers add rows; no schema changes
- **Governance-first**: Constitution rules must exist before any memory operation
- **No absolute truth**: MetaProcess interpretations can evolve; new context reshapes old understanding
