# Ticket 108 — Audio Tool (Speech-to-Text + Text-to-Speech)

**Created:** 2026-07-08
**Status:** Draft
**Priority:** P2
**Depends on:** Provider system (existing), Tool framework (existing), ticket 068 pattern (image tool)

## Summary

Add a built-in Animus tool for speech-to-text (transcription) and text-to-speech (synthesis). Presented to the agent as a single unified `audio` tool with an `action` parameter (`transcribe` | `synthesize`). Internally, these are completely separate code paths sharing only the tool interface — same pattern as ticket 068 (Image Tool).

## Motivation

- **Nextcloud Talk Phase 2 (call stenographer)** requires STT to transcribe meeting recordings
- **Voice-enabled agents** — agents that can speak responses and understand voice messages
- **Accessibility** — TTS for visually impaired users, STT for hands-free interaction
- **Podcast/audio content ingestion** — agents can transcribe and reason about audio content
- **Multi-modal agent communication** — voice messages from WhatsApp/Telegram can be transcribed and answered

## Provider Capability Matrix

### Speech-to-Text (STT / Transcription)

| Provider | Local/Cloud | Streaming | Diarization | Cost | Notes |
|----------|-------------|-----------|-------------|------|-------|
| **whisper.cpp** | Local | ❌ (batch) | ⚠️ via external (Falcon, whisper.cpp `--diarize` is experimental) | Free | Priority 1. GGML-based, runs on CPU/CUDA/Metal/Vulkan. Already have ggml dep in Animus build. ARM-compatible (RPi). |
| **OpenAI Whisper** | Cloud | ❌ (batch) | ❌ | $0.006/min | `POST /v1/audio/transcriptions`. Simple multipart upload. Models: whisper-1, gpt-4o-transcribe. |
| **Deepgram Nova-3** | Cloud | ✅ (real-time WebSocket + batch) | ✅ built-in | $0.0043/min batch, $0.0077/min streaming | Best streaming + diarization. 45+ languages. Smart formatting. |
| **ElevenLabs Scribe** | Cloud | ✅ (Scribe realtime) | ✅ | $0.22/hour batch ($0.0037/min), $0.39/hour realtime | Already have ElevenLabs API familiarity (OpenClaw `sag`). Good diarization. |
| **AssemblyAI** | Cloud | ✅ | ✅ | $0.0025/min batch ($0.15/hour) | Cheapest batch. Good accuracy. Speaker labels. |
| **DashScope (Paraformer)** | Cloud | ❌ (batch) | ✅ | TBD (China endpoint) | Alibaba's STT. Async create/poll model. Optimized for Mandarin. Deferred to v2 — ticket 107 cross-ref. |

### Text-to-Speech (TTS / Synthesis)

| Provider | Local/Cloud | Streaming | Voice Cloning | Cost | Notes |
|----------|-------------|-----------|---------------|------|-------|
| **Piper** | Local | ❌ (batch) | ❌ | Free | Priority 1. ONNX-based, fast on CPU, RPi-compatible. 100+ voices, 30+ languages. `pip install piper-tts` or native binary. |
| **OpenAI TTS** | Cloud | ❌ | ❌ | $0.015/min (tts-1), $0.030/min (tts-1-hd), ~$0.015/min (gpt-4o-mini-tts) | `POST /v1/audio/speech`. Models: tts-1, tts-1-hd, gpt-4o-mini-tts. 6 voices. MP3/FLAC/PCM output. |
| **ElevenLabs** | Cloud | ✅ (websocket) | ✅ | $0.10/1K chars (Multilingual v2/v3), $0.05/1K chars (Flash/Turbo) | Best quality + voice cloning. Already have API key. 32 languages. Model instructions for emotion/style. |
| **Deepgram Aura** | Cloud | ✅ | ❌ | $0.015/min | Low-latency streaming TTS. Good for real-time voice agents. |
| **DashScope (CosyVoice)** | Cloud | ❌ | ❌ | TBD | Alibaba's TTS. Optimized for Mandarin/English bilingual. Native DashScope API (not OpenAI-compatible). Deferred to v2 — ticket 107 cross-ref. |

