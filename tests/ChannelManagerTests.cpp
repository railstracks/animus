// ChannelManagerTests — #144: WARN loudly when a configured channel type has
// neither a C++ connector nor a registered Lua plugin.
//
// Origin (Oct 11 Mastodon trial): a configured channel whose type had no
// implementation anywhere was silently skipped at start — the entire signal
// was one DEBUG line; the failure only surfaced later as
// adapter_registered:false and "unknown platform" dispatch errors. "That's a
// config error masquerading as a missing feature."
//
// These tests pin the start-path behavior for all three states:
//   (a) made-up type, no implementation anywhere          -> WARN
//   (b) plugin-backed type, Lua plugin registered         -> no WARN (INFO)
//   (c) plugin-backed type, plugin absent (the trial)     -> WARN
//
// Test seam: a fresh ChannelManager has m_running == false, so RestartChannel
// takes its synchronous branch (StopChannel + StartChannel inline, no restart
// thread) — a deterministic, single-threaded path to the StartChannel
// fall-through under test. CreateChannel registers the channel without
// starting it (auto-start requires m_running, which only Initialize() sets).
// ALOG_* output is captured by swapping std::cerr's streambuf — the logger
// facade writes straight to std::cerr, so the buffer swap intercepts it
// without a production-code sink.

#include "animus_kernel/ChannelManager.h"
#include "animus_kernel/ChannelState.h"
#include <vector>
#include "animus_kernel/ChannelAdapters.h"
#include "animus_kernel/ChannelHelpers.h"
#include "animus_kernel/AgentConfigStore.h"
#include "animus_kernel/SqliteDataStore.h"
#include "animus_kernel/tools/HttpClient.h"

#include <json/value.h>
#include <json/writer.h>

#include <cstdio>

#include <iostream>
#include <sstream>
#include <string>

using namespace animus::kernel;

namespace {

int g_failures = 0;

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  ASSERT FAILED: " << msg << "\n";
        g_failures++;
    }
}

class StderrCapture {
public:
    StderrCapture() : m_old(std::cerr.rdbuf(m_buffer.rdbuf())) {}
    ~StderrCapture() { std::cerr.rdbuf(m_old); }
    StderrCapture(const StderrCapture&) = delete;
    StderrCapture& operator=(const StderrCapture&) = delete;

    std::string Take() {
        std::string out = m_buffer.str();
        m_buffer.str("");
        m_buffer.clear();
        return out;
    }

private:
    std::stringstream m_buffer;
    std::streambuf* m_old;
};

ChannelState MakeChannel(const std::string& name, const std::string& type) {
    ChannelState state;
    state.name = name;
    state.type = type;
    state.enabled = true;
    state.config = Json::Value(Json::objectValue);
    return state;
}

// Register the channel, then drive StartChannel synchronously via
// RestartChannel's m_running==false branch (see file header).
std::string StartAndGetLog(ChannelManager& manager, const ChannelState& state) {
    std::string error;
    if (!manager.CreateChannel(state, &error)) {
        Assert(false, "CreateChannel failed: " + error);
        return {};
    }
    StderrCapture capture;
    if (!manager.RestartChannel(state.name, &error)) {
        Assert(false, "RestartChannel failed: " + error);
    }
    return capture.Take();
}

// (a) configured type with no implementation anywhere (made-up type) -> WARN
void TestMadeUpTypeWithoutImplementationWarns() {
    std::cout << "[1/4] made-up type, no implementation anywhere -> WARN\n";
    HttpClient http;
    ChannelManager manager(http, nullptr, {}, {});
    manager.SetSocialPluginProbe(
        [](const std::string&) { return false; });

    const std::string log = StartAndGetLog(
        manager, MakeChannel("ghost", "definitely-not-a-platform"));

    Assert(log.find("WARN") != std::string::npos,
           "expected a WARN for a type with no implementation anywhere");
    Assert(log.find("definitely-not-a-platform") != std::string::npos,
           "WARN must name the channel type");
    Assert(log.find("C++ connector") != std::string::npos &&
               log.find("Lua") != std::string::npos,
           "WARN must say what to check (C++ connector vs Lua script)");
}

