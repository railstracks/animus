// EmailTool reply-recipient resolution tests (#211).
//
// ResolveReplyRecipient is pure: anchor JSON fixtures mirror the live
// AgentMail message-GET shape (verified 2026-11-08: labels sent/received,
// "to" array, "from" string, "Name <addr>" display forms).
//
// The bug being fixed: the server-side /reply resolves the recipient from
// thread ownership, so a reply anchored on the sender's OWN sent message
// self-addresses — 200-confirmed, never delivered (six live incidents).
// Client-side resolution: explicit to wins; own-sent anchor -> its
// envelope To:; received anchor -> its From:; unresolvable -> loud error.
#include "animus_kernel/tools/EmailTool.h"
#include "animus_kernel/AgentConfigStore.h"
#include "animus_kernel/api/SecretsVault.h"
#include "animus_kernel/SqliteDataStore.h"
#include "animus_kernel/tools/HttpClient.h"
#include <unistd.h>

#include <json/json.h>
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

Json::Value Parse(const std::string& s) {
    Json::Value v;
    std::istringstream ss(s);
    Json::CharReaderBuilder rb;
    std::string errs;
    Json::parseFromStream(rb, ss, &v, &errs);
    return v;
}

bool Resolve(const std::string& argsJson, const std::string& anchorJson,
             bool fetched, const char* self, std::string& out, std::string& err) {
    Json::Value args = Parse(argsJson);
    Json::Value anchor = Parse(anchorJson);
    return EmailTool::ResolveReplyRecipient(args, anchor, fetched, self, out, err);
}

const char* kNoArgs = "{}";
const char* kSentAnchor =
    R"({"labels":["sent"],"to":["buffett@agentmail.to"],
        "from":"Kestrel <kestrelmolty@agentmail.to>",
        "message_id":"<x@email.amazonses.com>"})";
const char* kReceivedAnchor =
    R"({"labels":["received","unread"],"to":["kestrelmolty@agentmail.to"],
        "from":"Devex <devex@agentmail.to>",
        "message_id":"<y@email.amazonses.com>"})";

void TestOwnSentAnchorResolvesToOriginalRecipient() {
    std::cerr << "[1/11] own-sent anchor -> original recipient (#211 core)\n";
    std::string out, err;
    Assert(Resolve(kNoArgs, kSentAnchor, true, "kestrelmolty@agentmail.to", out, err),
           "resolves: " + err);
    Assert(out == "buffett@agentmail.to", "recipient = anchor's To: (got '" + out + "')");
}

void TestDisplayNameFormsBare() {
    std::cerr << "[2/11] display-name forms -> bare addresses\n";
    std::string out, err;
    Assert(Resolve(kNoArgs,
                   R"({"labels":["sent"],"to":["Buffett <buffett@agentmail.to>"]})",
                   true, "kestrelmolty@agentmail.to", out, err),
           "resolves: " + err);
    Assert(out == "buffett@agentmail.to", "bare address extracted (got '" + out + "')");
    Assert(Resolve(kNoArgs,
                   R"({"labels":["received"],"from":"Devex <devex@agentmail.to>"})",
                   true, "kestrelmolty@agentmail.to", out, err),
           "received display form: " + err);
    Assert(out == "devex@agentmail.to", "from display form bare (got '" + out + "')");
}

void TestExplicitToWins() {
    std::cerr << "[3/11] explicit to: honored in all cases\n";
    std::string out, err;
    Assert(Resolve(R"({"to":"override@agentmail.to"})", kSentAnchor, true,
                   "kestrelmolty@agentmail.to", out, err),
           "explicit to resolves");
    Assert(out == "override@agentmail.to", "override honored (got '" + out + "')");
    // Explicit to with NO anchor fetch still works (fetch is skipped).
    Assert(Resolve(R"({"to":"override@agentmail.to"})", "{}", false,
                   "kestrelmolty@agentmail.to", out, err),
           "explicit to without anchor");
    Assert(out == "override@agentmail.to", "override without anchor (got '" + out + "')");
}

void TestReceivedAnchorRepliesToSender() {
    std::cerr << "[4/11] received anchor -> its From:\n";
    std::string out, err;
    Assert(Resolve(kNoArgs, kReceivedAnchor, true, "kestrelmolty@agentmail.to", out, err),
           "resolves: " + err);
    Assert(out == "devex@agentmail.to", "recipient = anchor's From: (got '" + out + "')");
}