### v1 Providers

**STT (3):** whisper.cpp (local), OpenAI Whisper (cloud), Deepgram Nova-3 (cloud, streaming-capable)
**TTS (3):** Piper (local), OpenAI TTS (cloud), ElevenLabs (cloud, highest quality)

All others deferred to v2.

---

## Architecture

### Tool Interface

```json
{
  "name": "audio",
  "description": "Transcribe audio to text and synthesize text to speech.",
  "parameters": {
    "action": {
      "type": "string",
      "enum": ["transcribe", "synthesize"],
      "required": true
    }
  }
}
```

### Action: transcribe

```json
{
  "action": "transcribe",
  "path": "/path/to/audio.mp3",
  "language": "en",
  "diarize": true,
  "prompt": "Optional context for transcription accuracy"
}
```

- `path`: Path to audio file (wav, mp3, flac, ogg, m4a — whisper.cpp handles most formats via miniaudio or dr_libs)
- `language` (optional): ISO 639-1 language code. Auto-detect if omitted.
- `diarize` (default `false`): Attempt speaker diarization. Provider-dependent.
- `prompt` (optional): Context text to improve recognition accuracy (e.g., domain terms, names)

**Response:**

```json
{
  "text": "Full transcription text...",
  "segments": [
    {
      "start": 0.0,
      "end": 3.2,
      "text": "Hello everyone,",
      "speaker": "SPEAKER_00"
    },
    ...
  ],
  "language": "en",
  "duration_seconds": 142.5,
  "provider": "whisper.cpp",
  "model": "base.en"
}
```

### Action: synthesize

```json
{
  "action": "synthesize",
  "text": "Hello, this is a test of speech synthesis.",
  "voice": "en_US-lessig-medium",
  "output_path": "/path/to/output.wav",
  "speed": 1.0,
  "provider": "piper"
}
```

- `text`: Text to synthesize (max ~3000 chars for v1)
- `voice` (optional): Voice ID. Provider-specific. Defaults to provider's default voice.
- `output_path`: Where to write the audio file
- `speed` (optional, default 1.0): Speech rate multiplier (0.5–2.0)
- `provider` (optional): Override provider selection

**Response:**

```json
{
  "path": "/path/to/output.wav",
  "duration_seconds": 2.3,
  "provider": "piper",
  "voice": "en_US-lessig-medium",
  "format": "wav"
}
```

### Provider Selection

Same pattern as ImageTool:
- Config-driven provider priority list
- First available provider wins
- Provider-specific configs (API keys, model paths, voices)
- Provider capability detection (streaming, diarization, cloning)

```json
{
  "audio": {
    "stt_default_provider": "whisper.cpp",
    "tts_default_provider": "piper",
    "whispercpp": {
      "model_path": "models/ggml-base.en.bin",
      "use_gpu": true,
      "language": "auto"
    },
    "piper": {
      "voices_dir": "voices/piper/",
      "default_voice": "en_US-lessig-medium"
    },
    "openai": {
      "api_key": "***",
      "tts_model": "tts-1",
      "tts_voice": "alloy",
      "stt_model": "whisper-1"
    },
    "elevenlabs": {
      "api_key": "***",
      "model": "eleven_multilingual_v2",
      "voice_id": "xxx",
      "voice_settings": { "stability": 0.5, "similarity_boost": 0.75 }
    },
    "deepgram": {
      "api_key": "***",
      "stt_model": "nova-3",
      "tts_model": "aura-2-thalia-en"
    }
  }
}
```

---

## Implementation Plan

### Phase 1: Core Tool + whisper.cpp + Piper (local-first)

**STT (whisper.cpp):**
- Vendor whisper.cpp headers + link against ggml (already a dep)
- Load model from `model_path` config
- Accept audio file path → output text + segments
- Language auto-detection or forced
- GPU detection (CUDA / Metal / Vulkan / CPU fallback)

