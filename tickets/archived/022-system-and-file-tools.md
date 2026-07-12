# Ticket 022 — System & File Tools

**Priority:** P1 (foundational capability tools, depends on 021 tool infrastructure)
**Status:** Open
**Dependencies:** Ticket 021 (Tool Calling and Reasoning Mode)
**Updated:** 2026-05-01

## Summary

Implement the first concrete tools that let the agent operate on its local environment: read and write files, execute shell commands, interact with git repositories, switch execution nodes, and read/modify its own configuration. These are the "agent acts on its world" tools.

## Tools

### `file` (composite)

Structured file operations. Composite tool — the LLM calls `file` with an `action` parameter that routes to the appropriate subcommand.

```json
{
    "name": "file",
    "description": "Read, write, or edit files on the current node. Use 'read' to get file contents, 'write' to create or overwrite, 'edit' to make targeted replacements in an existing file.",
    "parameters": {
        "action": {
            "type": "string",
            "enum": ["read", "write", "edit"],
            "description": "The file operation to perform"
        },
        "path": {
            "type": "string",
            "description": "File path, relative to the node's workspace root or absolute"
        },
        "content": {
            "type": "string",
            "description": "File content for 'write' action, or replacement text for 'edit' action"
        },
        "old_text": {
            "type": "string",
            "description": "Exact text to find and replace for 'edit' action. Must be unique in the file."
        },
        "offset": {
            "type": "integer",
            "description": "Line number to start reading from (1-indexed, for 'read' action). Omit for full file."
        },
        "limit": {
            "type": "integer",
            "description": "Maximum number of lines to read (for 'read' action). Omit for full file."
        }
    }
}
```

