// AudioInPipelineTests — #337 offline unit gates: pure helpers
// (stereo deinterleave, 3:1 decimation, tagged-text junk filter) and
// the segmentation state machine (open/close, min-utterance drop,
// end-silence close, hard-max gap-aware split, flush) driven with a
// deterministic VAD stub. The STT seam echoes PCM length; no network,
// no codec, no real audio in ctest (leg-3 replay rides the probe).

#include "animus_kernel/calls/AudioInPipeline.h"

#include <iostream>
#include <string>
#include <vector>

using animus::kernel::calls::AudioInConfig;
using animus::kernel::calls::AudioInPipeline;
using animus::kernel::calls::Utterance;

namespace {
int failures = 0;

void check(bool ok, const std::string& what) {
    std::cout << (ok ? "[ok] " : "[FAIL] ") << what << "\n";
    if (!ok) ++failures;
}

// Deterministic VAD stub: voiced exactly at the given frame indices.
class FrameVad {
public:
    std::vector<int> voiced_frames;
    bool operator()(const int16_t*, int) {
        const int n = called_++;
        for (int f : voiced_frames) {
            if (f == n) return true;
        }
        return false;
    }

private:
    int called_ = 0;
};

struct Sink {
    std::vector<Utterance> got;
    void operator()(const Utterance& u) { got.push_back(u); }
};

AudioInConfig TestConfig() {
    AudioInConfig c;
    c.min_utterance_s = 0.4;   // 40 frames
    c.end_silence_s = 1.2;     // 120 frames
    c.hard_max_s = 15.0;       // 1500 frames
    c.min_voiced_ratio = 0.5;
    c.drop_tagged_texts = true;
    return c;
}

// Feed n mono 16k frames (each 160 samples of a fixed pattern).
void FeedFrames(AudioInPipeline& p, int n, int16_t sample = 3000) {
    static int16_t buf[AudioInConfig::kFrameSamples];
    for (int i = 0; i < AudioInConfig::kFrameSamples; ++i) buf[i] = sample;
    for (int f = 0; f < n; ++f) p.FeedMono16k(buf, AudioInConfig::kFrameSamples);
}

} // namespace

