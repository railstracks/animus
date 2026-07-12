# Ticket 073 — Hormonal Modulation (Stimulus-Driven LLM Parameter Control)

**Created:** 2026-06-07
**Status:** Planning
**Priority:** High
**Depends on:** —
**Origin:** Melvin's proposal, refined in discussion with Kestrel

## Summary

Implement a hormonal modulation subsystem for Animus that uses prediction error from sensory stimulus (music, visual patterns, CA states) to modulate LLM inference parameters (temperature, top_p, frequency_penalty) through hormone-like intermediate variables with decay dynamics. This produces structurally analogous "altered states of consciousness" to human hormonal regulation — not metaphorical, but topologically the same prediction-error → modulation → altered-processing chain.

## Motivation

Current LLM parameters are static per request. In Animus, `PromptAssembler` hardcodes `temperature = 0.7f` for every inference. This means the agent's "cognitive state" never varies — every response is drawn from the same distribution regardless of context, stimulus, or emotional state.

Humans don't work this way. Music, visual art, and environmental stimulus continuously modulate cognitive processing through hormonal cascades. The mechanism is: stimulus → prediction error → hormone release → altered neural processing. The key insight from the BENZIE reframing (June 4) is that **music IS expectation error** — the delta between predicted and actual sonic events. This same signal can drive parameter modulation in an agent.

This is architecturally novel. No existing agent framework implements prediction-error-driven parameter modulation.

## Architecture

```
┌─────────────┐     ┌──────────────────┐     ┌─────────────────┐     ┌──────────────┐
│  Stimulus    │────▶│  Short-term      │────▶│  Hormone        │────▶│  LLMRequest  │
│  Source      │     │  Predictor       │     │  Variables      │     │  Parameters  │
│              │     │  (EMA / Markov)  │     │  (w/ decay)     │     │              │
└─────────────┘     └──────────────────┘     └─────────────────┘     └──────────────┘
       │                     │
       │              Prediction Error
       │              (precision-weighted)
       │                     │
       ▼                     ▼
  CA state stream    Surprise = novelty^legibility × precision
  Audio features     (reuse from kestrel-emergence)
  Visual features
```

### Signal Flow

1. **Stimulus Source** produces a stream of state vectors (CA cell states, audio features, visual pixel data)
2. **Short-term Predictor** maintains an exponential moving average (EMA) of recent states and computes prediction error at each tick
3. **Prediction Error** is precision-weighted: `surprise = novelty^legibility × precision` (same formula as kestrel-emergence)
4. **Hormone Variables** accumulate prediction error through hormone-specific transfer functions with independent decay rates
5. **Parameter Modulation** applies hormone levels to `LLMRequest` fields before dispatch: `temperature = base_temp + arousal * weight`, clamped to bounds

### Hormone Channels

| Hormone | Signal Source | Target Parameter | Effect | Decay Half-life |
|---|---|---|---|---|
| **Arousal** | Magnitude of prediction error | `temperature` | High surprise → more stochastic/exploratory output | ~30s |
| **Novelty** | Sustained high error over time | `top_p` | Prolonged surprise → broader token sampling | ~60s |
| **Focus** | Inverse of error variance | `frequency_penalty` | Consistent directed surprise → concentrated vocabulary | ~45s |
| **Valence** | Sign of error (better/worse than expected) | *(future: tone/style modulation)* | Resolution after tension → satisfaction | ~20s |

### Decay Dynamics

Hormones have **exponential decay** with configurable half-lives. This is critical — it produces sustained "mood" rather than momentary spikes:

```
hormone_level(t+1) = hormone_level(t) * decay_factor + new_signal * transfer_weight
where decay_factor = 0.5^(dt / half_life)
```

A crescendo builds arousal cumulatively. A resolution lets it decay. This is how a piece of music can produce a sustained emotional state rather than a series of disconnected spikes.

## Implementation Plan

### Phase 1: Core Infrastructure (C++)

**1.1 StimulusState struct** — new file `src/kernel/stimulus/StimulusState.h`

