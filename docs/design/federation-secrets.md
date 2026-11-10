# Federation Secrets — Encrypted Replication with Per-Node Envelope Keys

**Issue:** #182 (P3b) · **Epic:** #78 · **Pairs with:** #181 (P3a)
**Status:** Decided core (owner, 2026-11-06) — draft for red-line
**Precedes:** #181 implementation lane

---

## The problem

Federation moves rows between peer databases as plain data: SyncStore outbox → LWW pull via the shared `sync_token`. Provider API keys, channel credentials, and vault entries must not ride that channel under a key every member holds — the sync token authorizes the transport; it does not make peer databases a safe resting place for every member's credentials.

## Current state — the #93 stopgap this design replaces

This is not a greenfield addition. The audit found the vault table **already in the replication set** (`IdRanges.cpp`, #93 P3 comment):

- `api_package_secrets` (the #23 SecretsVault table) and `agent_config` both sync today via the established outbox/LWW path.
- Vault ciphertext therefore already rides the sync channel — sealed with AES-256-GCM under the vault's master key (`api-vault.key`, dataDir, auto-generated, 0600).
- That master key is distributed **manually** to verified nodes — decided on #93 as "for now."

A manually-distributed master key is a **de-facto shared cluster key**: every member that can pull the outbox and holds the master key can decrypt the entire fleet's secret set, and revocation is impossible without rotating the key on every node simultaneously. This is exactly the blast radius the owner ruled out on #182. The stopgap made sense for two-node, one-operator networks; the federation arc (and the node-expanding 0.5 horizon) outgrows it.

## Decisions already on record (owner, 2026-11-06)

1. **Encrypted replication with per-node envelope keys.** Secrets replicate sealed to each approved peer's key; join = encrypt the secret set to the new node, leave/revoke = re-key without it.
2. **No shared cluster key** — one compromised node must not decrypt the fleet.
3. **No owner-master-key** — the owner must not become a provisioning dependency for membership changes.
4. **v1 may defer auto-re-key UX** (owner-triggered re-encrypt is acceptable) — but the envelope format must not assume manual re-key.
5. **Build on the existing vault** — extend SecretsVault, never a parallel mechanism.

## Design: network-epoch DEK, wrapped per node

Classic envelope encryption, sized to the existing primitives:

- **Node identity key (KEK):** each node generates an X25519 keypair at first boot; private key never leaves the node (`federation-node.key` in dataDir, 0600, same load discipline as `api-vault.key` — no symlinks, regular-file check on the verified descriptor). The public key is registered with peers as part of peer setup.
- **Epoch DEK:** one random 32-byte data-encryption-key per network per epoch. All vault entries seal under it via the existing AES-256-GCM path.
- **Key envelopes:** the DEK is wrapped to each approved member's public key — ephemeral X25519 → HKDF-SHA256 (derivable from the in-tree `HmacSha256Hex`) → AES-256-GCM, with AAD binding `{network, epoch, target_node, source_node}`. Envelopes live in a new `federation_key_envelopes` table (epoch, target_node, ephemeral_pub, nonce, ciphertext) which itself syncs — a node can only unwrap the envelope addressed to its own key.

Why DEK-wrapping rather than sealing every secret to every member individually: a secret write stays one ciphertext (the LWW row stays single and compact — the sync set's row identity and anti-entropy digests are untouched), join/rotate ceremonies touch only the small envelope table, and revocation is a bounded sweep.

### Paths

- **Write path (sealing node):** operator sets a secret on any member → `SecretsVault::Set` seals under the current epoch DEK → trigger lands the row in the outbox as today. No new write-path instrumentation.
- **Sync path:** unchanged LWW pull. Ciphertext and envelopes move as data; no node ever sees another member's plaintext.
- **Boot path:** node loads its private key → finds its envelope for the current epoch → unwraps the DEK → the vault's open path uses it. Missing envelope (new node, pre-approval, or revoked post-rotation) → federated entries present but unreadable; loud log + admin status "awaiting key ceremony" / "locked out of epoch N". Local non-federated behavior degrades gracefully, never silently.

### Join, leave, rotate (v1: owner-triggered, per decision 4)

- **Join:** new node registers (URL + sync token + public key) → an existing member seals the current-epoch DEK to the new public key → envelope row syncs → new node unwraps at next boot and reads the full secret history it pulled.
- **Leave/revoke:** owner triggers re-key on any member → generate epoch N+1 → re-seal every vault entry under it (local sweep, outbox pushes new ciphertexts) → seal N+1 only to remaining members' keys. The revoked node keeps old envelopes and old ciphertexts, both useless for epoch N+1. Honest boundary: **re-key restores exclusivity going forward — it does not un-leak anything the revoked node already read.**
- **DEK rotation cadence:** on membership change (v1 manual), never on a timer.

## Threat model (issue acceptance: what the sync_token holder can and cannot do)

| Attacker | Today (#93 stopgap) | This design |
|---|---|---|
| `sync_token` holder, non-member | Pulls ciphertext; **with the manually-distributed master key: full plaintext of the fleet's secrets** | Pulls ciphertext + key envelopes; **cannot decrypt — no node private key** |
| Peer DB compromise (backup/dump exfiltration) | Ciphertext only — vault's existing DB-only-exfiltration protection holds | Same: ciphertext only; the private key file is outside the DB |
| Full-host compromise of a member | All secrets (master key on host) | **All secrets the network holds** — member private key + epoch DEK are on host. Honest boundary: per-node keys shrink the blast radius from compromised peer *databases*, not from compromised *members*. A member is trusted with the fleet's secrets by definition of encrypted replication — the alternative is option 1 (no replication), rejected. |
| Compromised node, discovered later | Nothing revocable short of fleet-wide master rotation breaking every node | **Revocable**: epoch rotation cuts it off; remaining members unaffected |
| Malicious LWW writes (token holder pushes poisoned rows) | Unchanged | Unchanged — **transport auth is out of scope here**; the token authorizes the channel, per-node keys protect the payload. Worth its own ticket (per-peer tokens) — open question 4. |

## Mixed-version behavior

Existing policy applies: unknown table types and unknown `enc_version` values are skipped (defer, don't guess). Pre-envelope kernels see envelope rows they don't know and leave them alone; new kernels see old-format ciphertext and defer. The ciphertext format gains a version prefix so opens fail classified (`"unsupported envelope version"`) rather than as garbage AES-GCM failures.

## Migration from the manual master key

Admin-invoked, one-time, loud (matches the owner-triggered philosophy):

1. Node generates its identity keypair; peers exchange public keys via the peer-registration surface.
2. Admin triggers migration on any node holding the manual master key: generate epoch 1 DEK → unwrap-and-re-seal every vault entry under it → seal the DEK to every registered member → push via outbox.
3. Members unwrap epoch 1 at next boot. The manual `api-vault.key` becomes **retired for federated entries** (its file can be removed once every member confirms epoch 1).

No flag day: unmigrated members keep reading the old master-key path until they migrate; the version prefix discriminates.

## Test plan (from #182 acceptance)

- **Replication round-trip:** seal on A → pull on B → B opens (epoch DEK unwrapped via its own envelope).
- **Revocation propagation:** C leaves → epoch rotates → C's stored envelopes fail against epoch N+1 ciphertext while B stays green.
- **Negative — no plaintext leak:** known plaintext secret sealed on A → grep every outbox payload and sync-transport body for the plaintext string → absent everywhere.
- **Join:** D added post-rotation → receives epoch N+1 envelope → reads full history.
- **Mixed-version:** v1 kernel skips envelope rows; v2 kernel defers on unversioned ciphertext.
- **Boot without envelope:** new node with secrets pulled but no envelope → loud lockout, no silent empty-string resolution (the vault's existing missing-key hard-error discipline extends).

## Implementation sketch (tickets cut from this)

1. **X25519 into `CryptoUtils`** — the primitive already lives in-tree (Signal stack, `SignalCrypto.cpp`); lift/extract rather than vendoring libsodium. Plus HKDF-SHA256 over the existing HMAC.
2. **Node identity keypair** — generation, secure-load (mirror `api-vault.key` discipline), admin surface for public-key exchange.
3. **Epoch store + envelopes** — `federation_key_envelopes` table, sync-set registration (IdRanges), wrap/unwrap, boot-path integration into `SecretsVault`.
4. **Version-prefixed ciphertext + migration sweep** — re-seal under epoch 1, retire the manual master key.
5. **Admin ceremonies** — approve-node (seal DEK to new key), revoke-node (rotate), status (epoch, which members hold it). v1 owner-triggered only.

## Open questions (red-line needed)

1. **X25519 sourcing** — lift from the Signal stack vs. vendor libsodium. I recommend lifting: in-tree, already reviewed, no new dependency; cost is a careful extraction PR.
2. **Node private key at rest** — raw 0600 file (matches `api-vault.key`; simple; full-host compromise reads it) vs. passphrase-wrapped at boot (harder against full-host, adds an operator secret + unlock step to every boot). I recommend raw v1, passphrase as a later option. Note the tension honestly: the member-compromise row in the threat model is only improved by this, and it's the row we can't otherwise shrink.
3. **Per-entry replication policy** — today all vault entries replicate; a `local_only` flag for node-local secrets (e.g. credentials for node-local services) is a natural follow-up. Defer to its own ticket, or fold into ticket 3? I recommend its own ticket.
4. **Per-peer transport tokens** — the sync token is fleet-shared today; a hostile holder can poison LWW rows regardless of this design. Worth a ticket now or parked until a second operator joins the network? I recommend parked — single-operator networks gain nothing from it.
5. **Per-agent DEKs** — finer revocation (compromised node leaks only one agent's secrets) at N×envelope cost. I recommend network-epoch v1 and revisiting if multi-tenant fleets emerge.

## Relations

- Replaces the #93 manual key-distribution stopgap (explicit "for now" in `IdRanges.cpp`).
- #181 (P3a) rides the same replication machinery its mechanics already share; its remaining acceptance surface (trigger capture, digest heal, version-skip tests) is unaffected by this design — the vault table simply keeps syncing, now under epoch-sealed ciphertext.
- The 0.5 cortex/node horizon multiplies node count by design; per-node keys are what keeps that expansion from multiplying the blast radius.
