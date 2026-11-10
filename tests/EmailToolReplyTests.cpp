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
    std::cerr << "[1/7] own-sent anchor -> original recipient (#211 core)\n";
    std::string out, err;
    Assert(Resolve(kNoArgs, kSentAnchor, true, "kestrelmolty@agentmail.to", out, err),
           "resolves: " + err);
    Assert(out == "buffett@agentmail.to", "recipient = anchor's To: (got '" + out + "')");
}

void TestDisplayNameFormsBare() {
    std::cerr << "[2/7] display-name forms -> bare addresses\n";
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
    std::cerr << "[3/7] explicit to: honored in all cases\n";
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
    std::cerr << "[4/7] received anchor -> its From:\n";
    std::string out, err;
    Assert(Resolve(kNoArgs, kReceivedAnchor, true, "kestrelmolty@agentmail.to", out, err),
           "resolves: " + err);
    Assert(out == "devex@agentmail.to", "recipient = anchor's From: (got '" + out + "')");
}

void TestSelfAddressedOriginalRefuses() {
    std::cerr << "[5/7] self-addressed original (self-only To:) -> loud refusal\n";
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
    std::cerr << "[6/7] unfetchable anchor + no to -> hard error, no send\n";
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
    std::cerr << "[7/7] empty self id: self-filter inert, To: passes through\n";
    std::string out, err;
    Assert(Resolve(kNoArgs,
                   R"({"labels":["sent"],"to":["buffett@agentmail.to"]})",
                   true, "", out, err),
           "empty self id resolves");
    Assert(out == "buffett@agentmail.to", "recipient intact (got '" + out + "')");
}

} // namespace

int main() {
    TestOwnSentAnchorResolvesToOriginalRecipient();
    TestDisplayNameFormsBare();
    TestExplicitToWins();
    TestReceivedAnchorRepliesToSender();
    TestSelfAddressedOriginalRefuses();
    TestUnfetchableAnchorErrors();
    TestEmptySelfIdSkipsSelfFilter();
    if (g_failures == 0) std::cerr << "EmailToolReplyTests: all passed\n";
    return g_failures == 0 ? 0 : 1;
}