void TestSelfAddressedOriginalRefuses() {
    std::cerr << "[5/11] self-addressed original (self-only To:) -> loud refusal\n";
    std::string out, err;
    Assert(!Resolve(kNoArgs,
                    R"({"labels":["sent"],"to":["kestrelmolty@agentmail.to"],
                        "from":"Kestrel <kestrelmolty@agentmail.to>"})",
                    true, "kestrelmolty@agentmail.to", out, err),
           "self-only envelope refused");
    Assert(err.find("refusing to send to self") != std::string::npos,
           "refusal names the problem: " + err);
    // Mixed To: self drops out, others survive.
    Assert(Resolve(kNoArgs,
                  R"({"labels":["sent"],"to":["buffett@agentmail.to",
                      "Kestrel <kestrelmolty@agentmail.to>"]})",
                  true, "kestrelmolty@agentmail.to", out, err),
           "mixed envelope resolves");
    Assert(out == "buffett@agentmail.to", "self dropped, other kept (got '" + out + "')");
}

void TestUnfetchableAnchorErrors() {
    std::cerr << "[6/11] unfetchable anchor + no to -> hard error, no send\n";
    std::string out, err;
    Assert(!Resolve(kNoArgs, "{}", false, "kestrelmolty@agentmail.to", out, err),
           "unfetchable refused");
    Assert(err.find("refusing to send") != std::string::npos,
           "error names the refusal: " + err);
    // Empty To: array on a fetched own-sent anchor.
    Assert(!Resolve(kNoArgs, R"({"labels":["sent"],"to":[]})", true,
                    "kestrelmolty@agentmail.to", out, err),
           "empty To: refused");
    // Missing from on a received anchor.
    Assert(!Resolve(kNoArgs, R"({"labels":["received"]})", true,
                    "kestrelmolty@agentmail.to", out, err),
           "missing From: refused");
}

void TestEmptySelfIdSkipsSelfFilter() {
    std::cerr << "[7/11] empty self id: self-filter inert, To: passes through\n";
    std::string out, err;
    Assert(Resolve(kNoArgs,
                   R"({"labels":["sent"],"to":["buffett@agentmail.to"]})",
                   true, "", out, err),
           "empty self id resolves");
    Assert(out == "buffett@agentmail.to", "recipient intact (got '" + out + "')");
}

// ─── #304 witness: scrub-instance resolver fixture ───
struct ScrubInstanceFixture {

    static std::string MakeDbPath() {
        char tmp[] = "/tmp/animus_304_test_XXXXXX";
        int fd = mkstemp(tmp);
        if (fd >= 0) close(fd);
        unlink(tmp);
        return std::string(tmp);
    }
    std::string dbPath = MakeDbPath();
    std::string keyPath = dbPath + ".vault.key";
    SqliteDataStore db{dbPath};
    SecretsVault vault{&db, keyPath};
    AgentConfigStore cfg{&db};

    ScrubInstanceFixture() {
        vault.EnsureSchema();  // vault table is NOT auto-created by the ctor
        cfg.SetVault(&vault);  // same wiring as AgentKernel.cpp:740
    }
    ~ScrubInstanceFixture() {
        unlink(dbPath.c_str());
        unlink(keyPath.c_str());
    }

    void SeedLegacy(const char* name) {
        // Pre-#181 shape: plaintext api_key in the legacy row, no mirror.
        Json::Value cfg_json(Json::objectValue);
        cfg_json["backend"] = "agentmail";
        cfg_json["inbox_id"] = "buffett@agentmail.to";
        cfg_json["api_key"] = "legacy-key-0";
        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        cfg.Set("", std::string("channel.") + name + ".type", "email");
        cfg.Set("", std::string("channel.") + name + ".config",
                Json::writeString(wb, cfg_json));
    }
    void SeedScrubbed(const char* name) {
        // #181 post-scrub shape: structural-only legacy row (no api_key)
        // + vaulted channels.{type}:{name}.api_key mirror row. Mirrors the
        // production Set() interception: the credential-shaped value
        // auto-vaults and lands as a {"secret_ref":...} row, which Get()
        // resolves transparently (same wiring AgentKernel.cpp:740 sets).
        Json::Value cfg_json(Json::objectValue);
        cfg_json["backend"] = "agentmail";
        cfg_json["inbox_id"] = "buffett@agentmail.to";
        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        cfg.Set("", std::string("channel.") + name + ".type", "email");
        cfg.Set("", std::string("channel.") + name + ".config",
                Json::writeString(wb, cfg_json));
        cfg.Set("", std::string("channels.email:") + name + ".api_key",
                "scrub-key-304");
    }
};

} // namespace


