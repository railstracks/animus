// EmailTool attachment support tests (#212).
//
// NormalizeAttachments and CountConfirmedAttachments are pure; fixtures
// mirror the live AgentMail shapes verified 2026-10-22:
//  - send attachments: [{filename, content(base64), content_type?}]
//  - thread messages: attachments entries carry attachment_id (NOT id)
//  - send responses can echo "attachments": 0 despite success — the
//    thread is the receipt, hence CountConfirmedAttachments.
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

bool Normalize(const std::string& argsJson, Json::Value& out, std::string& err) {
    return EmailTool::NormalizeAttachments(Parse(argsJson), out, err);
}

void TestAbsentAttachmentsYieldsEmptyArray() {
    std::cerr << "[1/8] absent attachments -> empty array, no error\n";
    Json::Value out; std::string err;
    Assert(Normalize("{}", out, err), "absent accepted: " + err);
    Assert(out.isArray() && out.size() == 0, "empty array out");
}

void TestValidEntriesPassThrough() {
    std::cerr << "[2/8] valid entries pass through; optional content_type preserved\n";
    Json::Value out; std::string err;
    Assert(Normalize(
        R"({"attachments":[
            {"filename":"report.pdf","content":"QUJD"},
            {"filename":"data.csv","content":"REVG","content_type":"text/csv"}]})",
        out, err), "valid accepted: " + err);
    Assert(out.size() == 2, "two entries");
    Assert(out[0]["filename"].asString() == "report.pdf", "entry0 filename");
    Assert(!out[0].isMember("content_type"), "entry0 has no content_type");
    Assert(out[1]["content_type"].asString() == "text/csv", "entry1 content_type kept");
}

void TestNonArrayRejected() {
    std::cerr << "[3/8] non-array attachments rejected loudly\n";
    Json::Value out; std::string err;
    Assert(!Normalize(R"({"attachments":"report.pdf"})", out, err), "string rejected");
    Assert(err.find("array") != std::string::npos, "error names array: " + err);
}

void TestMissingFilenameRejected() {
    std::cerr << "[4/8] entry missing filename rejected with index\n";
    Json::Value out; std::string err;
    Assert(!Normalize(
        R"({"attachments":[{"filename":"ok.txt","content":"QQ=="},{"content":"QQ=="]})",
        out, err), "malformed entry 1 rejected");
    Assert(err.find("[1]") != std::string::npos, "error names index 1: " + err);
}

void TestEmptyContentRejected() {
    std::cerr << "[5/8] empty content rejected\n";
    Json::Value out; std::string err;
    Assert(!Normalize(R"({"attachments":[{"filename":"x.txt","content":""}]})", out, err),
           "empty content rejected");
}

int Count(const std::string& threadJson, const std::string& mid) {
    return EmailTool::CountConfirmedAttachments(Parse(threadJson), mid);
}

const char* kThread =
    R"({"thread_id":"t1","messages":[
        {"message_id":"<a@x>","labels":["received"],
         "attachments":[{"attachment_id":"at1","filename":"in.pdf","size":10}]},
        {"message_id":"<b@x>","labels":["sent"],
         "attachments":[{"attachment_id":"at2","filename":"out.pdf","size":20},
                        {"attachment_id":"at3","filename":"out.csv","size":30}]},
        {"message_id":"<c@x>","labels":["sent"]}]})";

void TestCountOnMessages() {
    std::cerr << "[6/8] thread receipt counts the target message's attachments\n";
    Assert(Count(kThread, "<b@x>") == 2, "sent message has 2");
    Assert(Count(kThread, "<a@x>") == 1, "received message has 1");
}

void TestCountZeroWhenNone() {
    std::cerr << "[7/8] message without attachments -> 0 (not -1)\n";
    Assert(Count(kThread, "<c@x>") == 0, "no attachments key -> 0");
}

void TestCountAbsentOrMalformed() {
    std::cerr << "[8/8] absent message / malformed body -> -1\n";
    Assert(Count(kThread, "<zz@x>") == -1, "absent message -> -1");
    Assert(Count("{}", "<a@x>") == -1, "no messages member -> -1");
    Assert(Count("[1,2]", "<a@x>") == -1, "non-object body -> -1");
}

} // namespace

int main() {
    TestAbsentAttachmentsYieldsEmptyArray();
    TestValidEntriesPassThrough();
    TestNonArrayRejected();
    TestMissingFilenameRejected();
    TestEmptyContentRejected();
    TestCountOnMessages();
    TestCountZeroWhenNone();
    TestCountAbsentOrMalformed();
    if (g_failures == 0) std::cerr << "EmailToolAttachmentTests: all 8 passed\n";
    return g_failures == 0 ? 0 : 1;
}
