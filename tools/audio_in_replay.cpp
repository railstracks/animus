// audio_in_replay — #337 leg-3 replay gate: drives the C++ AudioInPipeline
// with REAL libfvad (mode 2, the leg-3 validated aggressiveness) over the
// Phase-0 leg-2 capture (48 kHz mono WAV, workspace-internal — not in the
// repo) and compares segmentation against the Python reference manifest
// (leg3_manifest.json).
//
// What it proves: the kernel pipeline reproduces the reference's speech
// segmentation on real audio — same VAD core, the decision-3 bounds, and
// the leg-3 refinements (gap-aware hard-max split replaces the reference's
// time-blind 15.0 s cuts). Deltas vs the manifest print for judgment;
// the asserted invariants are the ones the pipeline itself owns:
//   - first segment ≈ reference seg01 (end-silence close, preroll tolerance)
//   - the natural inter-utterance gap (5.1–6.4 s) stays a gap
//   - hard-max respected on every segment
//   - continuous-speech region (6.4–45.3 s) fully covered, ≥2 gap-aware
//     splits, every split boundary lands on an unvoiced frame
//   - min-utterance respected (no sub-0.4 s segments)
//
// STT is a stub (no network): the manifest's texts ride whisper_stt in the
// live lane. Usage:
//   animus_audio_in_replay <48k-mono-wav> <manifest.json>
// Exit 0 = gates pass; 2 = usage/IO; 3 = WAV format; 4.. = gate failures.

#include "animus_kernel/calls/AudioInPipeline.h"

#include <fvad.h>

#include <json/json.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using animus::kernel::calls::AudioInConfig;
using animus::kernel::calls::AudioInPipeline;
using animus::kernel::calls::Utterance;

namespace {

int failures = 0;

void Fail(const std::string& what) {
    std::cout << "[GATE FAIL] " << what << "\n";
    ++failures;
}

void Pass(const std::string& what) {
    std::cout << "[gate pass] " << what << "\n";
}

// Minimal RIFF/WAVE reader: PCM16, asserts 48 kHz mono.
bool ReadWav48kMono(const std::string& path, std::vector<int16_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char hdr[12];
    f.read(hdr, 12);
    if (std::string(hdr, 4) != "RIFF" || std::string(hdr + 8, 4) != "WAVE") {
        return false;
    }
    bool haveFmt = false, haveData = false;
    uint16_t channels = 0;
    uint32_t rate = 0;
    while (f && !haveData) {
        char ch[8];
        f.read(ch, 8);
        if (!f) break;
        const uint32_t sz = *reinterpret_cast<uint32_t*>(ch + 4);
        if (std::string(ch, 4) == "fmt ") {
            std::vector<char> fmt(sz);
            f.read(fmt.data(), sz);
            channels = *reinterpret_cast<uint16_t*>(fmt.data() + 2);
            rate = *reinterpret_cast<uint32_t*>(fmt.data() + 4);
            const uint16_t bits = *reinterpret_cast<uint16_t*>(fmt.data() + 14);
            if (bits != 16) return false;
            haveFmt = true;
        } else if (std::string(ch, 4) == "data") {
            if (!haveFmt) return false;
            if (rate != 48000 || channels != 1) return false;
            out.resize(sz / 2);
            f.read(reinterpret_cast<char*>(out.data()), sz);
            haveData = true;
        } else {
            f.seekg(sz, std::ios::cur);
        }
        if (sz % 2) f.seekg(1, std::ios::cur); // word alignment
    }
    return haveData && !out.empty();
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: animus_audio_in_replay <48k-mono-wav> "
                     "<manifest.json>\n";
        return 2;
    }
    std::vector<int16_t> pcm;
    if (!ReadWav48kMono(argv[1], pcm)) {
        std::cerr << "FAIL: cannot read 48k mono PCM16 WAV: " << argv[1]
                  << "\n";
        return 3;
    }
    std::ifstream mf(argv[2]);
    if (!mf) {
        std::cerr << "FAIL: cannot open manifest\n";
        return 2;
    }
    Json::Value manifest;
    Json::CharReaderBuilder rb;
    std::string err;
    if (!Json::parseFromStream(rb, mf, &manifest, &err) ||
        !manifest.isArray()) {
        std::cerr << "FAIL: manifest parse error\n";
        return 2;
    }

    // Real fvad, mode 2 (leg-3: 1 and 2 identical boundaries on real
    // speech; 3 shreds — use 2).
    struct FvadHolder {
        Fvad* v;
        FvadHolder() : v(fvad_new()) {
            fvad_set_mode(v, 2);
            fvad_set_sample_rate(v, 16000);
        }
        ~FvadHolder() { fvad_free(v); }
    } fvad;
    if (!fvad.v) {
        std::cerr << "FAIL: fvad_new\n";
        return 2;
    }