// (b) configured mastodon + Lua plugin registered -> no WARN
void TestPluginBackedTypeWithPluginRegisteredIsQuiet() {
    std::cout << "[2/4] plugin-backed type, plugin registered -> no WARN\n";
    HttpClient http;
    ChannelManager manager(http, nullptr, {}, {});

    // The probe is the kernel's live view of the Lua plugin registry —
    // here "mastodon" is registered (plugin.id == channel type).
    manager.SetSocialPluginProbe([](const std::string& type) {
        return type == "mastodon";
    });

    const std::string log = StartAndGetLog(
        manager, MakeChannel("mastodon-main", "mastodon"));

    Assert(log.find("WARN") == std::string::npos,
           "plugin-backed channel must not WARN at start");
    Assert(log.find("handled by a registered Lua plugin") != std::string::npos,
           "expected an INFO line naming the Lua plugin path");
}

// (c) configured mastodon, plugin absent — the Oct 11 trial condition -> WARN
void TestPluginBackedTypeWithPluginAbsentWarns() {
    std::cout << "[3/4] plugin-backed type, plugin absent (the trial) -> WARN\n";
    HttpClient http;
    ChannelManager manager(http, nullptr, {}, {});

    // Plugin registry answers "nothing registered".
    manager.SetSocialPluginProbe(
        [](const std::string&) { return false; });

    const std::string log = StartAndGetLog(
        manager, MakeChannel("mastodon-main", "mastodon"));

    Assert(log.find("WARN") != std::string::npos,
           "the trial condition (plugin absent) must WARN at start");
    Assert(log.find("mastodon") != std::string::npos,
           "WARN must name the channel type");
}

// A manager wired without any probe must keep the loud behavior for unknown
// types rather than regressing to silence.
void TestProbelessManagerWarnsOnUnknownType() {
    std::cout << "[4/4] no probe set, unknown type -> WARN (no silent skip)\n";
    HttpClient http;
    ChannelManager manager(http, nullptr, {}, {});

    const std::string log = StartAndGetLog(
        manager, MakeChannel("ghost2", "no-such-connector"));

    Assert(log.find("WARN") != std::string::npos,
           "without a probe, unknown types must WARN, not stay silent");
    Assert(log.find("no-such-connector") != std::string::npos,
           "WARN must name the channel type");
}

// ============================================================================
// #181: channel config secret strip — credential-shaped fields must not
// persist in the replicated channel.<name>.config row; they mirror to
// channels.<type:name>.<key> rows (vaulted by the config store's
// interception in production) and merge back on load. Legacy rows with
// embedded secrets are scrubbed on load (migrate-then-strip).
// ============================================================================
void TestChannelConfigSecretStrip() {
    std::cout << "[5/5] #181 secret strip: config row structural-only\n";
    HttpClient http;
    std::remove("/tmp/animus_channel_mgr_tests_181.db");
    SqliteDataStore dataStore("/tmp/animus_channel_mgr_tests_181.db");
    AgentConfigStore configStore(&dataStore);
    ChannelManager manager(http, &configStore, {}, {});

    Json::Value cfg;
    cfg["host"] = "irc.libera.chat";
    cfg["nick"] = "buffett";
    cfg["agent_id"] = "buffett";
    cfg["server_password"] = "s3cret-hunter2";
    ChannelState st;
    st.name = "main";
    st.type = "irc";
    st.enabled = false;
    st.config = cfg;
    std::string err;
    Assert(manager.CreateChannel(st, &err), "create should succeed: " + err);

    // 1. Stored row is structural-only.
    const std::string stored = configStore.GetRaw("", "channel.main.config");
    Assert(stored.find("s3cret-hunter2") == std::string::npos,
           "stored config row must not embed the credential");
    Assert(stored.find("irc.libera.chat") != std::string::npos,
           "structural field (host) must persist");
    Assert(stored.find("buffett") != std::string::npos,
           "structural field (nick/agent_id) must persist");

    // 2. Mirror rows: channel namespace + agent namespace.
    Assert(configStore.GetRaw("", "channels.irc:main.server_password") ==
               "s3cret-hunter2",
           "channel-namespace mirror row holds the credential");
    Assert(configStore.GetRaw("buffett", "channels.irc:main.server_password") ==
               "s3cret-hunter2",
           "agent-namespace mirror row holds the credential");

    // 3. In-memory state keeps the full config (adapter path unchanged).
    auto state = manager.GetChannel("main");
    Assert(state.has_value() &&
               state->config["server_password"].asString() == "s3cret-hunter2",
           "in-memory config keeps the credential for adapters");

    // 4. Load: a legacy embedded-secret row gets scrubbed (migrate-then-
    //    strip); the credential survives via mirror rows; structural
    //    config is untouched.
    Json::Value legacy;
    legacy["host"] = "irc.old.net";
    legacy["nick"] = "oldbird";
    legacy["access_token"] = "legacy-tok-99";
    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    configStore.Set("", "channel.legacy.config",
                    Json::writeString(wb, legacy));
    configStore.Set("", "channel.legacy.type", "irc");
    configStore.Set("", "channel.legacy.enabled", "false");

    manager.LoadChannelsFromConfigStore();

    const std::string scrubbed =
        configStore.GetRaw("", "channel.legacy.config");
    Assert(scrubbed.find("legacy-tok-99") == std::string::npos,
           "legacy row must be scrubbed structural-only");
    Assert(scrubbed.find("irc.old.net") != std::string::npos,
           "legacy structural config survives the scrub");
    auto merged = manager.GetChannel("legacy");
    Assert(merged.has_value() &&
               merged->config["access_token"].asString() == "legacy-tok-99",
           "legacy credential merges back from the mirror row");
    Assert(merged.has_value() &&
               merged->config["host"].asString() == "irc.old.net",
           "legacy structural fields load normally");
    auto remerged = manager.GetChannel("main");
    Assert(remerged.has_value() &&
               remerged->config["server_password"].asString() == "s3cret-hunter2",
           "stripped channel re-merges its credential on load");
}

