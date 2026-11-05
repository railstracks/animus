# Design: Bidirectional Voice — Voice as a Regular Agent Thread

**Status:** Draft for v0.5 · Direction agreed 2026-11-05 · Pending owner red-line
**Depends on:** [Interjection & Device Events](interjection-and-events.md) · [Cortex](cortex.md) (virtual audio devices, STT/TTS services)
**Issues:** #69 (Nextcloud Talk notetaker/collaborator — STT/TTS entry point) · #162 (Nextcloud capability probe gate)

---

## Intent

Enable generation and parsing of audio, broadly: speak and listen through the
browser mic/speaker in the Animus UI webchat, join a Nextcloud Talk session, and
in the future place VoIP calls — with the agent as a full participant, not a
walkie-talkie.

The founding decision: **a voice conversation is a regular agent thread with a
different I/O surface.** Not a mode, not a special session type. Everything the
kernel already does for threads — session continuity, context cards, tools,
history — carries over unchanged. Only the transport differs.

The industry's failure mode is treating voice as a blocking request-response
loop: user speaks → STT → model → TTS → play → *only then* listen again. That
model cannot be interrupted, is deaf while speaking, and produces exactly the
disjointed conversations we want no part of. Our answers:

1. **Speech out is emitted via tool calls**, not by yielding the text body.
2. **Speech in is interjected mid-chain** — additional utterances are picked up,
   parsed to text, and inserted into the running prompt chain, so further speech
   filters in while the chain is working.

Both rest on the [interjection primitive](interjection-and-events.md).

## Layering

```
channel adapters          cortex                     agent thread
(webchat, Talk, VoIP)  →  virtual audio devices   →  normal thread:
                          audio_input  (VAD→STT)      speech IN  = interjections
                          audio_output (TTS→frames)    speech OUT = say() tool
```

- **Channels stay dumb audio pipes**, exactly as they are dumb text pipes. The
  webchat adapter carries browser mic/speaker frames; a Nextcloud Talk call or a
  VoIP leg is just another channel. Channels do no speech processing.
- **Channels register virtual audio devices with the cortex** at session/call
  start: a virtual microphone and a virtual speaker. Physical microphones and
  speakers register identically — this is how "any number of microphones,
  virtual or physical" falls out for free. The voice thread logic is then
  uniform regardless of where audio originates.
- **The cortex is the only component that touches audio frames.** STT and TTS
  are cortex services sitting on the stream archetypes
  ([Cortex → archetypes](cortex.md#three-interface-archetypes)).
- **Audio never enters the prompt.** The model sees transcripts, same as text
  chat. No multimodal context bloat; transcripts are the durable record.

## Emission contract: the `say()` tool

- **Small and frequent.** Utterances are emitted sentence-chunked, never one
  big blob. Chunking is what makes barge-in cheap: when new speech arrives, the
  next chain step cancels the remaining playback queue and emits a revised
  utterance instead.
- **Cancel and priority semantics from day one.** `say()` enqueues;
  `audio.cancel()` drops the queue. These are actuator-class commands on the
  audio_output device.
- **Turn-contract inversion.** With emission via tool call, turn completion no
  longer implies utterance completion: a turn may end with prose unplayed or
  playback outliving the chain. Two consequences the kernel must own:
  1. "Agent finished" means the *chain* finished; playback state is separate
     and observable.
  2. The transcript must reconstruct `say()` payloads as assistant speech —
     context coherence collapses if the model's own past utterances are opaque
     tool calls. Reconstruction at history-render time, not duplicate storage.

## Barge-in, end to end

```
user speaks ──► VAD → utterance event → STT ──► interjection (mid-chain)
                                                    │
agent chain, next step: sees new speech ◄────────────┘
  ├─ decides: supersede  → audio.cancel() → revised say()
  └─ decides: continue   → finish, fold input into reply
```

The loop is model-mediated by design (see interjection doc): speech never
cancels anything by itself; the agent decides. This keeps one policy brain and
avoids heuristics fighting the model.

## STT segmentation — the hidden hard problem

`audio_input` emits **utterance events, not a transcript firehose**: voice
activity detection plus end-of-utterance segmentation decide where one utterance
ends and the next begins. The buffered-STT-transcript hook is the right shape
(the cortex hook system, not a bespoke path).

Echo: on physical rigs the microphone hears the speaker. The browser path gets
WebRTC echo cancellation essentially for free; physical rigs need AEC or a
half-duplex fallback (mute input during playback — acceptable stub hack, not a
destination). One more reason the browser path ships first.

## Latency budget

Voice tolerates roughly 300–500 ms to first audio; multi-step tool chains blow
past that. The system must permit **acknowledge fast, finish slow**: a short
backchannel utterance ("looking —") while the chain completes. That is agent
policy, but it is only *possible* because emission is chunked `say()` calls —
the first chunk ships before the rest of the answer exists.

## Sequencing

1. **Webchat browser loop** (the stub below) — lowest-friction full path,
   exercises browser mic/speaker, proves interjection + emission end to end.
2. **Nextcloud Talk** as an audio-capable channel adapter — gated on #162
   (capability probe) and #69 (entry point).
3. **VoIP** — later.

## Stub scope (0.5)

Browser mic → virtual `audio_input` device → VAD/STT hook → **interjection into
a running chain** → agent reply via `say()` → TTS → browser speaker. No
Nextcloud in the stub. The kernel dependency is the interjection audit ticket.

## Non-goals (for 0.5)

- Speaker identification / voice prints
- Multi-party separation on physical rigs (who said what)
- Telephony/PSTN
- Multimodal audio-in-prompt (transcripts only, by design — listed here so the
  exclusion is a decision, not an omission)

## Open questions

- STT/TTS providers: cloud APIs vs local (whisper/piper class) — and what runs
  on ARM when the self-contained-inference endgame arrives
- Streaming TTS (synthesizing partial sentences as they render) vs chunked
  `say()` — streaming is lower-latency but complicates cancel semantics
- Voice selection & consistency per agent persona
- Utterance-event thresholds: VAD sensitivity vs. clipped speech, per-device
- Should `say()` be a kernel tool or a cortex tool surface? (Leaning cortex —
  it is an actuator-class command on audio_output — but kernel-owned ergonomics
  matter for model reliability)
