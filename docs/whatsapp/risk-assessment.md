# WhatsApp Integration — Risk Assessment

**Status: BLOCKED.** This assessment must be resolved before committing to implementation.

## The Core Problem

WhatsApp's Terms of Service **explicitly prohibit** automated clients. The multi-device protocol we're implementing is not a public API — it's the private protocol used by WhatsApp's own web client, reverse-engineered by the open-source community.

This is not a legal concern. This is a **user safety** concern.

## Risk Categories

### 1. Account Bans

**What happens:** WhatsApp detects unauthorized client usage and bans the phone number.

**Impact:** The user loses their WhatsApp account. Their phone number is flagged. They cannot register a new WhatsApp account on that number for some period (reports vary from hours to permanent).

**Who's affected:** The end user, NOT us. This is the critical asymmetry — we build the tool, but the user takes the risk.

**Severity:** **HIGH** — but primarily driven by usage patterns, not protocol fingerprinting.

**Evidence from Baileys:** Baileys has thrived for years without any anti-fingerprinting code. It passes a static browser identity string and uses fixed intervals. Community ban reports consistently point to message volume, cold outbound, and user reports — not protocol-level detection. This is relevant context for our risk assessment: the primary ban mechanism appears to be behavioral, not technical. Since we're reimplementing the protocol from scratch (not wrapping Baileys), their fingerprinting posture is informative rather than determinative — our risk depends on how accurately we replicate the wire format, which is independent.

### 2. Detection Vectors

WhatsApp has multiple ways to detect unofficial clients:

| Vector | Description | Mitigation Difficulty |
|--------|-------------|----------------------|
| **Client fingerprinting** | WhatsApp's official clients have specific behavioral fingerprints (timing, message ordering, feature usage patterns) | Low — Baileys has operated for years without any anti-fingerprinting measures (static browser string, fixed intervals, no jitter). Ban reports are consistently about usage patterns (volume, cold outbound), not protocol-level fingerprinting. This suggests WhatsApp does not aggressively detect unofficial clients via fingerprinting. |
| **Protocol compliance** | Subtle differences in binary node encoding, protobuf field ordering, or encryption padding | Low — if we accurately replicate the protocol |
| **Connection patterns** | Uptime patterns (24/7 connections are unusual for human users), ping timing, reconnection behavior | Medium — we can add human-like jitter |
| **Registration patterns** | Same phone number repeatedly linking new companion devices | High — we can't control this |
| **Server-side analytics** | Meta has sophisticated bot detection. Rate of messages, response patterns, content analysis | Medium-High — depends on usage pattern |
| **Pre-key upload patterns** | Unofficial clients may upload pre-keys differently than official clients | Low — well-documented in Baileys |
| **Missing features** | Not implementing certain protocol features (presence, typing, read receipts) that official clients always send | Medium — we can implement the expected feature set |

### 3. Cat-and-Mouse Maintenance

**What happens:** WhatsApp updates their protocol. Baileys breaks. We need to update our C++ implementation to match.

**Historical precedent:**
- Baileys has been through multiple major protocol changes
- WhatsApp has actively broken unofficial clients in the past
- The multi-device protocol itself was a major migration from the legacy protocol
- Protocol changes can be subtle (token dictionary updates, new protobuf fields) or fundamental (encryption changes)

**Maintenance burden:** Ongoing, unpredictable, potentially urgent. Melvin explicitly stated he does **NOT** want this to become a dayjob.

**For a C++ implementation:** The burden is higher than for Baileys users. When Baileys updates, they handle the TypeScript changes. We'd need to independently replicate those changes in C++.

### 4. Legal Risk

**Assessment:** **LOW.** WhatsApp/Meta has not pursued legal action against individual users of unofficial clients or the developers of libraries like Baileys. The primary enforcement mechanism is account bans, not lawsuits.

However:
- This could change, especially for commercial use
- If Animus is used by businesses, the calculus changes
- EU regulation (Digital Services Act) may affect this landscape