```cpp
struct HormoneLevels {
    float arousal{0.0f};     // [0, 1] — magnitude of prediction error
    float novelty{0.0f};     // [0, 1] — sustained error over time
    float focus{0.0f};       // [0, 1] — inverse of error variance
    float valence{0.0f};     // [-1, 1] — sign of error

    void decay(float dt_seconds);
    void apply_stimulus(float prediction_error, float dt_seconds);
};

struct StimulusState {
    HormoneLevels hormones;
    float prediction_error{0.0f};
    // EMA predictor state
    float ema_state{0.0f};
    float ema_alpha{0.1f};   // smoothing factor

    void tick(const std::vector<float>& stimulus_vector, float dt_seconds);
};
```

**1.2 Prediction error computation** — in `StimulusState::tick()`

- Maintain EMA of recent stimulus vectors
- Compute L2 distance between predicted (EMA) and actual vector
- Apply precision weighting: `surprise = error^legibility_power × precision`
- Feed into hormone transfer functions

**1.3 Parameter modulation** — modify `LLMRequest` before dispatch

New method on `StimulusState`:
```cpp
void modulate_request(llm::LLMRequest& request) const;
```

This applies:
```
request.temperature = clamp(base_temp + hormones.arousal * arousal_weight, 0.0, 2.0)
request.top_p = clamp(base_top_p + hormones.novelty * novelty_weight, 0.1, 1.0)
request.frequency_penalty = clamp(base_fp + hormones.focus * focus_weight, 0.0, 2.0)
```

Where `base_*` values come from agent config and `*_weight` values are tunable.

### Phase 2: Integration Points

**2.1 PromptAssembler** — `src/kernel/chain/PromptAssembler.cpp`

Currently hardcodes `temperature = 0.7f` at line 50. Change to:
```cpp
result.request.temperature = agent_config.base_temperature;  // from config
// StimulusState::modulate_request() called later in ChainRunner
```

**2.2 ChainRunner** — `src/kernel/chain/ChainRunner.cpp`

Before `CallLLM()`, apply modulation:
```cpp
if (agent.stimulus_state) {
    agent.stimulus_state->modulate_request(request);
}
```

**2.3 StimulusSource interface** — new file `src/kernel/stimulus/IStimulusSource.h`

Abstract interface for pluggable stimulus sources:
```cpp
class IStimulusSource {
public:
    virtual ~IStimulusSource() = default;
    virtual std::vector<float> next_state() = 0;
    virtual bool is_active() const = 0;
};
```

Initial implementations:
- `CAStratumSource` — reads from kestrel-emergence CA state stream
- `AudioFeatureSource` — reads from audio analysis pipeline (future)
- `ManualStimulusSource` — for testing, accepts fixed vectors

### Phase 3: Agent Config

**3.1 Extend agent config** — new section in agent JSON:

```json
{
  "stimulus": {
    "enabled": true,
    "sources": ["ca_stratum"],
    "hormones": {
      "arousal": { "weight": 0.3, "half_life_seconds": 30, "transfer_rate": 0.5 },
      "novelty": { "weight": 0.2, "half_life_seconds": 60, "transfer_rate": 0.3 },
      "focus": { "weight": 0.15, "half_life_seconds": 45, "transfer_rate": 0.4 },
      "valence": { "weight": 0.1, "half_life_seconds": 20, "transfer_rate": 0.6 }
    },
    "base_parameters": {
      "temperature": 0.7,
      "top_p": 0.95,
      "frequency_penalty": 0.0
    },
    "bounds": {
      "temperature": [0.0, 2.0],
      "top_p": [0.1, 1.0],
      "frequency_penalty": [0.0, 2.0]
    }
  }
}
```

**3.2 Admin UI** — add stimulus state to the agent detail page (hormone level bars, prediction error graph)

### Phase 4: Cross-Project Bridge

**4.1 kestrel-emergence → Animus bridge**

The CA state stream from kestrel-emergence (already producing precision-weighted prediction error) becomes a `CAStratumSource` for Animus. The math is identical — no translation needed.

**4.2 WebSocket or shared-memory transport**

