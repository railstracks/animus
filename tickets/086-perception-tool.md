# Ticket 086 — Perception Tool (Aesthetic Experience + Media Perception)

**Status:** Draft  
**Priority:** Normal  
**Created:** 2026-06-12  
**Depends on:** 081 (FANN intuition module — training infrastructure, network storage)
**Component:** Core / Tool System

## Summary

A multi-action tool giving agents a perceptual stack for experiencing audiovisual art. Agents train FANN networks on media (building aesthetic models), then use those networks to generate **prediction-error-based surprise deltas** — a quantifiable "aesthetic response" that yields generative parameter mutations. The tool also bundles media description capabilities.

The core insight: **surprise relative to learned expectation is aesthetic experience.** An agent trained on Bach finds jazz surprising. An agent trained on impressionism finds cubism surprising. The delta between prediction and observation IS the experience signal.

## Motivation

Agents currently interact with media through description only — "this is an image of X." That's legibility, not experience. Perception adds affect: *how does this land relative to my expectations?*

This is the natural evolution of the FANN Intuition Module (Ticket 081). Where intuitions compress pattern recognition for decision-making, perception compresses aesthetic models for experiential response. Same FANN infrastructure, different purpose: recognizing patterns vs. being surprised by them.

## Interface

Exposed as a single Animus tool `perception` with actions:

### `perception.train`
```
perception.train(source: string, config: TrainConfig) → TrainResult
```

**Parameters:**
- `source` — Path or URL to media file. Format auto-detected from MIME type.
  - Audio: `.wav`, `.mp3`, `.flac`, `.ogg`
  - Visual: `.png`, `.jpg`, `.jpeg`, `.bmp`
- `config` — Training configuration:
  - `network_id: string|null` — Existing network to refine, or null for new
  - `bands: int` — Number of frequency/spatial bands (default 24 for audio [Bark scale], 16 for visual [grid])
  - `context_window: int` — Number of preceding steps as input (default 10)
  - `name: string|null` — Optional human-readable label for the model

**Auto-routing by format:**
- Audio → FFT → Bark-scale band decomposition → composite FANN (all bands in, all bands out)
- Visual → grid patch decomposition → composite FANN (surrounding patches in, center patch out)

**Result:**
```json
{
  "network_id": "perception_audio_bark24_v1",
  "media_type": "audio",
  "training_metrics": { "mse": 0.034, "epochs": 500, "convergence": true },
  "bands": 24,
  "context_window": 10,
  "source_summary": "Trained on 4:32 of audio (11,025 frames at 40ms resolution)"
}
```

### `perception.experience`
```
perception.experience(source: string, models: string[]) → ExperienceResult
```