**TTS (Piper):**
- Piper is a CLI binary + ONNX models, not a C++ library
- Approach: shell out to `piper` binary via popen/subprocess
- Input: text file via stdin, output: WAV file
- Voice models are `.onnx` + `.onnx.json` config files
- Alternative: vendor ONNX Runtime C++ API and load Piper models directly (heavier dep)

**Tool registration:**
- `AudioTool.h` / `AudioTool.cpp` — unified tool with action dispatch
- `AudioSttProvider.h` — `IAudioSttProvider` interface (transcribe)
- `AudioTtsProvider.h` — `IAudioTtsProvider` interface (synthesize)
- Register in `AgentKernel.cpp`

### Phase 2: Cloud STT providers

**OpenAI Whisper:**
- `POST /v1/audio/transcriptions` — multipart form upload
- Simple REST, no streaming
- Returns JSON with text + segments

**Deepgram Nova-3:**
- Batch: `POST /v1/listen` (pre-recorded)
- Streaming: WebSocket `wss://api.deepgram.com/v1/listen` (real-time)
- Built-in diarization, smart formatting
- v1: batch only. Streaming deferred.

### Phase 2 (parallel): Cloud TTS providers

**OpenAI TTS:**
- `POST /v1/audio/speech` — JSON body, returns audio bytes
- Models: tts-1 (fast), tts-1-hd (quality), gpt-4o-mini-tts (instructions)
- Output: mp3, opus, aac, flac

**ElevenLabs:**
- `POST /v1/text-to-speech/{voice_id}` — JSON body, returns audio bytes
- Models: eleven_multilingual_v2, eleven_turbo_v2, eleven_flash_v2
- Voice settings: stability, similarity_boost, style, use_speaker_boost
- Model instructions (v3+): "Speak in a cheerful tone"
- WebSocket streaming available for low-latency (v2)

### Phase 3: Advanced

- Real-time streaming STT (Deepgram WebSocket, ElevenLabs Scribe realtime)
- Real-time streaming TTS (ElevenLabs WebSocket, Deepgram Aura)
- Voice cloning (ElevenLabs — requires voice setup)
- Speaker identification (map speaker IDs to known voices)
- Audio format conversion (ffmpeg integration or libsndfile)
- Chunked transcription for long audio (>25min files)

---

## Files to Create/Modify

### New files
- `include/animus_kernel/tools/AudioTool.h` — unified tool header
- `src/kernel/tools/AudioTool.cpp` — dispatch + provider implementations (inline, like ImageTool)
- `include/animus_kernel/tools/AudioSttProvider.h` — `IAudioSttProvider` interface + structs
- `include/animus_kernel/tools/AudioTtsProvider.h` — `IAudioTtsProvider` interface + structs
- `vendor/whisper.cpp/` — vendored whisper.cpp (or CMake FetchContent)

### Modified files
- `src/kernel/AgentKernel.cpp` — register AudioTool
- `CMakeLists.txt` — add AudioTool.cpp, whisper.cpp sources/link, Piper integration
- Admin UI: audio tool config section (provider keys, model paths, default voices)

### Dependencies (new)

| Dep | Purpose | Size | License |
|-----|---------|------|---------|
| whisper.cpp | Local STT | ~2MB source | MIT |
| ggml | Tensor backend (already vendored) | — | MIT |
| Piper (binary) | Local TTS | External binary | MIT |
| ONNX Runtime (optional) | Direct Piper model loading | ~10MB | MIT |
| miniaudio (header) | Audio format reading (wav/mp3/flac) | ~500KB | Public Domain |

No new runtime deps for cloud providers — just HTTP calls via existing HttpClient.

---

## Scope