int main() {
    // --- DeinterleaveStereo: averaged pairs, correct count
    {
        std::vector<int16_t> stereo = {100, 200, 300, 600}; // 2 frames, L/R
        auto mono = AudioInPipeline::DeinterleaveStereo(stereo.data(), 2);
        check(mono.size() == 2 && mono[0] == 150 && mono[1] == 450,
              "DeinterleaveStereo: averages L/R pairs");
        // Leg-2 anti-witness: naive flatten would give {100, 300} —
        // wrong signal and half speed on playback.
        check(mono[0] != stereo[0] || mono[1] != stereo[2],
              "DeinterleaveStereo: not a naive flatten");
    }

    // --- Decimate3to1: averaged triples, exact count
    {
        std::vector<int16_t> in = {3, 6, 9, 12, 15, 18, 100}; // 7 samples
        auto out = AudioInPipeline::Decimate3to1(in.data(), in.size());
        check(out.size() == 2 && out[0] == 6 && out[1] == 15,
              "Decimate3to1: averages triples, drops remainder");
    }

    // --- TextIsSpeech junk filter
    {
        check(AudioInPipeline::TextIsSpeech("hello there", false),
              "TextIsSpeech: plain text kept");
        check(!AudioInPipeline::TextIsSpeech("", false),
              "TextIsSpeech: empty dropped");
        check(!AudioInPipeline::TextIsSpeech("   ", false),
              "TextIsSpeech: whitespace-only dropped");
        check(!AudioInPipeline::TextIsSpeech("[Music]", false),
              "TextIsSpeech: [Music] tag dropped");
        check(!AudioInPipeline::TextIsSpeech(" [BLANK_AUDIO] ", false),
              "TextIsSpeech: [BLANK_AUDIO] tag dropped (trimmed)");
        check(AudioInPipeline::TextIsSpeech("The [Music] was loud", false),
              "TextIsSpeech: tag WITH surrounding text kept");
        check(AudioInPipeline::TextIsSpeech("[Music]", true),
              "TextIsSpeech: tag kept when allow_tagged");
    }

    // --- Segmentation: voiced run + end silence -> one utterance
    {
        FrameVad vad;
        std::vector<int> voiced;
        for (int i = 0; i < 60; ++i) voiced.push_back(i); // 0.6 s voiced
        vad.voiced_frames = voiced;
        Sink sink;
        AudioInPipeline p(TestConfig(), "speaker-a", vad,
                          [](const std::vector<int16_t>& pcm) {
                              return "transcribed (" +
                                     std::to_string(pcm.size()) + ")";
                          },
                          std::ref(sink));
        FeedFrames(p, 60 + 120); // voiced run + 1.2 s silence
        check(sink.got.size() == 1,
              "Segmentation: one utterance after end-silence close");
        if (sink.got.size() == 1) {
            const auto& u = sink.got[0];
            check(u.speaker == "speaker-a",
                  "Segmentation: speaker attributed (Tier 0)");
            check(!u.split_bound, "Segmentation: clean close not split");
            check(u.text.find("transcribed") != std::string::npos,
                  "Segmentation: STT seam output carried through");
        }
    }

    // --- Min-utterance drop: 0.2 s voiced run emits nothing
    {
        FrameVad vad;
        std::vector<int> voiced;
        for (int i = 0; i < 20; ++i) voiced.push_back(i); // 0.2 s
        vad.voiced_frames = voiced;
        Sink sink;
        AudioInPipeline p(TestConfig(), "s", vad,
                          [](const std::vector<int16_t>&) { return "x"; },
                          std::ref(sink));
        FeedFrames(p, 20 + 130);
        check(sink.got.empty(),
              "Segmentation: sub-minimum voiced run dropped");
    }

    // --- Segments only open on voiced frames: leading silence ignored
    {
        FrameVad vad;
        std::vector<int> voiced;
        for (int i = 0; i < 50; ++i) voiced.push_back(200 + i); // after 2 s
        vad.voiced_frames = voiced;
        Sink sink;
        AudioInPipeline p(TestConfig(), "s", vad,
                          [](const std::vector<int16_t>&) { return "x"; },
                          std::ref(sink));
        FeedFrames(p, 200);       // leading silence
        FeedFrames(p, 50 + 120);  // voiced + close
        check(sink.got.size() == 1,
              "Segmentation: leading silence does not open a segment");
        if (!sink.got.empty()) {
            check(sink.got[0].start_s >= 2.0,
                  "Segmentation: start time counts from pipeline start");
        }
    }

    // --- Hard-max gap-aware split: 15 s of continuous voiced splits at
    //     the last unvoiced gap when one exists; both parts real speech
    {
        FrameVad vad;
        std::vector<int> voiced;
        for (int i = 0; i < 900; ++i) voiced.push_back(i);    // 9 s speech
        // gap 900..999 (1 s), then speech 1000..1499 (5 s) = 15 s total
        for (int i = 1000; i < 1500; ++i) voiced.push_back(i);
        vad.voiced_frames = voiced;
        Sink sink;
        AudioInPipeline p(TestConfig(), "s", vad,
                          [](const std::vector<int16_t>&) { return "x"; },
                          std::ref(sink));
        FeedFrames(p, 1500);
        check(sink.got.size() == 1,
              "Split: first part emitted at hard-max (gap-aware)");
        if (!sink.got.empty()) {
            check(sink.got[0].split_bound,
                  "Split: emitted part carries suspect-boundary flag");
            check(sink.got[0].start_s < 9.0,
                  "Split: first part starts before the gap");
        }
        // Flush the remainder (5 s speech, still open).
        p.Flush();
        check(sink.got.size() == 2,
              "Split: remainder survives as its own segment (coalesce at thread)");
        if (sink.got.size() == 2) {
            check(!sink.got[1].split_bound,
                  "Split: flushed remainder is a normal close");
        }
    }

    // --- Junk filter: voiced-ratio below floor drops the event
    {
        // Alternating 1-voiced/1-unvoiced frames for 80 frames = 0.5
        // ratio at the exact floor passes; sparser fails.
        struct HalfVad {
            int n = 0;
            bool operator()(const int16_t*, int) { return (n++ % 4) < 1; }
        };
        HalfVad vad; // 25% voiced
        Sink sink;
        AudioInPipeline p(TestConfig(), "s", vad,
                          [](const std::vector<int16_t>&) { return "x"; },
                          std::ref(sink));
        FeedFrames(p, 60 + 120);
        check(sink.got.empty(),
              "Junk filter: voiced-ratio 0.25 below 0.5 floor dropped");
    }

    // --- Tagged STT result dropped even when audio passed the filters
    {
        FrameVad vad;
        std::vector<int> voiced;
        for (int i = 0; i < 60; ++i) voiced.push_back(i);
        vad.voiced_frames = voiced;
        Sink sink;
        AudioInPipeline p(TestConfig(), "s", vad,
                          [](const std::vector<int16_t>&) { return "[Music]"; },
                          std::ref(sink));
        FeedFrames(p, 60 + 120);
        check(sink.got.empty(),
              "Junk filter: tagged STT text dropped (leg-3 evidence)");
    }

    // --- Flush closes an open segment at call leave
    {
        FrameVad vad;
        std::vector<int> voiced;
        for (int i = 0; i < 60; ++i) voiced.push_back(i);
        vad.voiced_frames = voiced;
        Sink sink;
        AudioInPipeline p(TestConfig(), "s", vad,
                          [](const std::vector<int16_t>&) { return "last words"; },
                          std::ref(sink));
        FeedFrames(p, 60); // no end silence — segment still open
        check(p.SegmentOpen(), "Flush precondition: segment open");
        p.Flush();
        check(sink.got.size() == 1 &&
                  sink.got[0].text == "last words",
              "Flush: open segment closed and emitted at leave");
    }

    // --- Leave landing during an in-flight transcription emits the
    //     speech once, not twice. The leaving thread calls Flush() while
    //     the audio thread is still inside the STT callback; driven here
    //     re-entrantly from that callback.
    {
        FrameVad vad;
        std::vector<int> voiced;
        for (int i = 0; i < 60; ++i) voiced.push_back(i);
        vad.voiced_frames = voiced;
        Sink sink;
        AudioInPipeline* pipe = nullptr;
        int sttCalls = 0;
        AudioInPipeline p(TestConfig(), "s", vad,
                          [&](const std::vector<int16_t>&) {
                              // The leave lands inside the FIRST
                              // transcription only. A second one is the
                              // bug; recursing from it would overflow
                              // the stack instead of failing the test.
                              if (++sttCalls == 1) pipe->Flush();
                              return "once";
                          },
                          std::ref(sink));
        pipe = &p;
        FeedFrames(p, 60 + 120); // voiced run + end silence -> close
        check(sttCalls == 1,
              "Leave during in-flight STT: one transcription");
        check(sink.got.size() == 1 && sink.got[0].text == "once",
              "Leave during in-flight STT: utterance emitted once");
        check(!p.SegmentOpen(),
              "Leave during in-flight STT: segment stays closed");
    }

    if (failures > 0) {
        std::cout << "AUDIO IN PIPELINE TESTS FAILED (" << failures << ")\n";
        return 1;
    }
    std::cout << "ALL AUDIO IN PIPELINE TESTS PASSED\n";
    return 0;
}