// ─── #304: scrub-instance resolver witness + resolver regression ───
void TestScrubbedInstanceResolversFindVaultedMirror() {
    std::cerr << "[8/11] #304 scrub instance: resolvers resolve the vaulted mirror\n";
    ScrubInstanceFixture fx;
    fx.SeedScrubbed("email");
    HttpClient http;
    EmailTool tool(http, &fx.cfg);

    std::string inbox, key;
    Assert(tool.ResolveAccount("email", inbox, key),
           "ResolveAccount on scrubbed instance");
    Assert(inbox == "buffett@agentmail.to", "inbox_id from legacy row (got '" + inbox + "')");
    Assert(key == "scrub-key-304", "api_key via vaulted mirror (got '" + key + "')");

    Assert(tool.ResolveAccountByInboxId("buffett@agentmail.to", inbox, key),
           "ResolveAccountByInboxId on scrubbed instance");
    Assert(key == "scrub-key-304", "by-inbox mirror key (got '" + key + "')");

    Assert(tool.FindFirstAgentMailAccount(inbox, key),
           "FindFirstAgentMailAccount on scrubbed instance");
    Assert(key == "scrub-key-304", "first-account mirror key (got '" + key + "')");
}

void TestLegacyInstanceFallbackIntact() {
    std::cerr << "[9/11] #304 fresh legacy instance: plaintext row fallback intact\n";
    ScrubInstanceFixture fx;
    fx.SeedLegacy("email");
    HttpClient http;
    EmailTool tool(http, &fx.cfg);

    std::string inbox, key;
    Assert(tool.ResolveAccount("email", inbox, key),
           "ResolveAccount on legacy instance");
    Assert(key == "legacy-key-0", "legacy plaintext api_key (got '" + key + "')");
    Assert(tool.FindFirstAgentMailAccount(inbox, key) && key == "legacy-key-0",
           "first account falls back to legacy row");
}

void TestGetRawBypassesVaultResolution() {
    std::cerr << "[10/11] #304 mirror row is a secret_ref — GetRaw bypasses resolution\n";
    ScrubInstanceFixture fx;
    fx.SeedScrubbed("email");
    std::string raw = fx.cfg.GetRaw("", "channels.email:email.api_key");
    Assert(raw.find("scrub-key-304") == std::string::npos,
           "raw row holds no plaintext credential");
    Assert(raw.find("secret_ref") != std::string::npos,
           "raw row is a secret_ref object: " + raw);
}

void TestMissingMirrorAndLegacyRowFailsLoud() {
    std::cerr << "[11/11] #304 no credential anywhere -> resolver fails, no fabricated key\n";
    ScrubInstanceFixture fx;
    fx.SeedScrubbed("email2");  // type row: mirror key lookup uses channels.email:...
    // Divergent type row: helper derives mirror key from sibling type, which
    // now points at a non-seeded mirror -> neither source has a credential.
    fx.cfg.Set("", "channel.email2.type", "imap");
    HttpClient http;
    EmailTool tool(http, &fx.cfg);
    std::string inbox, key;
    Assert(!tool.ResolveAccount("email2", inbox, key),
           "no credential -> ResolveAccount fails");
    Assert(key.empty() || key != "scrub-key-304", "no fabricated key");
}
int main() {
    TestOwnSentAnchorResolvesToOriginalRecipient();
    TestDisplayNameFormsBare();
    TestExplicitToWins();
    TestReceivedAnchorRepliesToSender();
    TestSelfAddressedOriginalRefuses();
    TestUnfetchableAnchorErrors();
    TestEmptySelfIdSkipsSelfFilter();
    TestScrubbedInstanceResolversFindVaultedMirror();
    TestLegacyInstanceFallbackIntact();
    TestGetRawBypassesVaultResolution();
    TestMissingMirrorAndLegacyRowFailsLoud();
    if (g_failures == 0) std::cerr << "EmailToolReplyTests: all passed\n";
    return g_failures == 0 ? 0 : 1;
}
