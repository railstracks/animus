#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace animus::kernel::calls {

/// Audio-in pipeline for the calls lane (#337, Calls Tier 1 ticket 4).
///
/// Per-participant: Opus frames in (stereo 48 kHz) → deinterleave-to-mono
/// → 3:1 decimate to 16 kHz → VAD segmentation (decision 3: min 0.4 s,
/// end-silence 1.2 s, hard-max 15 s with gap-aware split) → junk filter
/// (voiced-ratio + tagged/empty drop, per Phase 0 leg-3 evidence) →
/// STT sink → utterance events out.
///
/// Cortex-shaped seams (decision 1): VAD and STT are callbacks — the
/// pipeline holds no codec/service state and never touches signaling.
/// The Opus DECODER itself is adapter-side (libopus); this component
/// receives raw PCM. Offline tests run deterministic stubs; production
/// wires fvad and whisper_stt.
///
/// Threading: not internally synchronized — one pipeline per participant
/// track, driven from the adapter's audio path. Callbacks fire inline.
struct AudioInConfig {
    double min_utterance_s = 0.4;   // shorter voiced runs are not speech
    double end_silence_s = 1.2;     // trailing unvoiced time closes a segment
    double hard_max_s = 15.0;       // force-split bound
    /// Junk filter: minimum voiced-frame ratio per emitted utterance
    /// (leg-3: VAD passes music; whisper hallucinates on some of it).
    double min_voiced_ratio = 0.5;
    /// Junk filter: drop STT results that are empty or exactly a
    /// bracketed tag (e.g. "[BLANK_AUDIO]", "[Music]").
    bool drop_tagged_texts = true;
    /// 10 ms frames at 16 kHz mono.
    static constexpr int kFrameSamples = 160;
};

/// One transcribed utterance, speaker-attributed (Tier 0: from the
/// track's session/userid, never a voiceprint).
struct Utterance {
    std::string speaker;   // participant display/user id (adapter-supplied)
    std::string text;      // STT result (post junk filter)
    double start_s = 0.0;  // segment start since pipeline start
    double end_s = 0.0;
    double voiced_ratio = 0.0;  // voiced frames / total frames
    /// True when the 15 s bound split the utterance mid-stream —
    /// boundaries are suspect (leg-3: "treat split boundaries as
    /// suspect"); the consumer coalesces at the thread boundary.
    bool split_bound = false;
};

class AudioInPipeline {
public:
    /// VAD seam: called per 10 ms mono frame; returns true when voiced.
    using VadCallback = std::function<bool(const int16_t* frame, int samples)>;
    /// STT seam: transcribe a completed utterance's PCM (16 kHz mono,
    /// little-endian 16-bit). Returns text ("" on failure).
    using SttCallback =
        std::function<std::string(const std::vector<int16_t>& pcm)>;
    /// Utterance event sink (fires inline, already junk-filtered).
    using UtteranceCallback = std::function<void(const Utterance&)>;

    AudioInPipeline(AudioInConfig cfg, std::string speaker,
                    VadCallback vad, SttCallback stt,
                    UtteranceCallback onUtterance);

    /// Stereo 48 kHz intake (per 20 ms Opus-decoded frame: 2 ch × 960).
    /// Deinterleaves to mono (averages the pair — leg-2 evidence: naive
    /// flatten halves speed; deinterleave is correct), then decimates
    /// 3:1 to 16 kHz and drives the segmenter.
    void FeedStereo48k(const int16_t* data, size_t samples_per_channel);

    /// Mono 16 kHz intake (already-resampled producers).
    void FeedMono16k(const int16_t* data, size_t samples);

    /// Flush trailing audio at call leave (closes any open segment).
    void Flush();

    /// Segmentation state for tests/inspection.
    bool SegmentOpen() const { return !current_.empty(); }

    // ---- pure helpers, offline-testable ----

    /// Stereo→mono deinterleave (average L/R pairs).
    static std::vector<int16_t> DeinterleaveStereo(
        const int16_t* data, size_t samples_per_channel);
    /// Exact 3:1 decimation 48 kHz→16 kHz (average each triple).
    static std::vector<int16_t> Decimate3to1(const int16_t* data,
                                             size_t samples);
    /// Junk filter: text is kept when non-empty and (unless
    /// allow_tagged) not exactly one bracketed tag. "[Music]",
    /// "[BLANK_AUDIO]", "[noise]" shapes all match.
    static bool TextIsSpeech(const std::string& text, bool allow_tagged);
    /// Junk filter: ratio above the configured floor.
    bool VoicedRatioPasses(double voiced_ratio) const;

private:
    void ProcessFrame(const int16_t* frame);  // one 10 ms mono frame
    void ProcessSample(int16_t s);           // 16k mono sample intake
    void FeedMono48k(const int16_t* data, size_t samples); // decimating path
    void CloseSegment(bool split_bound);
    void Emit(const std::vector<int16_t>& pcm, double voiced_ratio,
              double start_s, double end_s, bool split_bound);

    AudioInConfig cfg_;
    std::string speaker_;
    VadCallback vad_;
    SttCallback stt_;
    UtteranceCallback on_;

    std::vector<int16_t> decim_buf_;  // 48k mono accumulator (decimation)
    std::vector<int16_t> frame_buf_;  // 16k mono accumulator (10 ms frames)

    // Segmenter state (10 ms frames)
    std::vector<int16_t> current_;  // open segment PCM (bounded: hard-max)
    std::vector<uint8_t> current_flags_; // per-frame voiced flags
    size_t current_frames_ = 0;       // total frames in open segment
    size_t voiced_frames_ = 0;
    size_t trailing_silence_ = 0;     // consecutive unvoiced frames
    double seg_start_s_ = 0.0;
    double clock_s_ = 0.0;             // frames since pipeline start
};

} // namespace animus::kernel::calls
