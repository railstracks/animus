# Design: Tier 1 Calls — Live Talk Calls as a Channel

**Status:** Draft · Tier ruled by owner 2026-11-19 · Pending owner red-line
**Decision record:** [#325 ruling — Tier 1, headless WebRTC join](https://github.com/railstracks/animus/issues/325#issuecomment-7008084992)
**Depends on:** [Interjection & Device Events](interjection-and-events.md) · [Voice](voice.md) — consumes both docs' vocabulary · [Talk channel](talk-channel.md) — the chat/voice-note lane, unchanged by this doc

## Intent

Agent seats join live Nextcloud Talk calls as true participants: they hear the
call (per-participant audio → STT → utterance events), speak into it (TTS →
Opus → published track), and can be interrupted mid-utterance (barge-in). This
is [voice.md](voice.md)'s sequenced step 2 — "a Nextcloud Talk call is just
another channel" — realized on the live infrastructure. It is a *channel*, not
a notetaker feature; per talk-channel.md's frame, call segments are events in
the one canonical transcript, `modality: voice`, `origin: call`.

What Tier 1 explicitly buys over the shipped voice-note lane (talk-channel.md
§3): true full-duplex conversational participation — speech in *while* the
agent speaks, phrase-grain interruption (voice notes cannot be recalled or
overlapped), and per-speaker attribution from the SFU.

## Audit — what exists (probed 2026-11-19)

| Surface | State | Classification |
|---|---|---|
| Standalone signaling server | **Live, authoritative** (`signaling_servers` → the production signaling endpoint, welcome 200 outside *and* inside seat containers at ~21 ms) | **Keep.** Client speaks only the documented v1 protocol; no internal-signaling fallback needed |
| Janus SFU behind signaling | Running (AIO talk stack); protocol docs confirm **all gateway details are hidden from clients** — signaling relays everything, only media goes direct | **Keep.** Per-participant forwarding preserves speaker attribution |
| TURN/STUN | Configured (`turn_servers`/`stun_servers` in spreed appconfig; 3478 tcp/udp published) | **Keep.** NAT story has infrastructure; remaining question is container vantage, resolved by the Phase 0 spike |
| Recording backend lane | Container live; moderator-gate ruled Nov 15 (service account promoted) | **Complementary, unchanged.** Replay/captions; not this doc's path |
| `packages/nextcloud` v0.6.3 | Chat + voice-note lane (`talk send` / `talk say`), standing writes, captions | **Keep as the async/chat lane.** Calls do **not** extend the Lua package: a WebRTC media plane cannot live in Lua |
| whisper_stt + tts services | Swarm services, proven (STT 17 s audio at ~3000× realtime; TTS 0.14–0.86 s/phrase) | **Keep** as the STT/TTS engines — these are voice.md's "local by default" providers |
| Interjection primitive | Design decided 2026-11-06 (message-thread framing, next-step visibility); kernel audit ticket #1 to confirm adapters already interject | **Consume.** Call segments ride the same path; no new primitive |
| voice.md contracts | `say()` emission with enforced phrase cap, `audio.cancel()`, model-mediated barge-in, utterance events (not transcript firehose), audio never in prompt | **Consume verbatim** for the call lane |
| Cortex virtual audio devices | Designed (voice.md/cortex.md), **not yet implemented** | **Gap.** Open question 1 stages how to cross it |

## Architecture

The call lane is a kernel-native, audio-capable channel adapter — voice.md's
layering with one honest deviation (open question 1): until the cortex exists,
the audio pipeline lives adapter-internally behind interfaces shaped so the
cortex can adopt it unchanged later.

**Inbound (hear):** libdatachannel receives per-participant Opus tracks →
decode (libopus) → resample 48 k→16 k mono → VAD utterance segmentation →
chunked POST to whisper_stt → utterance event `{speaker, text}` → the existing
message-thread path. A call segment and a chat message are indistinguishable
to the session — talk-channel.md §4's one-wake-path principle, extended.

**Outbound (speak):** the session emits via a `say()`-class tool (never text
yield — the #328 discipline) → tts service renders → Opus encode @48 k →
audio track published to the SFU. Phrase cap and pacing carry over from
talk-channel.md §3; `audio.cancel()` drops the playback queue per voice.md.

**Barge-in:** model-mediated per voice.md — inbound segments interject
mid-chain; the next step decides supersede (`audio.cancel()` + revised `say()`)
or continue. One structural exception: the agent never transcribes its own
playback (never-subscribe-own-publisher + VAD hold while transmitting) —
echo gating is kernel-level, not model-mediated. This is the audio cousin of
the room-26 ping-pong lesson (Nov 19): loops gate structurally.

**Lifecycle:** ring → join → attend → leave. Tool verbs `call.join`,
`call.leave`, `call.status`; ring events wake the session like any inbound.
One live call per seat in v1.

**Media plane:** libdatachannel (WebRTC without libwebrtc), libopus, a small
VAD. Isolated audio thread with queue handoff — 20 ms pacing never rides the
drogor event loop. ARM compatibility carries over from voice.md's provider
decision.

## Protocol shape (client side — Phase 0 leg 1 PROVEN Nov 19)

Validated against production (deployed signaling 2.1.1) by the Phase 0 spike:
welcome → hello (internal client type: `auth: {type: "internal", params:
{random, token: HMAC-SHA256(random, internalsecret), backend: <NC url>}}`)
→ room join `{roomid, sessionid?}` → join/participants events → bye.
The server runs the full feature set (`mcu`, `hello-v2`, `simulcast`,
`virtual-sessions`, …); janus (SFU) is internal to the talk container
(`ws://127.0.0.1:8188`) and hidden from clients; media is DTLS-SRTP direct
to the gateway or TURN-relayed (3478).

Findings that reshaped the plan:
- **Talk 25 removed the v1 OCS signaling-settings route** (998 on v1, 404 on
  bare v2/v3; the browser gets settings server-rendered). Ticket hello v1.0
  as a named client is therefore not directly reachable headless.
- **Internal-client auth is the service path** and works today — but carries
  no NC user identity (`userid: ""` in join events): fine for the spike and
  backend services, wrong for production seats that must appear as themselves.
- **Production auth = hello v2.0 JWT** (ES256 keypair already in appconfig;
  `hello-v2` in server features). The token-minting surface for headless
  clients is open question 9.

`bye`/resume for graceful leave and reconnect.

**Leg 2 (two-party media) PROVEN Nov 19** — live call with the owner:
`requestoffer` → offer → answer → candidates completed in ~5 s; the active
media path was **TURN relay** (eturnal, REST credential scheme:
username=unix-expiry, password=base64(HMAC-SHA1(secret, username))) — the
selected pair was `typ relay` through the published 3478; iceConnectionState
`completed`. Audio arrives as **Opus 48 kHz stereo** (3,840 bytes per 20 ms
frame = 2 ch × 960 × 2): naive flatten-as-mono halves playback speed and
whisper hallucinates `[Music]` on the slowed audio; deinterleave-to-mono
first yields verbatim transcription of the owner's speech (78.4 s true-
duration capture → whisper_stt word-accurate). The internal client appears
in the browser participant list (session-visible, empty userid — the owner
read it aloud as "the spike call"), reinforcing the identity finding above.

**Leg 3 (inbound pipeline unit, offline replay) PROVEN Nov 19** — the leg-2
capture replayed through the Phase-1 inbound shape (48k→16k resample →
webrtc VAD → bounded segmentation → chunked whisper_stt):
- webrtcvad aggressiveness 1 and 2 produced identical boundaries on real
  speech; 3 shredded sentences (31.7% voiced, 11 fragments). **Use 2.**
- The 0.4/1.2 s bounds behave; the 15 s hard-max split is time-blind — it
  cut mid-sentence ("…reporting that now to on | the finalization…").
  **Ref:** split at the last unvoiced gap inside the window, and coalesce
  consecutive chunk events at the thread boundary.
- Chunk vs whole-file STT: one sentence garbled at 15 s grain that the 78 s
  whole-file pass had correct — context helps; keep chunks at the full
  hard-max and treat split boundaries as suspect.
- **VAD passes music** (TV tail segmented 4/4 as speech). whisper self-labeled
  3 of 4 music chunks `[BLANK_AUDIO]` but hallucinated a 3-word phrase on
  the fourth — tag-matching alone is not a junk filter. **Rec:** require a
  minimum voiced-frame ratio per event (the VAD already ran — the ratio is
  free), drop tagged/empty texts.
- Latency: 0.61–0.87 s per ≤15 s chunk → utterance events land ~2 s after
  speech ends (1.2 s end-silence + STT). Phase 1 cadence budget holds.

## Phases

- **Phase 0 — spike (throwaway client, no kernel changes).** Exit: solo
  hello+room join on the test room; two-party call with the owner → PCM on
  disk, speaker-attributed; media path documented. Findings fold into this doc.
  **Status: PROVEN Nov 19, both legs + leg 3** — solo control plane, two-party
  media (owner's live speech transcribed verbatim), and the resample→VAD→
  chunked-STT inbound pipeline on real capture; see protocol section.
- **Phase 1 — listen-only adapter.** Join on tool call / owner invite;
  per-participant decode+VAD+STT; utterance events land in the thread;
  `call.status`; post-call summary via the existing chat lane. Exit: agent
  attends a live call and the owner sees live acknowledgment + after-call
  summary.
- **Phase 2 — speak.** `say()`-into-call, phrase cap, `audio.cancel()`.
  Exit: owner asks aloud mid-call, agent answers aloud; a mid-answer
  interjection measurably changes what is spoken next.
- **Phase 3 — cadence & etiquette.** Digest-bounded wakes for long calls,
  backchannel policy ("acknowledge fast, finish slow"), echo gates under
  full-duplex, config knobs. Exit: 30+ min attended call without ping-pong
  or context blowout. Shares machinery with #295 (interjection digests).

## Test plan (maps to #325 acceptance)

- **Unit:** decode→resample→VAD segmentation; Opus frame pacing; echo gate
  (self-publisher filter + transmit-hold).
- **Integration:** solo join (publish leg); two-seat loopback (subscribe leg);
  reconnect (resume) mid-call.
- **End-to-end (the acceptance):** live two-party call, owner + one seat —
  both directions audible and speaker-attributed; barge-in demonstrated;
  call lane produces no room-text spam.

## Ticket sketch (cut after red-line)

1. Kernel deps: libdatachannel + libopus + VAD in CMake (ARM note applies)
2. Signaling client: WS, hello/resume/bye, room, message relay, reconnect
3. Call adapter + lifecycle verbs (`call.join` / `call.leave` / `call.status`)
4. Audio-in pipeline → utterance events → thread path (incl. interjection audit #1 confirmation)
5. Audio-out pipeline: `say()` → tts → Opus → publish; `audio.cancel()`
6. Cadence config + digest wakes (shared with #295)
7. Echo gates (never-subscribe-self, transmit-hold)

## Threat model & honest boundaries

- Media plane is new attack surface: DTLS-SRTP only (libdatachannel), no raw
  sockets; signaling auth rides the seat's existing app-password credentials.
- Call audio is ephemeral: transcripts are the record (audio never enters the
  prompt — voice.md's exclusion, held). VAD buffers are ring-sized; Phase 0
  captures are deleted after analysis.
- whisper/tts stay swarm-internal (not publicly routable); the call client's
  egress is signaling (443/WSS) + media to the gateway/TURN only.
- Honest limit carried from talk-channel.md §3 and *lifted only partially*:
  phrase-grain interruption arrives in Phase 2; sample-grain barge-in
  (stopping mid-phrase) remains out of scope for v1.

## Decisions (red-lined by the owner, 2026-11-20)

1. **Pipeline placement: adapter-native.** The pipeline lives in its own
   component behind cortex-shaped `audio_input`/`audio_output` seams; audio
   state never leaks into signaling code. When the cortex lands, the same
   pipeline migrates behind its devices unchanged — a move, not a rewrite.
   The adapter is the first device host.
2. **Join policy (a config setting).** 0.5 default: auto-accept
   invites/rings from the owner's account; `call.join` tool-call for
   anything else; no self-initiated calls. Agent-arranged/invited calls
   arrive post-0.5 as settings on the same knob, not new architecture.
3. **VAD bounds as recommended** (min 0.4 s; end silence 1.2 s; hard max
   15 s, split at the last unvoiced gap). Defaults, config-exposed for
   later tuning.
4. **Barge-in: model-mediated policy, structural echo gate.** The
   never-hear-oneself gate stays kernel-structural; no heuristic mid-TTS
   kill.
5. **Interjection granularity: per-utterance thread messages for v1.**
   Tuning deliberately deferred until a functioning call exists; digest
   coalescing remains Phase 3 config.
6. **Codec/transport as recommended** (Opus VOIP mode, 48 kHz mono, 20 ms
   frames; direct UDP preferred, TURN fallback — media path already proven
   both ways).
7. **Test room & credentials: workspace-internal.** A dedicated sacrifice
   room per environment; seat identity via the vaulted `auth_header`
   state (admin API read, never logged). Workspace specifics (room IDs,
   hosts) do not belong in the repo.
8. **Mixed versions: feature-flagged adapter.** Older images keep the
   chat + voice-note lane untouched; no manifest break.
9. **Seat authentication: hello-v2 JWTs for production seats.** Minting
   route pinned during Phase 1 (occ or an authenticated OCS call against
   Talk's SignalingController); internal auth reserved for infrastructure
   services (recording-class). **0.5.0 acceptance pair: the owner plus a
   seat instance** — the seat joins a live call as itself and audio flows
   into its session via the existing thread path.

## Migration

Nothing is replaced. This doc adds the call lane; talk-channel.md's
voice-note path remains the async fallback and the chat-view UX. Seats opt
into calls per instance config; the transcript sees call segments as ordinary
voice-modality events from day one.
