// IrcInterfaceTests — unit tests for IRC text normalization (#204).
//
// The incident: a /me message carries raw CTCP \001 bytes on the wire
// ("\001ACTION ...\001"); ingesting them verbatim poisons the session
// transcript — every later LLM call fails with HTTP 400 because raw
// control bytes are illegal in JSON strings. The agent goes silent:
// restart-proof, channel-restart-proof, invisible from the channel side.
//
// These tests pin the fix at every layer:
//   - NormalizeIncomingText: CTCP + mIRC formatting → safe markdown-ish
//     text; non-ACTION CTCP dropped entirely
//   - HandleLine end-to-end: what actually reaches the message callback
//   - MarkdownToIrcFormat / ActionFrame / SendAction: outbound surface

#include "animus_kernel/interfaces/IrcInterface.h"

#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace animus::kernel;

namespace {

int g_failures = 0;

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  ASSERT FAILED: " << msg << "\n";
        g_failures++;
    }
}

bool Dropped(const std::optional<std::string>& o) { return !o.has_value(); }

// ---------------------------------------------------------------------------
// NormalizeIncomingText — CTCP
// ---------------------------------------------------------------------------

void TestNormalizeAction() {
    const auto r = IrcInterfaceRuntime::NormalizeIncomingText(
        "\001ACTION rubs his hands as he considers his approach\001");
    Assert(r.has_value(), "ACTION should be kept");
    if (r) {
        Assert(*r == "* rubs his hands as he considers his approach",
               "ACTION renders as '* text', got: " + *r);
    }
    const auto empty = IrcInterfaceRuntime::NormalizeIncomingText("\001ACTION\001");
    Assert(Dropped(empty), "bare ACTION with no text carries nothing — dropped");
}

void TestNormalizeCtcpDropped() {
    Assert(Dropped(IrcInterfaceRuntime::NormalizeIncomingText("\001VERSION\001")),
           "VERSION probe dropped");
    Assert(Dropped(IrcInterfaceRuntime::NormalizeIncomingText("\001PING 1234567890\001")),
           "PING probe dropped");
    Assert(Dropped(IrcInterfaceRuntime::NormalizeIncomingText("\001DCC SEND evil.exe 2130706433 1024\001")),
           "DCC dropped (safety: never surface file-transfer probes)");
    Assert(Dropped(IrcInterfaceRuntime::NormalizeIncomingText("\001CLIENTINFO\001")),
           "CLIENTINFO dropped");
    Assert(Dropped(IrcInterfaceRuntime::NormalizeIncomingText("\001FINGER\001")),
           "FINGER dropped");
    Assert(Dropped(IrcInterfaceRuntime::NormalizeIncomingText("\001TIME\001")),
           "TIME dropped");
}

// ---------------------------------------------------------------------------
// NormalizeIncomingText — mIRC formatting codes → markdown
// ---------------------------------------------------------------------------

void TestNormalizeFormatting() {
    Assert(IrcInterfaceRuntime::NormalizeIncomingText("\002bold\002 plain") ==
               "**bold** plain",
           "bold toggle -> ** pairs");
    Assert(IrcInterfaceRuntime::NormalizeIncomingText("\035ital\035") == "*ital*",
           "italic toggle -> * pairs");
    Assert(IrcInterfaceRuntime::NormalizeIncomingText("\037under\037") == "__under__",
           "underline toggle -> __ pairs");
    Assert(IrcInterfaceRuntime::NormalizeIncomingText("\0034,5red\003 plain") ==
               "red plain",
           "color code with fg,bg params stripped");
    Assert(IrcInterfaceRuntime::NormalizeIncomingText("\00312blue") == "blue",
           "color code with fg param stripped");
    Assert(IrcInterfaceRuntime::NormalizeIncomingText("\003no-digits") == "no-digits",
           "bare color code stripped");
    Assert(IrcInterfaceRuntime::NormalizeIncomingText("\002bold\017 rest") ==
               "**bold** rest",
           "reset (0x0F) closes open toggles");
    Assert(IrcInterfaceRuntime::NormalizeIncomingText("a\026b\007c") == "abc",
           "reverse + beep stripped");
    Assert(IrcInterfaceRuntime::NormalizeIncomingText(
               "\002bold\002 and \035ital\035 and \037under\037") ==
               "**bold** and *ital* and __under__",
           "mixed formatting");
}

// ---------------------------------------------------------------------------
// NormalizeIncomingText — control-byte sweep
// ---------------------------------------------------------------------------

void TestNormalizeControlSweep() {
    Assert(IrcInterfaceRuntime::NormalizeIncomingText("a\001b") == "ab",
           "stray \\001 mid-text stripped");
    Assert(IrcInterfaceRuntime::NormalizeIncomingText("plain text") == "plain text",
           "plain text unchanged");
    Assert(IrcInterfaceRuntime::NormalizeIncomingText("multi\nline") == "multi\nline",
           "newline preserved");
    Assert(IrcInterfaceRuntime::NormalizeIncomingText("tab\tkept") == "tab\tkept",
           "tab preserved");
    Assert(IrcInterfaceRuntime::NormalizeIncomingText("del\177char") == "delchar",
           "0x7F DEL stripped");
}

// ---------------------------------------------------------------------------
// HandleLine — end to end (what reaches the message callback)
// ---------------------------------------------------------------------------

std::vector<std::string> g_dispatched;

