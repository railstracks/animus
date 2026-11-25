// AudioInPipeline — audio-in pipeline for the calls lane (#337).
// Stereo Opus intake → mono 16 kHz → VAD segmentation (decision 3) →
// junk filter (leg-3 evidence) → STT seam → utterance events.
// Cortex-shaped seams (decision 1): VAD/STT are callbacks; no codec or
// service state lives here; signaling code never sees audio.

#include "animus_kernel/calls/AudioInPipeline.h"
#include "animus_kernel/Log.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace animus::kernel::calls {

namespace {
constexpr double kFrameS = 0.01; // 10 ms frames
}

AudioInPipeline::AudioInPipeline(AudioInConfig cfg, std::string speaker,
                                 VadCallback vad, SttCallback stt,
                                 UtteranceCallback onUtterance)
    : cfg_(std::move(cfg)),
      speaker_(std::move(speaker)),
      vad_(std::move(vad)),
      stt_(std::move(stt)),
      on_(std::move(onUtterance)) {}

// ---- pure helpers ----

std::vector<int16_t> AudioInPipeline::DeinterleaveStereo(
    const int16_t* data, size_t samples_per_channel) {
    // Leg-2 evidence: naive flatten-as-mono halves playback speed (L
    // stream then R stream); averaging interleaved pairs is correct.
    std::vector<int16_t> out(samples_per_channel);
    for (size_t i = 0; i < samples_per_channel; ++i) {
        const int32_t l = data[2 * i];
        const int32_t r = data[2 * i + 1];
        out[i] = static_cast<int16_t>((l + r) / 2);
    }
    return out;
}

std::vector<int16_t> AudioInPipeline::Decimate3to1(const int16_t* data,
                                                   size_t samples) {
    std::vector<int16_t> out(samples / 3);
    for (size_t i = 0; i + 2 < samples; i += 3) {
        const int32_t avg = (static_cast<int32_t>(data[i]) +
                             static_cast<int32_t>(data[i + 1]) +
                             static_cast<int32_t>(data[i + 2])) / 3;
        out[i / 3] = static_cast<int16_t>(avg);
    }
    return out;
}

bool AudioInPipeline::TextIsSpeech(const std::string& text,
                                   bool allow_tagged) {
    std::string t = text;
    // trim whitespace
    const char* ws = " \t\r\n";
    const auto b = t.find_first_not_of(ws);
    if (b == std::string::npos) return false;
    const auto e = t.find_last_not_of(ws);
    t = t.substr(b, e - b + 1);
    if (t.empty()) return false;
    if (allow_tagged) return true;
    // Exactly one bracketed tag ("[Music]", "[BLANK_AUDIO]") is the
    // whisper-hallucination shape (leg-3: tag-matching alone is not a
    // junk filter — the ratio filter handles the rest).
    if (t.front() == '[' && t.back() == ']') {
        if (t.find('[') == 0 &&
            t.find('[', 1) == std::string::npos) {
            return false;
        }
    }
    return true;
}

bool AudioInPipeline::VoicedRatioPasses(double voiced_ratio) const {
    return voiced_ratio >= cfg_.min_voiced_ratio;
}

// ---- intake ----

void AudioInPipeline::FeedStereo48k(const int16_t* data,
                                    size_t samples_per_channel) {
    const std::vector<int16_t> mono = DeinterleaveStereo(data, samples_per_channel);
    FeedMono48k(mono.data(), mono.size());
}

// (private pass-through; decimation buffer is shared with mono-48k path)
void AudioInPipeline::FeedMono48k(const int16_t* data, size_t samples) {
    for (size_t i = 0; i < samples; ++i) {
        decim_buf_.push_back(data[i]);
        if (decim_buf_.size() == 3) {
            const int32_t avg = (static_cast<int32_t>(decim_buf_[0]) +
                                 static_cast<int32_t>(decim_buf_[1]) +
                                 static_cast<int32_t>(decim_buf_[2])) / 3;
            ProcessSample(static_cast<int16_t>(avg));
            decim_buf_.clear();
        }
    }
}

