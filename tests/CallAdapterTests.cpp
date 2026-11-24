// CallAdapterTests — #336 offline unit gates: join-policy decision table
// (decision 2), config-shape validation (A2 allowlist discipline), and the
// verb-surface error paths that need no network. Live join/leave/status
// against production signaling ride the calls probe (animus_calls_probe)
// and the adapter's own live gate — the suite stays network-free by design.

#include "animus_kernel/ChannelAdapters.h"
#include "animus_kernel/ChannelState.h"
#include "animus_kernel/calls/SignalingWire.h"

#include <json/json.h>

#include <iostream>
#include <string>

using animus::kernel::CallAdapter;

namespace {
int failures = 0;

void check(bool ok, const std::string& what) {
    std::cout << (ok ? "[ok] " : "[FAIL] ") << what << "\n";
    if (!ok) ++failures;
}

Json::Value Parse(const std::string& s) {
    Json::CharReaderBuilder rb;
    Json::Value v;
    std::istringstream ss(s);
    std::string err;
    Json::parseFromStream(rb, ss, &v, &err);
    return v;
}
} // namespace

int main() {
    // --- EvaluateJoinPolicy: the decision-2 table
    {
        using JD = CallAdapter::JoinDecision;
        check(CallAdapter::EvaluateJoinPolicy("auto_owner", true) ==
                  JD::AutoAccept,
              "policy auto_owner + owner invite -> auto-accept");
        check(CallAdapter::EvaluateJoinPolicy("auto_owner", false) ==
                  JD::RequireToolCall,
              "policy auto_owner + non-owner invite -> tool call");
        check(CallAdapter::EvaluateJoinPolicy("manual", true) ==
                  JD::RequireToolCall,
              "policy manual + owner invite -> tool call (manual is manual)");
        check(CallAdapter::EvaluateJoinPolicy("manual", false) ==
                  JD::RequireToolCall,
              "policy manual + non-owner -> tool call");
        check(CallAdapter::EvaluateJoinPolicy("", true) == JD::RequireToolCall,
              "empty policy fails closed to tool call");
        check(CallAdapter::EvaluateJoinPolicy("garbage", true) ==
                  JD::RequireToolCall,
              "unknown policy fails closed to tool call");
    }

    // --- ValidateConfigShape: required keys + A2 unknown-key rejection
    {
        std::string err;
        Json::Value good = Parse(
            R"({"nextcloud_url":"https://nextcloud.steadyfort.com",)"
            R"("username":"seat","app_password":"pw",)"
            R"("join_policy":"auto_owner","owner_users":["melvin"]})");
        check(CallAdapter::ValidateConfigShape(good, &err),
              "config shape: valid config accepted");

        Json::Value noUrl = good;
        noUrl.removeMember("nextcloud_url");
        check(!CallAdapter::ValidateConfigShape(noUrl, &err) &&
                  err.find("URL") != std::string::npos,
              "config shape: missing nextcloud_url rejected");

        Json::Value noUser = good;
        noUser.removeMember("username");
        check(!CallAdapter::ValidateConfigShape(noUser, &err) &&
                  err.find("username") != std::string::npos,
              "config shape: missing username rejected");

        Json::Value noPw = good;
        noPw.removeMember("app_password");
        check(!CallAdapter::ValidateConfigShape(noPw, &err) &&
                  err.find("app password") != std::string::npos,
              "config shape: missing app_password rejected");

        Json::Value typo = good;
        typo["join_polciy"] = "auto_owner"; // the typo'd-key bug class
        check(!CallAdapter::ValidateConfigShape(typo, &err) &&
                  err.find("Unknown calls channel config key") !=
                      std::string::npos,
              "config shape: unknown key (typo) fails loudly");

        // agent_id is framework-injected — must be allowed.
        Json::Value withAgent = good;
        withAgent["agent_id"] = "default";
        check(CallAdapter::ValidateConfigShape(withAgent, &err),
              "config shape: agent_id (framework key) accepted");
    }

    // --- SignalingWire: the trickle-candidate shapes that blocked runs
    // 11-18 (#337). `hubAccepts` mirrors the deployed hub's parse rule
    // (api/signaling.go CheckValid): payload.candidate must be a nested
    // object holding a string.
    {
        namespace wire = animus::kernel::calls;
        const auto hubAccepts = [](const Json::Value& payload) {
            const Json::Value& c = payload["candidate"];
            return c.isObject() && c["candidate"].isString();
        };
        const std::string text =
            "candidate:1 1 udp 2130706431 10.0.11.4 54321 typ host";

        const Json::Value env = wire::CandidateEnvelope(text, "1", 1);
        check(env.isObject() && env["candidate"].asString() == text,
              "candidate envelope: nested object holding the candidate string");
        check(env["sdpMid"].asString() == "1" &&
                  env["sdpMLineIndex"].asInt() == 1,
              "candidate envelope: carries sdpMid + sdpMLineIndex for the MCU");

        Json::Value payload(Json::objectValue);
        payload["candidate"] = env;
        check(hubAccepts(payload), "hub rule: the envelope is accepted");

        Json::Value endPayload(Json::objectValue);
        endPayload["candidate"] = wire::EndOfCandidatesEnvelope();
        check(hubAccepts(endPayload) &&
                  endPayload["candidate"]["candidate"].asString().empty(),
              "hub rule: end-of-candidates is the object with an empty string");

        // Regression witness: the shape runs 11-18 actually sent.
        Json::Value bare(Json::objectValue);
        bare["candidate"] = text;
        check(!hubAccepts(bare),
              "hub rule: the bare-string shape of runs 11-18 is rejected");

        wire::RemoteCandidate rc;
        const Json::Value fromMcu = Parse(
            R"({"type":"candidate","payload":{"type":"candidate","candidate":)"
            R"({"sdpMid":"0","sdpMLineIndex":0,)"
            R"("candidate":"candidate:1 1 udp 1 10.0.0.16 50000 typ host"}}})");
        check(wire::ParseRemoteCandidate(fromMcu, &rc) &&
                  !rc.endOfCandidates && rc.mid == "0" &&
                  rc.candidate ==
                      "candidate:1 1 udp 1 10.0.0.16 50000 typ host",
              "inbound: MCU bridge object form is read (dropped unread before)");

        const Json::Value fromPeer = Parse(
            R"({"payload":{"candidate":)"
            R"("candidate:2 1 udp 1 10.0.0.16 50001 typ host",)"
            R"("sdpMid":"1"}})");
        check(wire::ParseRemoteCandidate(fromPeer, &rc) && rc.mid == "1" &&
                  rc.candidate.rfind("candidate:2", 0) == 0,
              "inbound: plain-string peer form is read");

        const Json::Value prefixed = Parse(
            R"({"payload":{"candidate":{"sdpMid":"0",)"
            R"("candidate":" a=candidate:3 1 udp 1 10.0.0.16 50002 typ host "}}})");
        check(wire::ParseRemoteCandidate(prefixed, &rc) &&
                  rc.candidate ==
                      "candidate:3 1 udp 1 10.0.0.16 50002 typ host",
              "inbound: a= prefix and padding are stripped");

        const Json::Value end = Parse(
            R"({"payload":{"type":"candidate","candidate":{"candidate":""}}})");
        check(wire::ParseRemoteCandidate(end, &rc) && rc.endOfCandidates &&
                  rc.candidate.empty(),
              "inbound: empty candidate reads as end-of-candidates");

        const Json::Value none = Parse(R"({"payload":{"type":"candidate"}})");
        check(!wire::ParseRemoteCandidate(none, &rc),
              "inbound: missing candidate reports unusable (logged, not silent)");

        const std::string sdp =
            "v=0\r\no=- 1 1 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n"
            "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\na=mid:0\r\n"
            "m=video 9 UDP/TLS/RTP/SAVPF 96\r\na=mid:1\r\n";
        check(wire::MidToMlineIndex(sdp, "0") == 0 &&
                  wire::MidToMlineIndex(sdp, "1") == 1,
              "m-line index: resolved by mid over CRLF SDP");
        check(wire::MidToMlineIndex(sdp, "7") == -1 &&
                  wire::MidToMlineIndex("", "0") == -1,
              "m-line index: unknown mid / empty SDP -> -1");
    }

    if (failures > 0) {
        std::cout << "CALL ADAPTER TESTS FAILED (" << failures << ")\n";
        return 1;
    }
    std::cout << "ALL CALL ADAPTER TESTS PASSED\n";
    return 0;
}
