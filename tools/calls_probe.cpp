// calls_probe — live standalone-signaling gate for #335 (Calls Tier 1).
//
// Operational tool, not a test suite: drives the kernel's own
// SignalingClient through the live solo-join acceptance sequence against
// a real signaling server —
//
//   connect → internal-auth hello → room join → in-call flags →
//   participant snapshot (self present, anonymous) → ABRUPT transport
//   drop (no bye, via SignalingClient::DropTransport) → backoff →
//   reconnect → re-join that RESUMES the same session id →
//   participants restored → bye → clean exit.
//
// Exit codes:
//   0  all phases green
//   2  usage / missing secret
//   3  initial join timed out (auth/transport/room rejected)
//   4  self missing from participant snapshot
//   5  resume failed (no re-join, or session id changed after drop)
//   6  SetInCall rejected
//
// Secrets: --secret-file PATH or env ANIMUS_SIGNALING_INTERNAL_SECRET.
// Never argv. The secret is never logged.
//
// Usage:
//   animus_calls_probe --url wss://HOST[:port][/path] \
//       --backend https://NEXTCLOUD/ --room ROOMID --secret-file PATH \
//       [--timeout SECS] [--settle SECS]

#include "animus_kernel/calls/SignalingClient.h"

#include <json/json.h>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

using animus::kernel::calls::SignalingClient;
using animus::kernel::calls::SignalingConfig;

namespace {

const std::chrono::steady_clock::time_point kStart =
    std::chrono::steady_clock::now();

double ElapsedS() {
    return std::chrono::duration<double>(
               std::chrono::steady_clock::now() - kStart)
        .count();
}

void Log(const std::string& line) {
    static std::mutex log_mu;
    std::lock_guard<std::mutex> lk(log_mu);
    std::cout << "[+" << ElapsedS() << "s] " << line << std::endl;
}

std::string Compact(const Json::Value& v) {
    Json::StreamWriterBuilder w;
    w.settings_["indentation"] = "";
    std::string s = Json::writeString(w, v);
    if (s.size() > 400) s = s.substr(0, 400) + "...(truncated)";
    return s;
}

std::string Trim(std::string s) {
    const char* ws = " \t\r\n";
    const auto b = s.find_first_not_of(ws);
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

struct Args {
    std::string url;
    std::string backend;
    std::string room;
    std::string secret_file;
    double timeout_s = 25.0;
    double settle_s = 4.0;
};

void Usage() {
    std::cerr
        << "usage: animus_calls_probe --url wss://HOST[:port][/path]\n"
        << "  --backend https://NEXTCLOUD/ --room ROOMID\n"
        << "  (--secret-file PATH | env ANIMUS_SIGNALING_INTERNAL_SECRET)\n"
        << "  [--timeout SECS] [--settle SECS]\n";
}

struct Monitor {
    std::mutex mu;
    std::condition_variable cv;
    int joins = 0;
    int reconnect_waits = 0;
    bool closed = false;
};

} // namespace

int main(int argc, char** argv) {
    using State = SignalingClient::State;

    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        const bool has_value = i + 1 < argc;
        if (flag == "--url" && has_value) {
            args.url = argv[++i];
        } else if (flag == "--backend" && has_value) {
            args.backend = argv[++i];
        } else if (flag == "--room" && has_value) {
            args.room = argv[++i];
        } else if (flag == "--secret-file" && has_value) {
            args.secret_file = argv[++i];
        } else if (flag == "--timeout" && has_value) {
            args.timeout_s = std::atof(argv[++i]);
        } else if (flag == "--settle" && has_value) {
            args.settle_s = std::atof(argv[++i]);
        } else {
            Usage();
            return 2;
        }
    }
    if (args.url.empty() || args.backend.empty() || args.room.empty()) {
        Usage();
        return 2;
    }