void AudioInPipeline::FeedMono16k(const int16_t* data, size_t samples) {
    for (size_t i = 0; i < samples; ++i) {
        ProcessSample(data[i]);
    }
}

// accumulates 16k mono samples into 10 ms frames
void AudioInPipeline::ProcessSample(int16_t s) {
    frame_buf_.push_back(s);
    if (frame_buf_.size() == AudioInConfig::kFrameSamples) {
        ProcessFrame(frame_buf_.data());
        frame_buf_.clear();
    }
}

// ---- segmentation (decision 3 + leg-3 refinements) ----

void AudioInPipeline::ProcessFrame(const int16_t* frame) {
    const bool voiced = vad_ && vad_(frame, AudioInConfig::kFrameSamples);
    const size_t hard_max_frames =
        static_cast<size_t>(cfg_.hard_max_s / kFrameS);
    const size_t end_silence_frames =
        static_cast<size_t>(cfg_.end_silence_s / kFrameS);
    const size_t min_frames =
        static_cast<size_t>(cfg_.min_utterance_s / kFrameS);

    if (current_.empty()) {
        // Segments open on a VOICED frame only (min-utterance guard is
        // the voiced-run length at close).
        if (!voiced) {
            clock_s_ += kFrameS;
            return;
        }
        seg_start_s_ = clock_s_;
        trailing_silence_ = 0;
    }

    // Append frame to open segment.
    const int16_t* f = frame;
    current_.insert(current_.end(), f, f + AudioInConfig::kFrameSamples);
    current_flags_.push_back(voiced ? 1 : 0);
    ++current_frames_;
    if (voiced) {
        ++voiced_frames_;
        trailing_silence_ = 0;
    } else {
        ++trailing_silence_;
    }
    clock_s_ += kFrameS;

    // End-silence close: 1.2 s of trailing unvoiced ends the segment
    // (the separator frames are not part of the utterance).
    if (trailing_silence_ >= end_silence_frames) {
        CloseSegment(false);
        return;
    }

    // Hard-max: gap-aware split (leg-3 refinement — time-blind cuts
    // land mid-sentence; split at the LAST unvoiced gap in the window
    // when one exists late enough to carry a real first part).
    if (current_frames_ >= hard_max_frames) {
        size_t split = current_frames_; // default: exact hard-max cut
        for (size_t i = current_frames_; i > 0; --i) {
            if (current_flags_[i - 1] == 0) { split = i - 1; break; }
        }
        // Guard: the first part must hold a minimum utterance, else the
        // "gap" is stale and an exact cut is the honest boundary.
        size_t first_voiced = 0;
        for (size_t i = 0; i < split; ++i) {
            first_voiced += current_flags_[i];
        }
        if (first_voiced < min_frames) split = current_frames_;

        // Split: emit [0, split), keep [split, end) open.
        const size_t keep = current_frames_ - split;
        std::vector<int16_t> first(current_.begin(),
                                   current_.begin() + split * AudioInConfig::kFrameSamples);
        std::vector<uint8_t> firstFlags(current_flags_.begin(),
                                       current_flags_.begin() + split);
        std::vector<int16_t> rest(current_.begin() + split * AudioInConfig::kFrameSamples,
                                  current_.end());
        std::vector<uint8_t> restFlags(current_flags_.begin() + split,
                                       current_flags_.end());

        size_t firstVoiced = 0, restVoiced = 0;
        for (uint8_t f : firstFlags) firstVoiced += f;
        for (uint8_t f : restFlags) restVoiced += f;

        current_.swap(rest);
        current_flags_.swap(restFlags);
        current_frames_ = keep;
        voiced_frames_ = restVoiced;
        trailing_silence_ = 0;
        for (size_t i = keep; i > 0; --i) {
            if (current_flags_[i - 1] == 0) ++trailing_silence_;
            else break;
        }
        const double firstStart = seg_start_s_;
        seg_start_s_ = clock_s_ - keep * kFrameS;

        // Emit the first part (split_bound=true: suspect boundary —
        // the consumer coalesces at the thread boundary).
        const double firstRatio =
            firstFlags.empty() ? 0.0 : static_cast<double>(firstVoiced) / firstFlags.size();
        Emit(first, firstRatio, firstStart,
             firstStart + firstFlags.size() * kFrameS, true);
        // Note: min-utterance on the FIRST part is enforced by the
        // split guard above; the emitted part is real speech.
    }
}