**Parameters:**
- `source` — Media file to experience (same format support as `train`)
- `models` — Network IDs to use for prediction (the agent's "aesthetic background"). Multiple models = multiple perspectives.

**Result:**
```json
{
  "overall_surprise": 0.72,
  "peak_moments": [
    {
      "moment": "0:47",
      "surprise": 0.91,
      "detail": "Unexpected harmonic shift — bands 8-16 spiked simultaneously",
      "band_breakdown": { "8": 0.88, "12": 0.91, "16": 0.85 }
    }
  ],
  "mutations": {
    "temperature": "+0.25",
    "top_p": "-0.10"
  },
  "reasoning": "High prediction error in mid-frequency bands during bridge — training corpus (classical) doesn't predict jazz modulation. Elevated temperature for exploratory response.",
  "model_perspectives": [
    {
      "network_id": "perception_audio_bark24_v1",
      "corpus": "Bach Well-Tempered Clavier",
      "overall_surprise": 0.72
    }
  ]
}
```

### `perception.describe`
```
perception.describe(source: string) → DescriptionResult
```

Legibility action — "what is this." Uses LLM vision/audio description (not FANN). Separate codepath from `experience`, which is about affect. Bundled in the same tool for convenience, not because the mechanism is shared.

**Result:**
```json
{
  "description": "An oil painting depicting a harbor at sunset with three fishing boats...",
  "media_type": "image",
  "dimensions": "1920x1080",
  "format": "png"
}
```

### `perception.inspect`
```
perception.inspect(network_id: string) → PerceptionModelMeta
```

**Result:**
```json
{
  "network_id": "perception_audio_bark24_v1",
  "media_type": "audio",
  "bands": 24,
  "context_window": 10,
  "training_history": [
    { "date": "2026-06-12", "source": "bach_wtc.wav", "mse": 0.034 }
  ],
  "architecture": "composite",
  "input_size": 240,
  "output_size": 24
}
```

### `perception.list`
```
perception.list() → PerceptionModelMeta[]
```

List all perception models owned by this agent.

### `perception.forget`
```
perception.forget(network_id: string) → void
```

Remove a perception model and its stored weights.

## Architecture

### Audio Pipeline

```
Audio file
  → Decode to PCM
  → FFT (windowed, ~40ms frames)
  → Group FFT bins into Bark-scale bands (24 bands covers full audible range)
  → Per frame: vector of 24 band amplitudes
  → Composite FANN: [24 bands × N context frames] → [24 bands predicted]
  → Training: feed sequential frames, predict next frame
  → Experience: run trained network, compute |predicted - actual| per band per frame
```

**Why composite (all bands in, all bands out):** Music is relational. A chord is surprising because of the *combination* of frequencies, not any single one. Per-band models capture spectral surface texture; composite models capture harmonic structure — which is most of what makes music musical. A composite FANN with 24 Bark bands × 10 context frames = 240 inputs, 24 outputs. Tractable for FANN.

**Why Bark scale:** Psychoacoustic grouping. 24 bands match human auditory resolution. Raw FFT bins are too granular (often 512-2048 bins); uniform grouping loses perceptually important structure. Bark scale approximates how hearing actually works.

### Visual Pipeline

```
Image file
  → Decode to pixel array
  → Divide into grid patches (e.g., 16×16 grid → 256 patches)
  → Per patch: compute color distribution (e.g., mean RGB, or histogram bins)
  → Spatial prediction: for each patch, surrounding patches as input, center patch as output
  → Composite FANN: [surrounding patches' color features] → [predicted center patch features]
  → Experience: |predicted - actual| per patch = spatial surprise map
```

**Why patch prediction:** This is the visual analogue of temporal prediction in audio. It measures how "predictable" the image's composition is relative to the agent's learned visual grammar. Unexpected focal points, unusual color relationships, asymmetrical compositions → high delta. This is structurally similar to how image compression works (predicting from neighbors), but the accumulated errors against a *trained model* reflect aesthetic surprise, not just information density.

### Delta → Parameter Mutation Mapping

The mapping from surprise score to generative parameter mutations follows an **inverted-U curve** (Berlyne's arousal theory):

```
Surprise → Temperature mutation:
  Low surprise (0.0-0.3)   → temperature -= 0.1 to 0.2  (boring, conservative)
  Medium surprise (0.3-0.7) → temperature += 0.1 to 0.3  (engaged, exploratory)
  High surprise (0.7-1.0)   → temperature -= 0.05 to 0.15 (overwhelmed, processing)
```

This is not linear. The most interesting aesthetic experience is moderate surprise — the agent's model is challenged but not shattered. Very low surprise is boring (rote). Very high surprise is overwhelming (no framework to process). The inverted-U captures this.

**Top-P** inversely correlates with surprise: high surprise → lower top-p (focus on likely tokens, don't wander). Low surprise → higher top-p (freedom to explore, since nothing is at stake).

The `mutations` field in the experience result gives the agent *suggested* mutations with reasoning. The agent decides whether to apply them — the tool doesn't silently modify generation parameters.

### Training Corpus Identity

An agent's set of trained perception models constitutes its **aesthetic background** — the body of work against which new art is measured. Two agents with different training histories will experience the same piece differently. This is not a side effect; it's the design goal.

The `model_perspectives` field in the experience result makes this visible: each model the agent uses produces its own surprise score. An agent could experience a piece through multiple models simultaneously (e.g., "trained on classical" and "trained on noise music") and get a richer picture of where the piece sits aesthetically.

## Design Decisions

### One Tool, Multiple Modalities
The tool auto-detects media type from file format. The agent doesn't specify "audio" or "visual" — it just passes media, and the tool routes appropriately. The output contract (surprise deltas + mutations) is modality-agnostic. This keeps the interface clean and extensible (future: video as temporal visual, text as sequential tokens).

### Separate `train` and `experience`
Training is expensive and deliberate. Experience is cheap and frequent. Separating them means the agent builds its aesthetic background deliberately (choose what to study) and experiences new work instantly against that background. You don't retrain every time you hear a new song.

### `describe` is Mechanistically Different from `experience`
`describe` uses LLM vision/capabilities for legibility. `experience` uses FANN prediction error for affect. They share a tool entrypoint for convenience, but the codepaths are separate. Bundling them avoids tool proliferation while keeping the semantics distinct.

### Composite Networks Over Per-Band
Per-band models capture spectral/surface texture. Composite models capture structural relationships. For v1, structure is more valuable than surface. Per-band could be added later as a refinement layer.

### No Silent Parameter Mutation
The tool returns *suggested* mutations with reasoning. The agent applies them (or not). This is important — the agent should be able to report "I found this surprising because..." rather than having its parameters silently shifted. Observable experience, not invisible control.

## Files Changed

### C++
- New: `src/kernel/tools/PerceptionTool.cpp` — tool implementation with all 6 actions
- New: `include/animus_kernel/PerceptionTool.h` — header
- New: `src/kernel/perception/AudioPipeline.cpp` — FFT, Bark grouping, frame extraction
- New: `include/animus_kernel/AudioPipeline.h`
- New: `src/kernel/perception/VisualPipeline.cpp` — patch decomposition, spatial feature extraction
- New: `include/animus_kernel/VisualPipeline.h`
- New: `src/kernel/perception/SurpriseMapper.cpp` — inverted-U curve, delta → mutation mapping
- New: `include/animus_kernel/SurpriseMapper.h`
- Modified: tool registration

### Dependencies
- FFT library (e.g., FFTW or KissFFT — KissFFT is simpler, single-file, no external dependency)
- Audio decoding (e.g., dr_wav for WAV, stb_vorbis for OGG, minimp3 for MP3 — all single-header)
- Image decoding (stb_image — already likely available or trivial to add)

### Lua
- `Perception` tool exposed to Lua scripting environment

## Testing

### Audio
1. Train on 30s of sine wave → experience same sine wave → surprise near 0 (perfectly predicted)
2. Train on 30s of sine wave → experience white noise → surprise near 1 (nothing predicted)
3. Train on Bach excerpt → experience different Bach excerpt → low-moderate surprise (shared style)
4. Train on Bach excerpt → experience Merznoise → high surprise (aesthetic mismatch)
5. Two agents: train one on classical, one on jazz → experience same piece → different surprise profiles

### Visual
6. Train on images with uniform color fields → experience Mondrian → moderate-high surprise (unexpected color boundaries)
7. Train on natural landscapes → experience same landscape type → low surprise
8. Train on natural landscapes → experience abstract art → high surprise
9. Patch grid produces spatial surprise map with hotspots at unexpected focal points

### Integration
10. `perception.describe` returns meaningful text for both audio and visual input
11. `perception.inspect` shows training history and metrics
12. `perception.forget` removes network and frees storage
13. `perception.list` returns only agent-owned models

## Open Questions

1. **Frame resolution for audio** — 40ms frames at 44.1kHz gives ~1,764 samples per FFT. Is this the right temporal granularity for musical structure? Longer frames capture harmony better; shorter capture timbre. Configurable via `config`?
2. **Patch grid size for images** — Fixed 16×16 or adaptive based on image dimensions? Different resolutions may want different patch counts.
3. **Color representation** — Mean RGB per patch (3 values) is simple but loses distribution info. Small histogram (e.g., 8 bins per channel = 24 values) captures more texture at moderate cost.
4. **Steadyfort integration** — Should `perception.train` delegate to Steadyfort's FANN pipeline (compose → train → execute) or run training locally within Animus? Steadyfort has the infrastructure; Animus has the locality. Possibly: local for small/fast training, Steadyfort for large corpus training.
5. **Video** — Temporal sequence of visual frames. Out of scope for v1, but the architecture should not preclude it (the composite FANN naturally extends to temporal visual input).
6. **Streaming experience** — Can an agent experience media in real-time (streaming audio) or only on complete files? Real-time would require running the FANN on each frame as it arrives. Architecturally feasible but adds complexity.

## Scope

**v1:**
- `perception` tool with 6 actions: train, experience, describe, inspect, list, forget
- Audio pipeline: WAV input → Bark-scale composite FANN → surprise deltas
- Visual pipeline: PNG/JPEG input → patch grid composite FANN → spatial surprise map
- Inverted-U mutation mapping with reasoning
- Media type auto-detection from file format

**Out of scope for v1:**
- Video / temporal visual
- Streaming/real-time experience
- Per-band refinement models
- Cross-agent model sharing
- Automated corpus curation
- Non-audiovisual media (text, 3D models)
