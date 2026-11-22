// ============================================================================
// Live media gate tool (#336/#337) — drives the FULL CallAdapter against a
// real Nextcloud Talk deployment end-to-end: OCS hello-v2 minting, REST
// room pre-join, signaling join, per-publisher WebRTC (offer/candidate),
// Opus decode, VAD segmentation, whisper STT, utterance dispatch.
//
// The acceptance gate: speaker-attributed utterance events landing in the
// dispatch callback while a human speaks on the other seat.
//
// Secrets arrive via the --config file (600-mode), never argv; --stt-url is
// a plain URL (tunnel/relay target), not a secret.
//
// Exit codes: 0 = clean join+leave (utterances may be zero — solo mode);
//             2 = usage/config/start failure;
//             5 = join failure; 6 = leave failure.
// ============================================================================

#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include <json/json.h>

#include "animus_kernel/ChannelAdapters.h"
#include "animus_kernel/ChannelContext.h"
#include "animus_kernel/ChannelManager.h"
#include "animus_kernel/ChannelState.h"
#include "animus_kernel/Log.h"
#include "animus_kernel/tools/HttpClient.h"

using animus::kernel::CallAdapter;
using animus::kernel::ChannelContext;
using animus::kernel::ChannelReplyTarget;
using animus::kernel::ChannelRouter;
using animus::kernel::ChannelState;
using animus::kernel::HttpClient;

namespace {

std::atomic<bool> g_stop{false};
void OnSigint(int) { g_stop = true; }

bool LoadJson(const std::string& path, Json::Value* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    Json::CharReaderBuilder b;
    std::string errs;
    return Json::parseFromStream(b, f, out, &errs);
}

void Usage() {
    std::cerr
        << "usage: animus_calls_media_probe --config <channel-config.json>"
           " --room <token> [--stt-url <url>] [--duration <seconds>]\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string configPath;
    std::string room;
    std::string sttUrl;
    int durationS = 180;

    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        const bool hasValue = i + 1 < argc;
        if (flag == "--config" && hasValue) {
            configPath = argv[++i];
        } else if (flag == "--room" && hasValue) {
            room = argv[++i];
        } else if (flag == "--stt-url" && hasValue) {
            sttUrl = argv[++i];
        } else if (flag == "--duration" && hasValue) {
            durationS = std::atoi(argv[++i]);
        } else {
            Usage();
            return 2;
        }
    }
    if (configPath.empty() || room.empty()) {
        Usage();
        return 2;
    }

    Json::Value config;
    if (!LoadJson(configPath, &config)) {
        std::cerr << "FAIL cannot read/parse config: " << configPath << "\n";
        return 2;
    }
    if (!sttUrl.empty()) config["stt_url"] = sttUrl;
    if (!config.isMember("stun_server")) {
        config["stun_server"] = "stun:steadyfort.com:3478";
    }

    int utterances = 0;
    HttpClient http;
    ChannelRouter router;
    ChannelContext ctx{
        http,
        nullptr,
        router,
        [&utterances](const std::string& agentId, const std::string& sessionKey,
                      const std::string& message, const std::string& sessionType,
                      const ChannelReplyTarget& replyTarget,
                      const std::string& metadata) {
            ++utterances;
            std::cout << "\n=== UTTERANCE DISPATCH #" << utterances << " ===\n"
                      << "agent=" << agentId << " session=" << sessionKey
                      << " type=" << sessionType << "\n"
                      << "reply.channel=" << replyTarget.channel_name
                      << " peer=" << replyTarget.peer_id << "\n"
                      << "metadata=" << metadata << "\n"
                      << "message:\n" << message << "\n=== END DISPATCH ==="
                      << std::endl;
        },
        [](const std::string& agentId, const std::string& sessionKey,
           const std::string& message, const std::string& sessionType) {
            std::cout << "[log-lane] agent=" << agentId
                      << " session=" << sessionKey << " type=" << sessionType
                      << " msg=" << message << std::endl;
        },
        nullptr};

    CallAdapter adapter(ctx);
    ChannelState state;
    state.name = "calls-media-probe";
    state.type = "calls";
    state.config = config;
    std::string err;
    if (!adapter.Start(state, &err)) {
        std::cerr << "FAIL start: " << err << "\n";
        return 2;
    }

    std::signal(SIGINT, OnSigint);
    std::cout << "joining room " << room << " (duration " << durationS
              << "s, Ctrl-C to leave early)..." << std::endl;
    if (!adapter.JoinCall(room, &err)) {
        std::cerr << "FAIL join: " << err << "\n";
        return 5;
    }

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(durationS);
    int lastBeat = -1;
    while (!g_stop && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const int elapsed = durationS - static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(
                deadline - std::chrono::steady_clock::now()).count());
        if (elapsed / 15 != lastBeat && elapsed % 15 == 0) {
            lastBeat = elapsed / 15;
            std::cout << "[probe] t+" << elapsed << "s, dispatches="
                      << utterances << std::endl;
        }
    }

    if (g_stop) std::cout << "[probe] SIGINT — leaving" << std::endl;
    else std::cout << "[probe] duration elapsed — leaving" << std::endl;
    if (!adapter.LeaveCall(&err)) {
        std::cerr << "FAIL leave: " << err << "\n";
        return 6;
    }
    std::cout << "GATE-SOLO-CLEAN leave; total utterance dispatches: "
              << utterances << std::endl;
    return 0;
}