void AudioInPipeline::CloseSegment(bool split_bound) {
    if (current_.empty()) return;

    // Strip trailing silence (the separator, not the utterance) unless
    // this close is a hard split (split already excluded the gap).
    size_t emit_frames = current_frames_;
    if (!split_bound) {
        emit_frames = current_frames_ > trailing_silence_
                          ? current_frames_ - trailing_silence_
                          : 0;
    }

    const size_t min_frames =
        static_cast<size_t>(cfg_.min_utterance_s / kFrameS);
    size_t voiced = 0;
    for (size_t i = 0; i < emit_frames; ++i) voiced += current_flags_[i];

    std::vector<int16_t> pcm;
    if (emit_frames > 0 && voiced >= min_frames) {
        pcm.assign(current_.begin(),
                   current_.begin() + emit_frames * AudioInConfig::kFrameSamples);
    } else if (emit_frames > 0) { // voiced run below the minimum
        ALOG_INFO("calls", "audio-in: " << emit_frames * kFrameS
                  << " s segment closed with " << voiced
                  << " voiced frames (minimum " << min_frames << "): dropped");
    }
    // else: silence-only — dropped silently.

    // The segment is closed BEFORE it is emitted. Emitting runs the STT
    // callback inline, and a call leave can land while that is in
    // flight: the leaving thread calls Flush(), and with the segment
    // still open here it transcribed and emitted the same speech a
    // second time. Closed first, that flush finds nothing, and the
    // utterance in flight stays the only one.
    const double start_s = seg_start_s_;
    current_.clear();
    current_flags_.clear();
    current_frames_ = 0;
    voiced_frames_ = 0;
    trailing_silence_ = 0;

    if (!pcm.empty()) {
        Emit(pcm, static_cast<double>(voiced) / emit_frames, start_s,
             start_s + emit_frames * kFrameS, split_bound);
    }
}

void AudioInPipeline::Emit(const std::vector<int16_t>& pcm,
                           double voiced_ratio, double start_s,
                           double end_s, bool split_bound) {
    // Every drop below used to be silent: run 19 ended at 0 utterances
    // with no line to say where the speech went.
    if (!VoicedRatioPasses(voiced_ratio)) { // junk (music) filter
        ALOG_INFO("calls", "audio-in: " << end_s - start_s
                  << " s segment dropped before STT: voiced ratio "
                  << voiced_ratio << " below " << cfg_.min_voiced_ratio);
        return;
    }
    if (!stt_) {
        ALOG_WARNING("calls", "audio-in: " << end_s - start_s
                     << " s segment reached STT with no STT callback: dropped");
        return;
    }
    const std::string text = stt_(pcm);
    if (!TextIsSpeech(text, !cfg_.drop_tagged_texts)) {
        ALOG_INFO("calls", "audio-in: STT returned no speech for a "
                  << end_s - start_s << " s segment: dropped");
        return;
    }
    Utterance u;
    u.speaker = speaker_;
    u.text = text;
    u.start_s = start_s;
    u.end_s = end_s;
    u.voiced_ratio = voiced_ratio;
    u.split_bound = split_bound;
    if (on_) on_(u);
}

void AudioInPipeline::Flush() {
    // Call leave: close any open segment as a normal (non-split) close.
    CloseSegment(false);
}

} // namespace animus::kernel::calls
