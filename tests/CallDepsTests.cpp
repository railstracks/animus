// CallDepsTests — calls-lane dependency probe (ticket sketch #1,
// docs/design/calls-tier1.md). Proves libdatachannel, opus, and fvad
// resolve, link, and behave on every platform we build. This is not the
// adapter's test suite — pipeline behavior arrives with tickets 2-4.
//
// Evidence basis: Phase 0 legs 1-3 (PR #332) validated the protocol and
// pipeline against production; the numbers used here (48 kHz mono Opus
// VOIP, 16 kHz VAD at aggressiveness 2, 20 ms / 30 ms frames) are the
// ones the design doc pins.

#include <rtc/rtc.hpp>

#include <fvad.h>
#include <opus.h>

#include <cstdint>
#include <iostream>
#include <vector>

namespace {
int failures = 0;

void check(bool ok, const char* what) {
    std::cout << (ok ? "[ok] " : "[FAIL] ") << what << "\n";
    if (!ok) ++failures;
}
} // namespace

int main() {
    // --- libdatachannel: PeerConnection constructs cleanly (no ICE servers;
    // no network is touched — state stays New until an actual transport).
    {
        rtc::InitLogger(rtc::LogLevel::None);
        rtc::Configuration cfg;
        rtc::PeerConnection pc(cfg);
        check(pc.state() == rtc::PeerConnection::State::New,
              "libdatachannel: PeerConnection constructs, state=New");
        check(pc.gatheringState() ==
                  rtc::PeerConnection::GatheringState::New,
              "libdatachannel: gathering state=New");
    }

    // --- opus: VOIP-mode encoder/decoder roundtrip, 20 ms silent frame,
    // 48 kHz mono — the exact parameters the call lane uses (Q6 rec).
    {
        int err = 0;
        OpusEncoder* enc =
            opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &err);
        OpusDecoder* dec = opus_decoder_create(48000, 1, &err);
        check(enc != nullptr && dec != nullptr,
              "opus: encoder/decoder create (VOIP, 48 kHz mono)");

        std::vector<opus_int16> pcm(960, 0); // 20 ms @ 48 kHz
        std::vector<unsigned char> packet(4000);
        const int n =
            opus_encode(enc, pcm.data(), 960, packet.data(), packet.size());
        check(n > 0, "opus: encode 20 ms silent frame");

        std::vector<opus_int16> decoded(960);
        const int m =
            opus_decode(dec, packet.data(), n, decoded.data(), 960, 0);
        check(m == 960, "opus: decode roundtrip yields 960 samples");

        opus_encoder_destroy(enc);
        opus_decoder_destroy(dec);
    }

    // --- fvad: WebRTC VAD at the calls default (aggressiveness 2, 16 kHz,
    // 30 ms frames). Silence must report not-voice; the API contract
    // (-1 error / 0 not voice / 1 voice) is what the pipeline branches on.
    {
        Fvad* v = fvad_new();
        check(v != nullptr, "fvad: fvad_new");
        check(fvad_set_sample_rate(v, 16000) == 0,
              "fvad: set_sample_rate(16000)");
        fvad_set_mode(v, 2);
        std::vector<int16_t> frame(480, 0); // 30 ms @ 16 kHz
        const int r = fvad_process(v, frame.data(),
                                   static_cast<int>(frame.size()));
        check(r == 0, "fvad: 30 ms silence -> not voice (mode 2)");
        fvad_free(v);
    }

    if (failures > 0) {
        std::cout << "CALL DEPS TESTS FAILED (" << failures << ")\n";
        return 1;
    }
    std::cout << "ALL CALL DEPS TESTS PASSED\n";
    return 0;
}