IrcInterfaceRuntime::Config TestConfig() {
    IrcInterfaceRuntime::Config cfg;
    cfg.host = "irc.invalid";
    cfg.nick = "testbot";
    return cfg;
}

void TestHandleLineEndToEnd() {
    g_dispatched.clear();
    IrcInterfaceRuntime rt(
        "test", TestConfig(),
        [](const std::string& nick, const std::string& target,
           const std::string& message, bool /*isNotice*/) {
            g_dispatched.push_back(nick + "|" + target + "|" + message);
        },
        nullptr);
    bool joined = false;

    rt.HandleLine(
        ":aprile!user@host PRIVMSG #st_test :\001ACTION rubs his hands\001",
        &joined);
    Assert(g_dispatched.size() == 1, "ACTION dispatches exactly once");
    if (g_dispatched.size() == 1) {
        Assert(g_dispatched[0] == "aprile|#st_test|* rubs his hands",
               "callback receives normalized action, got: " + g_dispatched[0]);
    }

    rt.HandleLine(":aprile!user@host PRIVMSG #st_test :\002bold\002 move", &joined);
    Assert(g_dispatched.size() == 2, "formatted message dispatches");
    if (g_dispatched.size() == 2) {
        Assert(g_dispatched[1] == "aprile|#st_test|**bold** move",
               "callback receives markdown, got: " + g_dispatched[1]);
    }

    rt.HandleLine(":aprile!user@host PRIVMSG #st_test :\001VERSION\001", &joined);
    Assert(g_dispatched.size() == 2, "CTCP VERSION probe never dispatched");

    rt.HandleLine(":aprile!user@host PRIVMSG testbot :hello there", &joined);
    Assert(g_dispatched.size() == 3, "plain DM dispatches");
    if (g_dispatched.size() == 3) {
        Assert(g_dispatched[2] == "aprile|testbot|hello there",
               "plain text unchanged end to end");
    }
}

// ---------------------------------------------------------------------------
// MarkdownToIrcFormat — outbound markdown → IRC formatting codes
// ---------------------------------------------------------------------------

void TestMarkdownToIrc() {
    Assert(IrcInterfaceRuntime::MarkdownToIrcFormat("**bold**") == "\002bold\002",
           "** -> 0x02 pair");
    Assert(IrcInterfaceRuntime::MarkdownToIrcFormat("__under__") == "\037under\037",
           "__ -> 0x1F pair");
    Assert(IrcInterfaceRuntime::MarkdownToIrcFormat("*ital*") == "\035ital\035",
           "* -> 0x1D pair");
    Assert(IrcInterfaceRuntime::MarkdownToIrcFormat("_ital_") == "\035ital\035",
           "_ -> 0x1D pair (word-bounded)");
    Assert(IrcInterfaceRuntime::MarkdownToIrcFormat("* item\n* item2") ==
               "* item\n* item2",
           "markdown bullets are NOT italic markers");
    Assert(IrcInterfaceRuntime::MarkdownToIrcFormat("snake_case_word") ==
               "snake_case_word",
           "snake_case is not emphasis");
    Assert(IrcInterfaceRuntime::MarkdownToIrcFormat("**a** and **b**") ==
               "\002a\002 and \002b\002",
           "multiple bold pairs");
    Assert(IrcInterfaceRuntime::MarkdownToIrcFormat("plain text") == "plain text",
           "plain text unchanged");
    Assert(IrcInterfaceRuntime::MarkdownToIrcFormat("a ** b") == "a ** b",
           "unpaired ** untouched");
    Assert(IrcInterfaceRuntime::MarkdownToIrcFormat("2 ** 3 ** 4") == "2 ** 3 ** 4",
           "math-style double asterisks untouched");
    // Already-IRC text passes through: no markdown pairs to convert.
    Assert(IrcInterfaceRuntime::MarkdownToIrcFormat("\002bold\002") == "\002bold\002",
           "raw IRC codes pass through unchanged");
}

// ---------------------------------------------------------------------------
// ActionFrame + SendAction
// ---------------------------------------------------------------------------

void TestActionFrame() {
    Assert(IrcInterfaceRuntime::ActionFrame("waves") == "\001ACTION waves\001",
           "action frame wraps text in CTCP ACTION");
    Assert(IrcInterfaceRuntime::ActionFrame("") == "\001ACTION \001",
           "empty action still frames (caller validates)");
}

void TestSendActionNoSocket() {
    IrcInterfaceRuntime rt(
        "test", TestConfig(),
        [](const std::string&, const std::string&, const std::string&, bool) {},
        nullptr);
    Assert(!rt.SendAction("#st_test", "waves"),
           "SendAction without a connection returns false (no crash)");
    Assert(!rt.SendAction("", "waves"), "SendAction with empty target false");
    Assert(!rt.SendAction("#st_test", ""), "SendAction with empty message false");
}

}  // namespace

// ============================================================================
// Main
// ============================================================================
int main() {
    std::cerr << "IrcInterface tests:\n";

    TestNormalizeAction();
    TestNormalizeCtcpDropped();
    TestNormalizeFormatting();
    TestNormalizeControlSweep();
    TestHandleLineEndToEnd();
    TestMarkdownToIrc();
    TestActionFrame();
    TestSendActionNoSocket();

    std::cerr << "\n";
    if (g_failures > 0) {
        std::cerr << "FAILED: " << g_failures << " assertion(s)\n";
        return 1;
    }
    std::cerr << "All IRC interface tests passed!\n";
    return 0;
}