// A2 channel hardening: unknown nextcloud config keys fail loudly — a
// typo'd key riding the config row while doing nothing was the gap.
// ValidateConfig is a static public seam: no manager or store needed.
void TestNextcloudConfigAllowlist() {
    std::string err;

    Json::Value good;
    good["server_url"] = "https://nextcloud.example.com";
    good["username"] = "agent";
    good["app_password"] = "app-tok";
    good["watch_tokens"] = "d7vv2vaa";
    good["respond_in_dm"] = "true";
    good["respond_in_group_on_mention"] = "true";
    good["group_mention_trigger"] = "@animus";
    good["agent_id"] = "a1b2c3";
    good["poll_interval"] = "30";
    good["stt_url"] = "http://stt:8080";
    Assert(ChannelManager::ValidateConfig("nextcloud", good, &err),
           "full known key set should validate: " + err);

    Json::Value typo = good;
    typo["serverurl"] = "https://nextcloud.example.com";  // typo'd key
    err.clear();
    Assert(!ChannelManager::ValidateConfig("nextcloud", typo, &err),
           "typo'd key must be rejected");
    Assert(err.find("Unknown nextcloud channel config key") != std::string::npos,
           "rejection must name the gap, got: " + err);
}

// #324 voice inbound: DetectVoiceNote against the live Talk 25 payload
// captured Nov 17 (petrel's voice note id 166 in animus-lab) plus the
// variants that must NOT wake the voice path.
void TestNextcloudVoiceDetection() {
    const char* voiceFixture = R"({
      "id": 166, "actorType": "users", "actorId": "petrel",
      "actorDisplayName": "Petrel", "message": "Petrel",
      "messageParameters": {
        "actor": {"type": "user", "id": "petrel", "name": "Petrel",
                  "mention-id": "petrel"},
        "file": {"type": "file", "id": "652",
                 "name": "tts-9536e16ca1.wav", "size": "208940",
                 "path": "Talk/tts-9536e16ca1.wav",
                 "mimetype": "audio/wav", "preview-available": "no"}
      },
      "systemMessage": "", "messageType": "voice-message"
    })";
    std::string name, mime;
    Json::Value voice = channel_detail::ParseJson(voiceFixture);
    Assert(NextcloudAdapter::DetectVoiceNote(voice, name, mime),
           "live voice-message fixture must detect as voice");
    Assert(name == "tts-9536e16ca1.wav",
           "voice file name must carry through, got: " + name);
    Assert(mime == "audio/wav",
           "voice mimetype must carry through, got: " + mime);

    // Audio file share (no voice-message marker) — same treatment.
    Json::Value share = voice;
    share["messageType"] = "comment";
    name.clear(); mime.clear();
    Assert(NextcloudAdapter::DetectVoiceNote(share, name, mime),
           "audio/* file share must detect as voice");
    Assert(name == "tts-9536e16ca1.wav",
           "share file name must carry through, got: " + name);

    // Non-audio file share — not voice.
    Json::Value doc = share;
    doc["messageParameters"]["file"]["mimetype"] = "text/plain";
    doc["messageParameters"]["file"]["name"] = "notes.txt";
    name.clear();
    Assert(!NextcloudAdapter::DetectVoiceNote(doc, name, mime),
           "text/plain file share must NOT detect as voice");

    // Plain text with the PHP empty-array quirk ([] params) — not voice.
    Json::Value text = channel_detail::ParseJson(R"({
      "id": 170, "actorType": "users", "actorId": "melvin",
      "message": "hello room", "messageParameters": [],
      "systemMessage": "", "messageType": "comment"
    })");
    Assert(!NextcloudAdapter::DetectVoiceNote(text, name, mime),
           "plain text ([] params quirk) must NOT detect as voice");
}

