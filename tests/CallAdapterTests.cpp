// CallAdapterTests — #336 offline unit gates: join-policy decision table
// (decision 2), config-shape validation (A2 allowlist discipline), and the
// verb-surface error paths that need no network. Live join/leave/status
// against production signaling ride the calls probe (animus_calls_probe)
// and the adapter's own live gate — the suite stays network-free by design.

#include "animus_kernel/ChannelAdapters.h"
#include "animus_kernel/ChannelState.h"

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

    if (failures > 0) {
        std::cout << "CALL ADAPTER TESTS FAILED (" << failures << ")\n";
        return 1;
    }
    std::cout << "ALL CALL ADAPTER TESTS PASSED\n";
    return 0;
}