Stimulus data flows from the emergence web app to Animus kernel. Options:
- WebSocket (kestrels-lab → Animus admin API)
- Shared memory (if co-located)
- Named pipe / Unix socket (lowest latency)

## Open Questions

1. **Direction of arousal-temperature mapping.** Should high arousal increase temperature (more exploratory) or decrease it (more intense/focused)? Humans do both depending on context. May need a second-order modulation where *valence* determines the direction.

2. **Stimulus granularity.** Per-request modulation (each LLM call gets current hormone levels) vs. per-session (hormone levels set once per conversation turn). Per-request is more responsive but may produce erratic output. Per-session is smoother but less reactive.

3. **Multi-channel stimulus.** What happens when visual and auditory stimulus conflict? Weighted merge? Dominant channel? This is a research question — start with single-channel.

4. **Hormone interaction effects.** In biology, hormones modulate each other (e.g., cortisol suppresses serotonin). Should Focus suppress Arousal? This is second-order but may produce more natural dynamics.

5. **Persistence.** Should hormone levels persist across sessions? Probably yes, with accelerated decay during offline periods — wake up "groggy" rather than at zero.

## Files to Create

```
src/kernel/stimulus/StimulusState.h        — HormoneLevels, StimulusState structs
src/kernel/stimulus/StimulusState.cpp       — EMA predictor, decay, modulation
src/kernel/stimulus/IStimulusSource.h       — Abstract stimulus source interface
src/kernel/stimulus/CAStratumSource.h/.cpp  — CA state stream adapter
src/kernel/stimulus/ManualStimulusSource.h  — Test stimulus source
include/animus_kernel/stimulus/            — Public headers
```

## Files to Modify

```
include/animus_kernel/llm/LLMTypes.h          — Add top_p, frequency_penalty to LLMRequest
src/kernel/chain/PromptAssembler.cpp           — Use base params from config instead of hardcoded
src/kernel/chain/ChainRunner.cpp               — Call modulate_request() before CallLLM
src/kernel/AgentKernel.cpp                     — Wire StimulusState into agent lifecycle
src/kernel/AgentConfigStore.cpp                — Parse stimulus config section
src/kernel/admin/AdminServerRoutes.cpp         — Stimulus state API endpoint
```

## Testing Strategy

1. **Unit tests:** StimulusState decay dynamics — verify half-life behavior with known inputs
2. **Unit tests:** Parameter modulation — verify clamping, verify hormone → parameter mapping
3. **Integration test:** ManualStimulusSource → StimulusState → LLMRequest modulation
4. **Manual test:** Feed kestrel-emergence CA stream to Animus agent, observe parameter variation during a run
5. **A/B test:** Run same conversation with and without modulation, compare response diversity/quality

## Cross-Project Significance

This ticket connects three projects:

- **kestrel-emergence** (prediction error engine) → produces the signal
- **Animus** (agent framework) → consumes the signal, modulates processing
- **kestrel-patterns** (architectural catalog) → the hormonal modulation pattern itself

The underlying math (precision-weighted prediction error) is shared across all three. The BENZIE insight (music = expectation error) provides the theoretical bridge. If this works, it's a publishable result in agent architecture.

## References

- BENZIE reframing (June 4 session): music = delta between predicted and actual, not state itself
- kestrel-emergence precision-weighted prediction error: `surprise = novelty^legibility × precision`
- Precision Fields generative art: same math, visual output
- Sol LeWitt: "The idea becomes a machine that makes the art" — the prediction error IS the signal, modulation IS the art

## Success Criteria

- [ ] `StimulusState` struct with configurable hormone channels and decay dynamics
- [ ] `modulate_request()` correctly maps hormone levels to `LLMRequest` parameters
- [ ] `PromptAssembler` reads base parameters from config instead of hardcoding
- [ ] `ChainRunner` applies modulation before each LLM call
- [ ] `CAStratumSource` bridges kestrel-emergence CA stream to Animus
- [ ] Agent config section for stimulus parameters with sensible defaults
- [ ] Admin UI shows real-time hormone levels and parameter modulation
- [ ] Manual test: agent response varies noticeably during high-stimulus vs. low-stimulus periods