// #324 acceptance fix: composer voice notes share the sender's per-room
// FOLDER (room share type 10 + per-user derivations) — the wav is a child
// of the mount. Fixture captured live Nov 18 (animus-lab acceptance test,
// received-shares list as animus-agent).
void TestNextcloudVoiceShareResolution() {
    const char* sharesFixture = R"([
      {"id": "14", "share_type": 10, "uid_file_owner": "petrel",
       "file_target": "/Talk/probe-1794949732.txt",
       "mimetype": "text/plain"},
      {"id": "16", "share_type": 10, "uid_file_owner": "petrel",
       "file_target": "/Talk/tts-9536e16ca1.wav",
       "mimetype": "audio/wav"},
      {"id": "21", "share_type": 10, "uid_file_owner": "Melvin Sommer",
       "file_target": "/Talk/animus-lab-xe86d93n/Melvin-Melvin Sommer",
       "mimetype": "httpd/unix-directory"}
    ])";
    Json::Value shares = channel_detail::ParseJson(sharesFixture);

    // Composer voice note (Nov 18 acceptance): only the folder mount can
    // serve it — resolution must descend into the mount.
    const std::string voiceWav =
        "Voice message 2026-10-03 15-00-55 (animus-lab).wav";
    std::vector<std::string> targets =
        NextcloudAdapter::ResolveShareGetTargets(shares, voiceWav);
    Assert(targets.size() == 1,
           "folder share must resolve via child path, got "
           + std::to_string(targets.size()) + " targets");
    Assert(targets[0] ==
               "/Talk/animus-lab-xe86d93n/Melvin-Melvin Sommer/" + voiceWav,
           "child path must be mount + file name, got: " + targets[0]);

    // Direct file share: basename match outranks folder children
    // (petrel's TTS notes). The folder mount still offers a fallback
    // child candidate — fetched only if the direct GET fails (share
    // propagation races), so order is the contract, not count.
    targets = NextcloudAdapter::ResolveShareGetTargets(
        shares, "tts-9536e16ca1.wav");
    Assert(targets.size() == 2
               && targets[0] == "/Talk/tts-9536e16ca1.wav"
               && targets[1] == "/Talk/animus-lab-xe86d93n/"
                                "Melvin-Melvin Sommer/tts-9536e16ca1.wav",
           "direct file share must resolve by basename, ranked first");

    // Both shapes at once: direct target first, folder child after.
    Json::Value both = shares;
    both[1]["file_target"] = "/Talk/" + voiceWav;
    both[1]["mimetype"] = "audio/wav";
    targets = NextcloudAdapter::ResolveShareGetTargets(both, voiceWav);
    Assert(targets.size() == 2 && targets[0] == "/Talk/" + voiceWav,
           "direct match must outrank folder child, got "
           + std::to_string(targets.size()) + " targets");
}

} // namespace

int main() {
    TestMadeUpTypeWithoutImplementationWarns();
    TestPluginBackedTypeWithPluginRegisteredIsQuiet();
    TestPluginBackedTypeWithPluginAbsentWarns();
    TestProbelessManagerWarnsOnUnknownType();
    TestChannelConfigSecretStrip();
    TestNextcloudConfigAllowlist();
    TestNextcloudVoiceDetection();
    TestNextcloudVoiceShareResolution();

    if (g_failures == 0) {
        std::cout << "ChannelManagerTests: ALL PASSED\n";
        return 0;
    }
    std::cout << "ChannelManagerTests: " << g_failures << " FAILURE(S)\n";
    return 1;
}
