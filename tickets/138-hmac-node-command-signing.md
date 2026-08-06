# Ticket 138: HMAC-SHA256 Command Signing for Node Communication

**Created:** 2026-08-06
**Status:** Open
**Branch:** `dev`

## Problem

Inter-node commands flow over a WebSocket connection that is authenticated at connect time (token-based), but individual commands are unsigned. A compromised intermediary, MITM, or session hijack could inject arbitrary tool calls (`exec`, `file`, etc.) into the command stream. There is also no replay protection — a captured command could be re-sent.

## Goal

Sign every command message with HMAC-SHA256 using a per-node shared secret. The node verifies the HMAC before executing any tool call. Timestamp + nonce prevent replay attacks.

## Design

### Shared Secret

At token generation time (`POST /api/v1/nodes/tokens`), derive a signing key alongside the auth token. Store the signing key hash (same pattern as token storage — never store plaintext). The plaintext signing key is returned once at creation time and given to the node operator.

```
signing_key = random_bytes(32)   // 256-bit key
signing_key_hash = SHA256(signing_key)  // stored server-side
```

Node config gains a `signing_key` field:

```
struct NodeDaemonConfig {
    std::string serverUrl;
    std::string token;           // existing — connection auth
    std::string signingKey;      // new — HMAC signing key
    std::string name;
    std::vector<std::string> allowedTools;
};
```

### Message Format

Every `tool_call` message from server to node includes:

```json
{
  "type": "tool_call",
  "call_id": "...",
  "tool": "exec",
  "arguments": { ... },
  "timestamp": 1786000000,
  "nonce": "a1b2c3d4e5f6...",
  "hmac": "sha256=<hex digest>"
}
```

### HMAC Computation

Canonical message string (deterministic ordering):

```
sha256_hmac(key, type + "|" + call_id + "|" + tool + "|" + canonical_json(arguments) + "|" + timestamp + "|" + nonce)
```

- `canonical_json(arguments)` — JSON with sorted keys, no whitespace
- `timestamp` — Unix seconds, UTC
- `nonce` — 16+ bytes of hex-encoded randomness, unique per command

### Verification (Node-side)

On receiving a `tool_call`:

1. **Check timestamp** — reject if `|now - timestamp| > 60` seconds (clock skew tolerance)
2. **Check nonce** — reject if nonce was seen before (sliding window of recent nonces, e.g. last 10,000)
3. **Compute HMAC** — recompute using the shared key over the same canonical string
4. **Compare** — constant-time comparison. Reject if mismatch.
5. **Execute** — only if all checks pass

If verification fails, log the rejection with reason and send an error result back. Do NOT execute.

### Nonce Tracking

Node maintains a `std::unordered_set<std::string>` of recently seen nonces. Prune entries older than the timestamp window (60s + margin). Bounded memory — at most ~60s worth of nonces.

### Backwards Compatibility

Nodes without a `signing_key` in config operate in legacy mode (no verification). Server detects whether the node registered with HMAC support (via registration handshake) and only sends signed commands to nodes that can verify them. This allows rolling deployment without breaking existing nodes.

### Registration Handshake

Node registration message gains a `capabilities` array:

```json
{
  "type": "register",
  "name": "...",
  "hostname": "...",
  "os": "...",
  "tools": [...],
  "capabilities": ["hmac-v1"]
}
```

Server checks for `"hmac-v1"` in capabilities. If present, commands to this node include HMAC fields. If absent, commands are sent unsigned (legacy mode).

## Files Affected

### Backend (C++)

| File | Change |
|------|--------|
| `include/animus_kernel/NodeDaemon.h` | Add `signingKey` to `NodeDaemonConfig` |
| `src/kernel/NodeDaemon.cpp` | HMAC verification in `HandleToolCall`, nonce tracking, timestamp check, capabilities in registration |
| `src/kernel/admin/internal/AdminServerNodeWebSocket.inc` | Detect `hmac-v1` capability, flag connection as HMAC-enabled |
| `src/kernel/admin/internal/AdminServerRoutesNodes.inc` | Generate signing key at token creation, return to user, store hash |
| `src/kernel/admin/NodeManager.*` | Store/validate signing key hash alongside token hash |
| `src/kernel/admin/NodeCommandDispatcher.*` (or equivalent) | Sign commands with HMAC when sending to HMAC-capable nodes |

### Crypto

HMAC-SHA256 via OpenSSL (already linked — used for existing token hashing) or a lightweight single-header implementation. No new external dependency.

### Frontend

Node token creation response shows the signing key once (same pattern as auth token). Node config UI gains a `signing_key` field.

### CLI

`--node-signing-key` command-line argument for `animusd --node` mode.

## Testing

1. Generate token + signing key via API
2. Connect node with both token and signing key
3. Send signed command — verify executes
4. Send unsigned command to HMAC-capable node — verify rejected
5. Send tampered command (modified arguments) — verify HMAC mismatch rejects
6. Send replay (same nonce) — verify rejected
7. Send stale command (old timestamp) — verify rejected
8. Connect node without signing key — verify legacy mode works