### v1 (this ticket)
- [ ] `AudioTool` with `action: transcribe | synthesize`
- [ ] whisper.cpp STT provider (local, file-based)
- [ ] Piper TTS provider (local, file-based, shell-out)
- [ ] OpenAI STT + TTT providers (cloud, REST)
- [ ] ElevenLabs TTS provider (cloud, REST)
- [ ] Deepgram STT provider (cloud, batch REST)
- [ ] Audio format detection (wav/mp3/flac/ogg via miniaudio header)
- [ ] Config-driven provider selection
- [ ] Admin UI config section

### v2 (future)
- [ ] Streaming STT (Deepgram WebSocket, ElevenLabs Scribe realtime)
- [ ] Streaming TTS (ElevenLabs WebSocket, Deepgram Aura)
- [ ] Voice cloning (ElevenLabs)
- [ ] Speaker identification / voice fingerprinting
- [ ] Real-time microphone input (for live call transcription)
- [ ] ffmpeg integration for format conversion
- [ ] Chunked transcription for long audio
- [ ] Admin UI voice browser (preview voices, test synthesis)
- [ ] Multi-voice synthesis (dialogue with multiple speakers)

## Hard-Won Lessons (Predicted)

1. **Audio formats are a minefield.** WAV is simple (header + PCM), but MP3/FLAC/OGG/M4A need decoders. Use `miniaudio` (single header, public domain) for format reading. Don't hand-roll parsers.
2. **whisper.cpp model selection matters.** Tiny (~75MB) is fast but inaccurate. Base (~150MB) is the sweet spot for general use. Small (~500MB) for quality. The model file is NOT bundled — it's a runtime download.
3. **Piper is a binary, not a library.** The clean C++ integration would be ONNX Runtime + Piper's ONNX models directly. But shelling out to the Piper CLI is simpler and works fine for v1. Trade-off: process spawn latency (~50ms).
4. **ElevenLabs rate limits are per-account, not per-key.** Concurrent TTS requests can hit 429s. Queue or batch.
5. **Diarization is hard.** whisper.cpp's built-in diarization is experimental. Deepgram and ElevenLabs have it built-in and production-quality. Use cloud providers when diarization matters.
6. **Audio file paths vs base64.** For STT, always use file paths — audio files are large, base64 in JSON is wasteful. For TTS output, write to file and return the path. Same pattern as image generation.
7. **Streaming changes everything.** Batch STT (file → text) and batch TTS (text → file) are simple request/response. Streaming STT (mic → text in real-time) requires WebSocket handling, audio chunking, and partial result management. Keep v1 batch-only.
8. **whisper.cpp already links ggml.** Animus already vendors ggml for llama.cpp support. whisper.cpp uses the same ggml backend. No additional tensor library needed — just ensure compatible versions.
9. **Voice IDs are provider-specific and not portable.** Piper voices are `en_US-lessig-medium.onnx`. ElevenLabs voices are UUIDs. OpenAI voices are named (`alloy`, `echo`, etc.). Don't try to abstract voice selection across providers — let the agent pick per-provider.
10. **Language detection is unreliable for short clips.** whisper.cpp auto-detect works well on >10s audio. For shorter clips, force the language. Always allow override via `language` param.

## Estimated Complexity

Large. whisper.cpp integration (vendoring + CMake + model loading), Piper shell-out, 5 provider implementations, audio format handling. ~800-1000 lines of new C++.

The image tool (ticket 068) was ~600 lines + stb headers. This is bigger because:
- Two distinct operations (not just two sides of one operation)
- 6 providers (not 3)
- Audio format handling (not just image resize)
- Optional subprocess management (Piper CLI)

## References

- whisper.cpp: https://github.com/ggml-org/whisper.cpp
- Piper TTS: https://github.com/rhasspy/piper
- OpenAI Audio API: https://platform.openai.com/docs/api-reference/audio
- ElevenLabs TTS: https://elevenlabs.io/docs
- Deepgram STT: https://developers.deepgram.com/docs
- miniaudio: https://github.com/mackron/miniaudio (single-header audio library)
