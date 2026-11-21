// SignalingClientTests — pure protocol-layer tests for #335. No network:
// the builders, token computation, URL parsing, backoff policy, and
// participant bookkeeping are exercised with wire shapes captured in
// Phase 0 (legs 1–2) plus a published HMAC-SHA256 vector. Live solo-join
// integration (leg-1 flow from the kernel) is the ticket's follow-up gate.

#include "animus_kernel/calls/SignalingClient.h"

#include <json/json.h>

#include <iostream>
#include <map>
#include <string>

using animus::kernel::calls::SignalingClient;

namespace {
int failures = 0;

void check(bool ok, const char* what) {
    std::cout << (ok ? "[ok] " : "[FAIL] ") << what << "\n";
    if (!ok) ++failures;
}

Json::Value Parse(const std::string& s) {
    Json::CharReaderBuilder rb;
    Json::Value v;
    std::istringstream ss(s);
    std::string err;
    if (!Json::parseFromStream(rb, ss, &v, &err)) {
        std::cout << "[parse error] " << err << "\n";
    }
    return v;
}
} // namespace

int main() {
    // --- ComputeInternalToken: published HMAC-SHA256 vector
    // (key="key", message="The quick brown fox jumps over the lazy dog").
    {
        const std::string token = SignalingClient::ComputeInternalToken(
            "The quick brown fox jumps over the lazy dog", "key");
        check(token ==
                  "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc"
                  "2d1a3cd8",
              "ComputeInternalToken: known HMAC-SHA256 vector");
        const std::string again = SignalingClient::ComputeInternalToken(
            "The quick brown fox jumps over the lazy dog", "key");
        check(token == again, "ComputeInternalToken: deterministic");
    }

    // --- ParseServerUrl
    {
        auto ep = SignalingClient::ParseServerUrl(
            "wss://sig.example.com:8443/standalone-signaling/spreed");
        check(ep.client_url == "wss://sig.example.com:8443" &&
                  ep.path == "/standalone-signaling/spreed",
              "ParseServerUrl: wss with port and path");
        ep = SignalingClient::ParseServerUrl("wss://sig.example.com");
        check(ep.client_url == "wss://sig.example.com" && ep.path == "/",
              "ParseServerUrl: bare host, path defaults to /");
        ep = SignalingClient::ParseServerUrl("https://sig.example.com/sig");
        check(ep.client_url == "wss://sig.example.com" && ep.path == "/sig",
              "ParseServerUrl: https normalized to wss");
        ep = SignalingClient::ParseServerUrl("sig.example.com/sig");
        check(ep.client_url == "wss://sig.example.com" && ep.path == "/sig",
              "ParseServerUrl: missing scheme defaults to wss");
    }

    // --- BuildHelloInternal
    {
        const auto v = Parse(SignalingClient::BuildHelloInternal(
            "r4nd0m", "t0k3n", "https://nextcloud/", "id-1"));
        check(v["type"].asString() == "hello" &&
                  v["id"].asString() == "id-1" &&
                  v["hello"]["version"].asString() == "1.0" &&
                  v["hello"]["auth"]["type"].asString() == "internal" &&
                  v["hello"]["auth"]["params"]["random"].asString() ==
                      "r4nd0m" &&
                  v["hello"]["auth"]["params"]["token"].asString() ==
                      "t0k3n" &&
                  v["hello"]["auth"]["params"]["backend"].asString() ==
                      "https://nextcloud/",
              "BuildHelloInternal: v1.0 internal-auth shape");
    }

    // --- BuildHelloResume (reconnect re-attach by server resumeid)
    {
        const auto v = Parse(
            SignalingClient::BuildHelloResume("rs-1", "id-1b"));
        check(v["type"].asString() == "hello" &&
                  v["id"].asString() == "id-1b" &&
                  v["hello"]["version"].asString() == "1.0" &&
                  v["hello"]["auth"]["type"].asString() == "resume" &&
                  v["hello"]["auth"]["resumeid"].asString() == "rs-1",
              "BuildHelloResume: resume-auth shape");
    }

    // --- BuildRoomJoin: fresh (empty session) and resume (prior id)
    {
        const auto fresh = Parse(
            SignalingClient::BuildRoomJoin("room1", "", "id-2"));
        check(fresh["type"].asString() == "room" &&
                  fresh["room"]["roomid"].asString() == "room1" &&
                  fresh["room"]["sessionid"].asString() == "",
              "BuildRoomJoin: fresh join carries empty sessionid");
        const auto resume = Parse(
            SignalingClient::BuildRoomJoin("room1", "oldsid", "id-3"));
        check(resume["room"]["sessionid"].asString() == "oldsid",
              "BuildRoomJoin: resume carries prior sessionid");
    }

    // --- BuildInCall (nested internal.incall.incall, per leg-1 wire)
    {
        const auto v = Parse(SignalingClient::BuildInCall(7, "id-4"));
        check(v["type"].asString() == "internal" &&
                  v["internal"]["type"].asString() == "incall" &&
                  v["internal"]["incall"]["incall"].asInt() == 7,
              "BuildInCall: nested incall envelope");
    }

    // --- BuildBye
    {
        const auto v = Parse(SignalingClient::BuildBye("id-5"));
        check(v["type"].asString() == "bye" && v["bye"].isObject(),
              "BuildBye: shape");
    }

    // --- BuildSessionMessage (recipient envelope, data preserved)
    {
        Json::Value data;
        data["type"] = "requestoffer";
        data["roomType"] = "video";
        const auto v = Parse(SignalingClient::BuildSessionMessage(
            "peer-1", data, "id-6"));
        check(v["type"].asString() == "message" &&
                  v["message"]["recipient"]["type"].asString() ==
                      "session" &&
                  v["message"]["recipient"]["sessionid"].asString() ==
                      "peer-1" &&
                  v["message"]["data"]["type"].asString() ==
                      "requestoffer",
              "BuildSessionMessage: session recipient + payload");
    }

    // --- BackoffSeconds: base*2^n clamped to max
    {
        check(SignalingClient::BackoffSeconds(0, 1.0, 30.0) == 1.0,
              "BackoffSeconds: attempt 0 -> base");
        check(SignalingClient::BackoffSeconds(1, 1.0, 30.0) == 2.0,
              "BackoffSeconds: attempt 1 -> 2x");
        check(SignalingClient::BackoffSeconds(2, 1.0, 30.0) == 4.0,
              "BackoffSeconds: attempt 2 -> 4x");
        check(SignalingClient::BackoffSeconds(10, 1.0, 30.0) == 30.0,
              "BackoffSeconds: clamped to max");
        check(SignalingClient::BackoffSeconds(-3, 1.0, 30.0) == 1.0,
              "BackoffSeconds: negative attempt -> base");
    }

    // --- ApplyParticipantEvent: participants-update (camelCase wire)
    {
        std::map<std::string, SignalingClient::Participant> ps;
        const auto ev = Parse(
            R"({"target":"participants","update":{"users":[)"
            R"({"sessionId":"u-7gby9I","userId":"melvin","inCall":3},)"
            R"({"sessionId":"anon-1","userId":"","inCall":0}]}})");
        SignalingClient::ApplyParticipantEvent(ev, ps);
        check(ps.size() == 2 && ps.at("u-7gby9I").user_id == "melvin" &&
                  ps.at("u-7gby9I").in_call == 3 &&
                  ps.at("anon-1").in_call == 0,
              "ApplyParticipantEvent: participants-update");

        // Room join (lowercase wire) adds a session.
        const auto join = Parse(
            R"({"target":"room","type":"join","join":[)"
            R"({"sessionid":"new-1","userid":"petrel"}]})");
        SignalingClient::ApplyParticipantEvent(join, ps);
        check(ps.count("new-1") == 1 && ps.at("new-1").user_id == "petrel",
              "ApplyParticipantEvent: room join adds");

        // Leave removes (string array form).
        const auto leave =
            Parse(R"({"target":"room","type":"leave","leave":["new-1"]})");
        SignalingClient::ApplyParticipantEvent(leave, ps);
        check(ps.count("new-1") == 0,
              "ApplyParticipantEvent: leave removes");

        // Update overwrites in-place (flags change).
        const auto upd = Parse(
            R"({"target":"participants","update":{"users":[)"
            R"({"sessionId":"u-7gby9I","userId":"melvin","inCall":0}]}})");
        SignalingClient::ApplyParticipantEvent(upd, ps);
        check(ps.at("u-7gby9I").in_call == 0,
              "ApplyParticipantEvent: update overwrites");
    }

    // --- DropTransport: safe no-op before Start (no worker loop yet).
    // The live-path proof (drop → reconnect → resume) is
    // tools/calls_probe.cpp against production signaling; this pins
    // only the degenerate case.
    {
        animus::kernel::calls::SignalingConfig cfg;
        cfg.server_url = "wss://sig.example.invalid";
        cfg.backend_url = "https://nc.example.invalid/";
        cfg.internal_secret = "s3cret";
        cfg.room_id = "room1";
        SignalingClient client(cfg);
        client.DropTransport();
        check(client.state() == SignalingClient::State::Idle,
              "DropTransport: safe no-op before Start");
    }

    if (failures > 0) {
        std::cout << "SIGNALING CLIENT TESTS FAILED (" << failures << ")\n";
        return 1;
    }
    std::cout << "ALL SIGNALING CLIENT TESTS PASSED\n";
    return 0;
}