## Mitigation Options

### Option A: Accept the Risk, Document Clearly

- Implement the full protocol
- Prominently warn users about ban risk
- Recommend using a dedicated phone number (not their primary one)
- Provide documentation on what to do if banned

**Pros:** Full functionality, no dependency on third party
**Cons:** Users still bear the risk. Cat-and-mouse maintenance.

### Option B: Use Official Business API Instead

- WhatsApp Cloud API (official, REST-based)
- Requires Meta Business account + verified phone number
- Per-message pricing ($0.01-0.09 per conversation depending on region)
- No ban risk — it's the sanctioned path

**Pros:** Zero ban risk, stable API, supported by Meta
**Cons:** Requires business registration, costs money per message, phone number must be verified, not suitable for self-hosted multi-tenant without each tenant having their own business account

**Verdict:** This is what Melvin explicitly rejected. The Business API has too many gatekeeping requirements for an agent framework.

### Option C: Hybrid — Baileys Bridge

- Run Baileys (Node.js) as a sidecar process
- Animus communicates with it via IPC or HTTP
- Protocol updates are handled by updating Baileys version

**Pros:** Less maintenance burden — Baileys community handles protocol changes
**Cons:** Introduces Node.js dependency (which Melvin explicitly rejected for the main daemon), adds a process to manage, IPC latency, potential memory overhead

**Verdict:** Melvin rejected this approach during scoping. He wants a pure C++ implementation to keep the stack minimal.

### Option D: Wait and Watch

- Don't implement WhatsApp yet
- Monitor the Baileys community for ban reports and detection changes
- Implement only when the risk profile improves or the community has better mitigations

**Pros:** Zero risk, zero maintenance
**Cons:** Delays feature, may never become "safe enough"

## Recommendations

### Before Implementation

1. **Survey Baileys community** — Check GitHub issues, Discord, and Telegram groups for recent ban reports. Is detection getting better or worse?
2. **Test with a throwaway number** — Set up a dedicated SIM with a number we're willing to lose. Run Baileys for 2-4 weeks with realistic usage patterns. See if it gets banned.
3. **Assess fingerprinting depth** — Read recent Baileys commits related to client fingerprinting. Are they actively evading detection? How sophisticated is WhatsApp's detection?
4. **Consider the target audience** — If Animus is primarily used by Melvin and Thomas (tech-savvy, willing to accept risk), the calculus is different than if it's meant for non-technical end users.

### If Proceeding

1. **Separate phone numbers** — Never connect Animus to a user's primary WhatsApp number. Always use a dedicated number.
2. **Behavioral jitter** — Add random delays, human-like response timing, connection patterns. Don't be a perfect bot.
3. **Feature parity** — Implement presence updates, typing indicators, read receipts. Missing features are a detection signal.
4. **Version tracking** — Pin to a specific Baileys version and audit their changelog weekly.
5. **Graceful degradation** — If banned, detect it quickly and notify the admin. Don't silently fail.

### Decision Matrix

| Condition | Recommendation |
|-----------|----------------|
| Baileys community reports widespread bans | **Do not implement** |
| Baileys community reports rare/occasional bans | **Proceed with dedicated number only** |
| Baileys community reports no recent bans | **Proceed with clear user warnings** |
| WhatsApp releases official companion device API | **Switch to official API immediately** |
| Detection becomes too aggressive | **Deprecate and remove** |

## Open Questions

1. **How aggressive is WhatsApp's detection in 2026?** This needs current data, not historical assumptions.
2. **Is the risk acceptable for Kestrel's specific use case?** (single user, dedicated number, low message volume)
3. **What's the fallback if banned mid-use?** Can we recover gracefully, or does the user lose all conversation history?
4. **Should we invest in the C++ implementation if the risk might make it unusable?** Opportunity cost vs. other adapters.

---

**Decision needed from Melvin before proceeding.**