    std::string secret;
    if (!args.secret_file.empty()) {
        std::ifstream f(args.secret_file, std::ios::binary);
        if (!f) {
            Log("FAIL cannot open --secret-file");
            return 2;
        }
        secret = Trim(std::string((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>()));
    } else {
        const char* env = std::getenv("ANIMUS_SIGNALING_INTERNAL_SECRET");
        if (env != nullptr) secret = Trim(env);
    }
    if (secret.empty()) {
        Log("FAIL no secret: --secret-file or ANIMUS_SIGNALING_INTERNAL_SECRET");
        return 2;
    }

    Log("animus_calls_probe (#335 live gate) room=" + args.room +
        " url=" + args.url + " backend=" + args.backend);

    SignalingConfig cfg;
    cfg.server_url = args.url;
    cfg.backend_url = args.backend;
    cfg.internal_secret = std::move(secret);
    cfg.room_id = args.room;
    cfg.reconnect_base_s = 1.0;
    cfg.reconnect_max_s = 5.0; // probe wants a fast resume, not patience
    cfg.hello_timeout_s = 10.0;
    SignalingClient client(std::move(cfg));

    Monitor mon;
    client.SetStateChangedCallback([&mon](State s) {
        {
            std::lock_guard<std::mutex> lk(mon.mu);
            if (s == State::Joined) ++mon.joins;
            if (s == State::ReconnectWait) ++mon.reconnect_waits;
            if (s == State::Closed) mon.closed = true;
        }
        Log(std::string("state -> ") + SignalingClient::StateName(s));
        mon.cv.notify_all();
    });
    client.SetFrameCallback([](const Json::Value& f) {
        const std::string type = f.get("type", "?").asString();
        if (type == "event" || type == "error" || type == "hello" ||
            type == "room") {
            Log("frame " + type + ": " + Compact(f));
        } else {
            Log("frame " + type);
        }
    });

    auto joins_now = [&mon]() {
        std::lock_guard<std::mutex> lk(mon.mu);
        return mon.joins;
    };
    auto joins_at_least = [&mon](int n) {
        std::lock_guard<std::mutex> lk(mon.mu);
        return mon.joins >= n;
    };
    auto waits_now = [&mon]() {
        std::lock_guard<std::mutex> lk(mon.mu);
        return mon.reconnect_waits;
    };
    // Waits until joins reaches n (or the client closes / timeout).
    auto wait_joins = [&](int n) {
        std::unique_lock<std::mutex> lk(mon.mu);
        return mon.cv.wait_for(
            lk, std::chrono::duration<double>(args.timeout_s),
            [&] { return mon.joins >= n || mon.closed; });
    };

    // Phase 1: connect → internal-auth hello → room join.
    client.Start();
    if (!wait_joins(1)) {
        Log("FAIL initial join timed out");
        client.Close();
        return 3;
    }
    if (!joins_at_least(1)) {
        Log("FAIL client closed before initial join");
        return 3;
    }
    const std::string s1 = client.session_id();
    Log("PHASE1 PASS joined, session_id=" + s1);

    // Phase 2: settle, then participant snapshot must contain self.
    std::this_thread::sleep_for(
        std::chrono::duration<double>(args.settle_s));
    {
        const auto ps = client.participants();
        Log("participants snapshot 1: n=" + std::to_string(ps.size()));
        bool self_found = false;
        for (const auto& p : ps) {
            Log("  sid=" + p.session_id + " user='" + p.user_id +
                "' incall=" + std::to_string(p.in_call));
            if (p.session_id == s1) self_found = true;
        }
        if (!self_found) {
            Log("FAIL self missing from participant snapshot 1");
            client.Close();
            return 4;
        }
        Log("PHASE2 PASS self present in participants (anonymous)");
    }

    // Phase 3: in-call flags (acceptance = SetInCall accepted; the
    // participants echo is corroborating evidence, not a hard gate).
    if (!client.SetInCall(1)) {
        Log("FAIL SetInCall rejected (not Joined)");
        client.Close();
        return 6;
    }
    std::this_thread::sleep_for(
        std::chrono::duration<double>(args.settle_s));
    {
        const auto ps = client.participants();
        for (const auto& p : ps) {
            if (p.session_id == s1 && p.in_call == 1) {
                Log("PHASE3 PASS in-call flag echoed for self");
                break;
            }
        }
    }

    // Phase 4: abrupt drop → reconnect → resume on the SAME session id.
    const int joins_target = joins_now() + 1;
    const int waits_before = waits_now();
    Log("dropping transport abruptly (no bye)");
    client.DropTransport();
    if (!wait_joins(joins_target)) {
        Log("FAIL no re-join after abrupt drop");
        client.Close();
        return 5;
    }
    if (!joins_at_least(joins_target)) {
        Log("FAIL client closed instead of resuming");
        return 5;
    }
    if (waits_now() <= waits_before) {
        Log("WARN rejoin without an observed ReconnectWait state");
    }
    const std::string s2 = client.session_id();
    if (s2 != s1 || s2.empty()) {
        Log("FAIL resume: session id changed (before='" + s1 +
            "' after='" + s2 + "')");
        client.Close();
        return 5;
    }
    Log("PHASE4 PASS resumed same session id after abrupt drop: " + s2);

    // Phase 5: participants restored after resume.
    std::this_thread::sleep_for(
        std::chrono::duration<double>(args.settle_s));
    {
        const auto ps = client.participants();
        bool self_found = false;
        for (const auto& p : ps) {
            if (p.session_id == s2) self_found = true;
        }
        Log("participants snapshot 2: n=" + std::to_string(ps.size()));
        if (!self_found) {
            Log("FAIL self missing after resume");
            client.Close();
            return 4;
        }
        Log("PHASE5 PASS participants restored after resume");
    }

    // Phase 6: graceful bye.
    client.Close();
    Log("PROBE PASS hello -> join -> incall -> drop -> resume -> bye");
    return 0;
}
