// call_adapter_probe — live #336 acceptance gate: drives the real
// CallAdapter (not just the signaling client) through join → status →
// leave against production, proving the verb surface end to end:
//
//   Start (config validated) → JoinCall (OCS mint + REST join + signaling
//   join, seat appears in participants) → CallStatus truth (in_call, self
//   in participant list) → LeaveCall (graceful bye) → status reflects it.
//
// Exit codes: 0 pass; 2 usage/config; 3 Start/validation; 4 JoinCall;
// 5 status wrong after join; 6 LeaveCall; 7 status wrong after leave.
//
// Secrets via --config PATH only (JSON: nextcloud_url, username,
// app_password, join_policy, owner_users); never argv, never logged.
// Usage: animus_call_adapter_probe --config PATH --room ROOMID

#include "animus_kernel/ChannelAdapters.h"
#include "animus_kernel/ChannelContext.h"
#include "animus_kernel/ChannelManager.h"
#include "animus_kernel/ChannelState.h"
#include "animus_kernel/tools/HttpClient.h"

#include <json/json.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>

using animus::kernel::CallAdapter;
using animus::kernel::ChannelContext;
using animus::kernel::ChannelRouter;
using animus::kernel::ChannelState;
using animus::kernel::HttpClient;

namespace {

void Log(const std::string& line) { std::cout << "[probe] " << line << std::endl; }

Json::Value Parse(const std::string& s) {
    Json::CharReaderBuilder rb;
    Json::Value v;
    std::istringstream ss(s);
    std::string err;
    if (!Json::parseFromStream(rb, ss, &v, &err)) {
        Log("config parse error: " + err);
    }
    return v;
}

std::string Trim(std::string s) {
    const char* ws = " \t\r\n";
    const auto b = s.find_first_not_of(ws);
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

} // namespace

int main(int argc, char** argv) {
    std::string configPath, room;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--config" && i + 1 < argc) {
            configPath = argv[++i];
        } else if (std::string(argv[i]) == "--room" && i + 1 < argc) {
            room = argv[++i];
        } else {
            std::cerr << "usage: animus_call_adapter_probe --config PATH "
                         "--room ROOMID\n";
            return 2;
        }
    }
    if (configPath.empty() || room.empty()) {
        std::cerr << "usage: animus_call_adapter_probe --config PATH "
                     "--room ROOMID\n";
        return 2;
    }

    std::ifstream cf(configPath, std::ios::binary);
    if (!cf) {
        Log("FAIL cannot open --config");
        return 2;
    }
    Json::Value config =
        Parse(Trim(std::string((std::istreambuf_iterator<char>(cf)),
                               std::istreambuf_iterator<char>())));
    if (!config.isObject() || config.empty()) {
        Log("FAIL config did not parse as an object");
        return 2;
    }

    HttpClient httpClient;
    ChannelRouter router;
    ChannelContext ctx{httpClient, nullptr, router, nullptr, nullptr};

    // Phase 1: Start with validated config.
    CallAdapter adapter(ctx);
    ChannelState state;
    state.name = "probe-calls";
    state.type = "calls";
    state.config = config;
    std::string err;
    if (!adapter.Start(state, &err)) {
        Log("FAIL Start: " + err);
        return 3;
    }
    Log("PHASE1 PASS adapter started (config validated)");

    // Phase 2: JoinCall through the full seat flow.
    if (!adapter.JoinCall(room, &err)) {
        Log("FAIL JoinCall: " + err);
        return 4;
    }
    Log("PHASE2 PASS JoinCall (OCS mint + REST join + signaling join)");

    // Let the room settle: in-call flags echo back via participants.
    std::this_thread::sleep_for(std::chrono::seconds(4));

    // Phase 3: CallStatus truth.
    Json::Value status = adapter.CallStatus();
    const bool inCall = status.get("in_call", false).asBool();
    const std::string sid = status.get("session_id", "").asString();
    bool selfListed = false;
    const Json::Value& participants = status["participants"];
    if (participants.isArray()) {
        for (const auto& p : participants) {
            if (p.get("session_id", "").asString() == sid) selfListed = true;
        }
    }
    Log(std::string("status: in_call=") + (inCall ? "true" : "false") +
        " state=" + status.get("state", "?").asString() +
        " participants=" + std::to_string(participants.size()));
    if (!inCall || sid.empty()) {
        Log("FAIL status after join: not in_call or no session id");
        adapter.LeaveCall(nullptr);
        return 5;
    }
    if (!selfListed) {
        Log("FAIL status after join: self missing from participants");
        adapter.LeaveCall(nullptr);
        return 5;
    }
    Log("PHASE3 PASS CallStatus truth (in_call, self in participants)");

    // Phase 4: LeaveCall.
    if (!adapter.LeaveCall(&err)) {
        Log("FAIL LeaveCall: " + err);
        return 6;
    }
    Log("PHASE4 PASS LeaveCall (graceful bye)");

    // Phase 5: status reflects the left call.
    std::this_thread::sleep_for(std::chrono::seconds(1));
    Json::Value after = adapter.CallStatus();
    if (after.get("in_call", true).asBool()) {
        Log("FAIL status after leave still in_call");
        return 7;
    }
    Log("PHASE5 PASS status reflects leave");

    Log("PROBE PASS start -> join -> status(truth) -> leave -> status");
    return 0;
}
