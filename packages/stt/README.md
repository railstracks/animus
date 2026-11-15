# stt — speech-to-text package (v0.1.0)

Transcribes audio through the internal **whisper.cpp server**: Swarm
service `whisper_stt` (stack `whisper`), reachable on the
`animus-generic_default` overlay as **`stt:8080`**. No auth — the allowlist
IS the boundary (top-level `egress_hosts: ["stt"]`; any other host,
including a rewritten `base_url`, is refused at the egress gate).

## Contract (probe-verified 2026-11-15)

- `POST /inference`, multipart `file=@<audio>` + `response_format=json`
  → `{"text": "…"}` (live receipt: JFK sample through the overlay DNS name).
- Server converts input via ffmpeg (`--convert`): wav / ogg / webm / mp3
  all accepted. Talk recordings land as ogg — no pre-conversion needed.
- Multilingual model `ggml-base.bin` (DE+EN). For higher quality later:
  swap `base` → `medium` in the stack entrypoint and raise the memory cap.

## Commands

- **transcribe `<file>` [`mime`]** — uploads `<file>` from the package
  files area (kernel-side, `ctx.http.upload`; the 1 MB Lua read cap never
  applies) and returns `{text, transcript_path}`. The transcript is also
  persisted to `<file>.txt` in the files area, so long transcripts survive
  the execute-result string cap — `data.text` is convenience for short
  clips, the file is the durable copy.

## Boundaries

- **No write gate.** Transcription is side-effect-free; `writes_gated` is
  deliberately off so meetings don't wait on approvals. The egress
  allowlist confines the package to the STT service.
- **300 s single-request timeout** (kernel clamp). Chunk hour-scale audio
  before upload — the Phase 1.5 notetaker owns that splitting.
- **`files_quota_mb: 256`** — meeting audio lives in the same quota; prune
  old recordings after transcription.

## Dependencies

- Kernel with the `ctx.http.upload` seam (PR #312, merged as `b0948db`).
- The `whisper` stack deployed on a workers node, attached to
  `animus-generic_default` (see `projects/animus-whisper/` in Kestrel's
  workspace: `stack.yml` + `PREFLIGHT.md` with deploy + smoke receipts).

## v0.1.1 — files_from cross-package reads (#69 notetaker handoff)

- `files_from: ["nextcloud"]` — declared sibling files areas. Upload parts
  accept `<pkg>/<slug>` refs (e.g. `nextcloud/talk-recording-….ogg` pulled by
  the nextcloud package's `file get`); resolution is kernel-side, deny-by-
  default, loud WARN on undeclared packages (egress-style). Bare names stay
  own-area. Requires a kernel with the files_from seam (#69).
- Transcript persistence is now loud: `ctx.fs.write` returns `(ok, err)`, the
  script fails the command on a failed persist instead of silently dropping
  the path (found live Nov 15 — root-owned files dir made the write fail
  silently). `data.transcript_file` names the persisted slug.