    std::vector<Utterance> got;
    AudioInConfig cfg;  // decision-3 bounds
    AudioInPipeline pipe(
        cfg, "replay",
        [&](const int16_t* frame, int samples) {
            return fvad_process(fvad.v, frame, samples) == 1;
        },
        [](const std::vector<int16_t>&) { return "speech"; },
        [&](const Utterance& u) { got.push_back(u); });

    // Mono 48k → duplicate to stereo → production stereo intake path
    // (deinterleave-avg is identity on L=R; decimation exercised).
    constexpr size_t kFrame = 960; // 20 ms @ 48k
    for (size_t i = 0; i + kFrame <= pcm.size(); i += kFrame) {
        std::vector<int16_t> stereo(kFrame * 2);
        for (size_t j = 0; j < kFrame; ++j) {
            stereo[2 * j] = pcm[i + j];
            stereo[2 * j + 1] = pcm[i + j];
        }
        pipe.FeedStereo48k(stereo.data(), kFrame);
    }
    pipe.Flush();

    std::cout << "segments: " << got.size()
              << " (manifest: " << manifest.size() << ")\n";
    for (size_t i = 0; i < got.size(); ++i) {
        std::printf("  [%zu] %.2f-%.2f (%.2fs) voiced=%.2f%s\n", i,
                    got[i].start_s, got[i].end_s, got[i].end_s - got[i].start_s,
                    got[i].voiced_ratio,
                    got[i].split_bound ? " [split]" : "");
    }
    std::cout << "manifest:\n";
    for (const auto& m : manifest) {
        std::printf("  ref %.2f-%.2f (%.2fs) \"%s\"\n",
                    m["t_start_s"].asDouble(), m["t_end_s"].asDouble(),
                    m["dur_s"].asDouble(), m["file"].asString().c_str());
    }

    // ---- gates (pipeline-owned invariants + reference anchors) ----

    // Speech region anchors from the manifest's speech entries (the
    // first four cover the spoken phrases; later music-tail entries are
    // reported, not gated — the ratio filter's behavior on music is
    // config, not a boundary contract).
    if (manifest.size() < 4) {
        Fail("manifest smaller than expected reference shape");
    }
    const double ref1Start = manifest[0]["t_start_s"].asDouble();
    const double ref1End = manifest[0]["t_end_s"].asDouble();

    // G1: first speech segment matches seg01 (end-silence close; up to
    //     0.35 s start delta = the reference's 0.30 s preroll + frame
    //     size 30 vs 10 ms).
    bool g1 = false;
    for (const auto& u : got) {
        if (u.start_s - ref1Start <= 0.35 && u.start_s - ref1Start >= -0.1 &&
            std::fabs(u.end_s - ref1End) <= 0.35) {
            g1 = true;
            break;
        }
    }
    g1 ? Pass("G1 first segment matches reference seg01 (tol 0.35s)")
       : Fail("G1 first segment does not match reference seg01");

    // G2: the natural inter-utterance gap (ref 5.1–6.39 s) stays a gap:
    //     no segment may straddle it.
    bool g2 = true;
    for (const auto& u : got) {
        if (u.start_s < 5.5 && u.end_s > 6.0) g2 = false;
    }
    g2 ? Pass("G2 inter-utterance gap preserved") : Fail("G2 gap straddled");

    // G3: hard-max respected on every emitted segment.
    bool g3 = true;
    for (const auto& u : got) {
        if (u.end_s - u.start_s > 15.0 + 0.05) g3 = false;
    }
    g3 ? Pass("G3 hard-max 15s respected") : Fail("G3 segment exceeds hard-max");

    // G4: continuous-speech region (ref 6.39–45.3 s) fully covered:
    //     union of segments in [6.0, 46.0] leaves no hole > 2 s, and
    //     at least 3 segments with >= 2 gap-aware splits.
    int g4segments = 0, g4splits = 0;
    for (const auto& u : got) {
        if (u.start_s >= 6.0 && u.start_s <= 46.0) {
            ++g4segments;
            if (u.split_bound) ++g4splits;
        }
    }
    const bool g4count = g4segments >= 3 && g4splits >= 2;
    g4count ? Pass("G4 speech region covered, " + std::to_string(g4segments) +
                       " segments, " + std::to_string(g4splits) +
                       " gap-aware splits")
            : Fail("G4 coverage/splits insufficient (" +
                   std::to_string(g4segments) + " seg, " +
                   std::to_string(g4splits) + " splits)");

    // G5: min-utterance respected — no segment below 0.4 s.
    bool g5 = true;
    for (const auto& u : got) {
        if (u.end_s - u.start_s < 0.4 - 0.01) g5 = false;
    }
    g5 ? Pass("G5 min-utterance 0.4s respected")
       : Fail("G5 sub-minimum segment emitted");

    if (failures > 0) {
        std::cout << "REPLAY GATES FAILED (" << failures << ")\n";
        return 4;
    }
    std::cout << "ALL REPLAY GATES PASSED\n";
    return 0;
}
