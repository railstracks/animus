# Ticket 022 — Progress Report: System & File Tools

**Date:** 2026-05-05  
**Status:** Partially Complete

---

## Summary

Ticket 022 has been partially delivered. The core tool runtime and two foundational tools (`file`, `shell_exec`) are implemented and integrated into chain execution.  
The broader composite tool set defined in the ticket (`git`, `node_switch`, `config`) is not implemented in the current codebase.

---

## Implemented Scope

### 1. Tool execution infrastructure

- Tool registry and handler interface are implemented.
- Tool execution loop is wired through `ChainRunner`.
- Tool result modes (`deliver_to_model`, `stream_to_user`, `both`) are supported.

**Key files:**
- `include/animus_kernel/tools/ToolRegistry.h`
- `src/kernel/tools/ToolRegistry.cpp`
- `include/animus_kernel/tools/ToolTypes.h`
- `src/kernel/chain/ChainRunner.cpp`

### 2. `file` tool (composite actions)

- Implemented actions:
  - `read`
  - `write`
  - `edit`
- Read behavior includes truncation/line limit behavior with offset-based continuation.
- `edit` enforces unique `old_text` replacement semantics.
- Path policy support exists:
  - workspace restriction toggle
  - path allowlist
  - path denylist

**Key files:**
- `include/animus_kernel/tools/FileTool.h`
- `src/kernel/tools/FileTool.cpp`

### 3. `shell_exec` tool

- Command execution implemented with timeout handling and structured output semantics.
- Policy controls implemented:
  - enabled/disabled
  - command allowlist
  - command denylist
  - max timeout

**Key files:**
- `include/animus_kernel/tools/ShellExecTool.h`
- `src/kernel/tools/ShellExecTool.cpp`

### 4. Kernel registration

- Built-in tool registration includes `file` and `shell_exec`.

**Key file:**
- `src/kernel/AgentKernel.cpp`

---

## Not Yet Implemented (from Ticket 022 definition)

1. `git` composite tool.
2. `node_switch` tool.
3. `config` composite self-modification tool.
4. Integration tests specifically covering those missing tools.

---

## Acceptance Criteria Mapping

- `file` read/write/edit with boundary enforcement: **Implemented**
- `file` truncation + offset continuation: **Implemented**
- `file` unique edit semantics: **Implemented**
- `shell_exec` execution with timeout and structured output: **Implemented**
- `shell_exec` denylist enforcement: **Implemented**
- `git` tool actions and structured responses: **Not Implemented**
- `node_switch` context-switch behavior: **Not Implemented**
- `config` read/update audit-logged tool: **Not Implemented**
- All listed tools registered and callable: **Partially Implemented**

---

## Notes

The implemented portion of Ticket 022 now serves as the foundation for recent Ticket 033 work (per-agent File tool policy/config).  
Remaining Ticket 022 scope can be split into follow-up tickets if desired for cleaner delivery tracking.

