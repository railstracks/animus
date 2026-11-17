# Talk Channel — Design (DRAFT)

**Status:** APPROVED Nov 16 (Melvin) — frame signed off same evening. Implementation sequencing underway: step 1 (tts package v0.1.0) merged, deployed and accepted live (PR #316).
**Decisions so far (Nov 16, Melvin):** Option A (voice notes) for v1 · local Piper TTS (hosted backend later, config-selectable) · **channel-first framing: this is a *channel*, not a notetaker feature** — input/output is a mixture of speech and text, reconciled structurally. Voice: default now, per-agent choice later.
**Parked:** Option B (true in-call WebRTC audio) — separate doc, 0.6+ shape.

## 1. Frame

**One canonical transcript; modality is an attribute.** Speech and text are the same event type at the session boundary — the channel translates modality at both ends. A call and a chat are two skins over one conversation stream.

- Event shape: `{speaker, time, modality: voice|text|system, text, origin}`
- Voice inbound: STT of spoken audio → text event (voice-tagged)
- Voice outbound: utterance → TTS → voice-note share **+ text caption**; the caption *is* the transcript entry. The transcript never carries speech without text — voice is a presentation layer over the text stream, never a parallel channel.

## 2. Inbound speech (piecemeal)

Goal: segment-grain STT, not whole-call transcripts. Routes, preference order pending spike:

- **(a) Recording chunks** — `recording-chunked-upload` capability confirmed present on this instance (Nov 15 probe). Pull chunks while live, whisper each. Latency = chunk cadence. **Unverified:** chunk readability mid-call, file location, cadence.
- **(b) Rolling windows** — start/stop recording as moderator on a 60–90s timer. Every primitive proven in the notetaker lane. Clunkier (gap between windows), but this is the fallback that ships.
- **(c) Native ExApp** — implement Talk 25.0.7's `live_transcription` AppAPI surface (`GET /api/v1/languages`, `POST /api/v1/call/transcribe`) against our whisper. Biggest lift (AppAPI registration, ExApp skeleton), biggest payoff: captions for all human participants, translation support. v2 shape.

Spike (a) first; (b) is the fallback. Segment producer feeds the adapter's wake queue — an STT segment and a chat message are indistinguishable to the session.

## 3. Outbound (phrase-capped)

- `talk send` — text message into room chat.
- `talk say` — voice note: TTS render → upload → share, caption always attached.
- **Standing consent for `talk say` writes (`"writes": "standing"`, v0.6.2):** voice notes are conversational output — the same class as text chat, substituting IRC-DM / group-call participation where the user typically has no terminal. Per-invocation #126 approval would break the channel: one phrase is three writes (synthesis POST, WebDAV upload, room share), retries re-hold earlier hops (approvals are one-shot), and byte-nondeterministic TTS output rotates the upload digest. The manifest field moves consent to install time — the owner approves the package content once, script writes then pass the per-invocation hold, and a STANDING audit line (method, masked URL, digest) preserves the trail. Scope: per-command declaration, actions only (hooks lint-rejected), only meaningful inside a `writes_gated` package — everything else holds as before.
- **Phrase cap ~300 chars (≈20–25s audio).** The cap is a *backstop*: the cadence comes from the session protocol — compose in phrases, one `say` per thought. **No mechanical slicing of long text** — mid-sentence chunk breaks produce a voicemail machine, not conversation.
- **Pacing:** min-interval between phrase emissions (configurable, default ~2s) so the floor genuinely reopens between phrases. Measured Nov 16 (live service on the overlay): short phrase 0.14s, cap-length phrase 0.86s — synthesis is nowhere near latency-bound, so the gap can stay short without slowing the lane.
- **Interjection:** new inbound segment during an outbound sequence wakes the session with the remaining outbox visible → re-plan instead of finishing the monologue. Turn-taking emerges from phrase cap + wake-on-inbound; no central conversation manager.
- **v1 honest limit:** interruption granularity = one phrase. Already-shared notes cannot be recalled. True barge-in belongs to Option B.

## 4. Reconciliation

- **Ordering:** STT and chat arrive on different collectors; the transcript carries a single channel-local sequence (server timestamps + tie-break) so the session never reads a reshuffled history.
- **No cross-modality dedup:** a participant speaking and typing the same content yields two tagged events.
- One wake path for both collectors — the session cannot tell a live call from a texted room. That is the point.

## 5. Sequencing

1. **DONE Nov 16** — `tts` package 0.1.0 (PR #316 merged): service `tts` (image animus-tts:0.1.0, Swarm service on `animus-generic_default`) live; package accepted 3/3 on the real instance (synth + determinism + cap loud-fail). Output is PCM WAV, not OGG — transcode stays downstream.
2. Channel hardening — readback/history on wake (#263 dispatch pattern), agent_id routing audit, config-key allowlist; unified event stream + ordering guarantee.
3. `talk send` (text) → `talk say` (voice note: cap, pacing, caption) — standing write-consent shipped with it (0.6.2).
4. Inbound speech — chunk spike (a) → whisper-on-segments → stream integration; fallback (b) rolling windows.
5. Attribution/group — per-room agent config, caption prefix, one voice per agent.
6. (v2) `live_transcription` ExApp (c); Option B in-call audio — own doc.

## Open questions

- Chunk readability/cadence while a call is live (spike)
- Exact phrase cap (300 chars? sentence-based instead?)
- Pacing default interval
- Default Piper voice pick
- Where the segment producer lives (adapter-internal vs package-level polling)