**Implementation notes:**
- Path restrictions: configurable allowlist/denylist of paths per node (e.g. workspace only, or specific directories)
- `edit` action uses exact text replacement (same model as OpenClaw's `edit` tool — find unique `old_text`, replace with `content`)
- `read` truncates at a configurable max size (default 50KB / 2000 lines) with continuation support via `offset`
- Symlink resolution follows standard safe practices (no escape beyond allowed paths)

### `shell_exec`

Execute a shell command on the current node.

```json
{
    "name": "shell_exec",
    "description": "Execute a shell command on the current node. Returns stdout, stderr, and exit code. Commands run in the node's working directory.",
    "parameters": {
        "command": {
            "type": "string",
            "description": "Shell command to execute"
        },
        "timeout": {
            "type": "integer",
            "description": "Timeout in seconds (default 30). 0 for no timeout."
        },
        "working_dir": {
            "type": "string",
            "description": "Working directory override. Defaults to node's workspace root."
        }
    }
}
```

**Implementation notes:**
- Sandbox restrictions: configurable command allowlist/denylist per node
- Default deny for destructive commands (`rm -rf /`, `mkfs`, etc.) — enforce at tool level, not prompt level
- Working directory defaults to the node's configured workspace
- Timeout enforced with process kill on expiry
- Returns structured result: `{stdout, stderr, exit_code, timed_out}`

### `git` (composite)

Structured git operations. Composite tool — subcommands provide parseable results rather than raw stdout.

```json
{
    "name": "git",
    "description": "Perform git operations on the current repository. Returns structured results for reasoning, not raw shell output.",
    "parameters": {
        "action": {
            "type": "string",
            "enum": ["status", "diff", "log", "commit", "push", "pull", "branch", "checkout", "add"],
            "description": "Git operation to perform"
        },
        "args": {
            "type": "object",
            "description": "Action-specific arguments. See action docs for required fields."
        }
    }
}
```

**Action-specific args:**
- `status`: `{path?: string}` — returns `{branch, staged: [{status, path}], unstaged: [{status, path}], untracked: [path]}`
- `diff`: `{staged?: bool, path?: string}` — returns structured diff or raw unified diff text
- `log`: `{count?: int, path?: string, author?: string}` — returns `[{hash, author, date, message}]`
- `commit`: `{message: string, paths?: [string], all?: bool}` — commit with message
- `push`: `{remote?: string, branch?: string}` — push to remote
- `pull`: `{remote?: string, branch?: string}` — pull from remote
- `branch`: `{name?: string, delete?: bool, list?: bool}` — list/create/delete branches
- `checkout`: `{branch: string, create?: bool}` — switch branches
- `add`: `{paths: [string]}` — stage files

**Implementation notes:**
- Runs git commands under the hood but parses output into structured JSON
- Enforceable policies at tool level: e.g. `allow_force_push: false`, `require_commit_message: true`
- Default working directory is the node's workspace (assumed to be a git repo)

### `node_switch`

Switch the agent's execution context to a different node.

```json
{
    "name": "node_switch",
    "description": "Switch execution context to a different node. After switching, all subsequent tool calls (file, shell_exec, git, etc.) execute on the new node. Available nodes are listed in your context.",
    "parameters": {
        "node_id": {
            "type": "string",
            "description": "The ID of the target node to switch to"
        }
    }
}
```

**Implementation notes:**
- Available nodes list is injected into the LLM context by the kernel (not hardcoded in the tool description)
- Current node ID is also in context so the agent knows where it is
- Node choice is NOT session-persisted — it's per-chain, not sticky. This matches the design from the distributed node architecture vision.
- Validation: node must be reachable and authenticated before switching. Return error if node unavailable.
- On switch: subsequent tool calls in the same chain route to the new node

### `config` (composite)

Read and modify the agent's own configuration at runtime.

```json
{
    "name": "config",
    "description": "Read or update the agent's own configuration. Use 'read' to inspect current settings, 'update' to change them. Changes take effect according to the config key — some are immediate, some require a restart.",
    "parameters": {
        "action": {
            "type": "string",
            "enum": ["read", "update"],
            "description": "Read current config or update a setting"
        },
        "key": {
            "type": "string",
            "description": "Config key path (dot notation, e.g. 'agent.temperature', 'agent.reasoningEnabled'). Omit for 'read' to get all config."
        },
        "value": {
            "type": "string",
            "description": "New value for 'update' action. JSON-serialized (strings in quotes, numbers bare, booleans true/false)."
        }
    }
}
```

**Implementation notes:**
- Parameter descriptions include human-readable explanations — the LLM can understand what each setting does without reading a separate doc
- Guardrails: immutable config keys (constitution layer), restricted keys requiring elevated auth, rate-limited changes
- `read` without a key returns the full config tree (sanitized — no secrets)
- `read` with a key returns that specific value with its current state and description
- `update` validates the new value type, range, and policy before applying
- Some changes are hot-reloaded (temperature, reasoning mode), some require restart notification (model, provider)
- Audit trail: all config changes logged to mutation_audit table

## Acceptance Criteria

- [ ] `file` tool handles read/write/edit actions with path restriction enforcement
- [ ] `file` read truncates large files with offset-based continuation
- [ ] `file` edit uses unique-text replacement, errors on ambiguous matches
- [ ] `shell_exec` executes commands with timeout and structured result output
- [ ] `shell_exec` enforces command denylist at tool level
- [ ] `git` tool supports status, diff, log, commit, push, pull, branch, checkout, add actions
- [ ] `git` returns structured (parsed) results for status and log
- [ ] `git` enforces configurable policies (no force-push by default)
- [ ] `node_switch` changes execution context for subsequent tool calls in the same chain
- [ ] `node_switch` validates node availability before switching
- [ ] `node_switch` returns error for unreachable/unauthenticated nodes
- [ ] `config` read returns current config with descriptions (no secrets)
- [ ] `config` update validates type, range, and policy before applying
- [ ] `config` changes are audit-logged
- [ ] All tools registered in ToolRegistry and wired through ChainRunner tool loop
- [ ] Integration test: each tool callable through the chain execution pipeline

## Notes

- These tools are the "hands" of the agent. Without them, the agent can only talk. With them, it can act.
- `shell_exec` is the most dangerous tool here. The denylist approach is a baseline; a proper sandbox (seccomp, namespaces) is a future hardening target.
- `git` returning structured results is a deliberate design choice — parseable data beats raw text for agent reasoning. The agent can always fall back to `shell_exec git ...` for esoteric operations.
- `node_switch` is the first step toward the distributed node architecture. It's deliberately simple — just context switching, not mesh routing.
- `config` self-modification is powerful. The guardrails (immutable keys, audit logging, validation) are the minimum viable safety layer. Expect to iterate on this as we learn what the agent actually tries to change.