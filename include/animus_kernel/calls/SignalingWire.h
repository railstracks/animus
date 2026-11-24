// SignalingWire.h — wire shapes of the Nextcloud Talk signaling protocol
// as the deployed hub (nextcloud-spreed-signaling v2) parses them.
//
// Pure and header-only (jsoncpp only) so the offline suite
// (CallAdapterTests) can gate the exact shapes that go on the wire.
//
// Why this exists (ticket #337, runs 11-18, pinned Nov 24 2026 against
// the server source and the run-18 hub log):
//   - every trickle candidate the adapter SENT was rejected by the hub
//     with "Payload does not contain a valid candidate" — it was a bare
//     string — so the MCU never learned where to reach the seat;
//   - every candidate the MCU sent BACK was dropped unread — it is an
//     object and the handler accepted strings only — so the seat's ICE
//     agent never ran a single check.
//
// Server rules (api/signaling.go CheckValid, server/clientsession.go
// sendCandidate, sfu/janus):
//   - payload.candidate must be a nested object holding "candidate".
//     A bare string fails ConvertStringMap -> invalid_candidate.
//   - "candidate": "" inside that object is the end-of-candidates signal.
//   - the object is handed to the MCU untouched (handle.Trickle), so it
//     is what carries sdpMid / sdpMLineIndex for Janus's m-line lookup;
//   - inbound from the MCU the object is Janus's own trickle shape
//     {"sdpMid","sdpMLineIndex","candidate"} (janus.go TrickleMsg).

#pragma once

#include <json/json.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>

namespace animus::kernel::calls {

/// Trickle-candidate envelope in the shape the hub accepts. `mid` and
/// `mlineIndex` locate the m-line (Janus matches on them); index < 0
/// means "unknown, omit".
inline Json::Value CandidateEnvelope(const std::string& candidate,
                                     const std::string& mid,
                                     int mlineIndex) {
    Json::Value cand(Json::objectValue);
    cand["candidate"] = candidate;
    if (!mid.empty()) cand["sdpMid"] = mid;
    if (mlineIndex >= 0) cand["sdpMLineIndex"] = mlineIndex;
    return cand;
}

/// End-of-candidates: the same envelope with an empty candidate string.
inline Json::Value EndOfCandidatesEnvelope() {
    return CandidateEnvelope("", "", -1);
}

struct RemoteCandidate {
    std::string candidate; // bare "candidate:..." form; empty at end
    std::string mid;       // m-line id, when the sender supplied one
    bool endOfCandidates = false;
};

inline void TrimWhitespace(std::string& s) {
    const auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
}

/// Tolerant reader for an inbound candidate message's `data` node.
/// Accepts the MCU bridge's object form and a peer's plain string (plus
/// the "candiate" typo key some clients emit). Returns false when the
/// payload carries nothing usable, so the caller can log the drop.
inline bool ParseRemoteCandidate(const Json::Value& data,
                                 RemoteCandidate* out) {
    if (out) *out = RemoteCandidate{};
    const Json::Value& payload = data["payload"];
    if (!payload.isObject()) return false;

    const Json::Value& primary = payload["candidate"];
    const Json::Value& legacy = payload["candiate"];
    const Json::Value& node =
        (primary.isObject() || primary.isString()) ? primary : legacy;

    RemoteCandidate rc;
    if (node.isObject()) {
        const Json::Value& inner = node["candidate"];
        if (!inner.isString()) return false;
        rc.candidate = inner.asString();
        const Json::Value& mid = node["sdpMid"];
        if (mid.isString()) rc.mid = mid.asString();
    } else if (node.isString()) {
        rc.candidate = node.asString();
        const Json::Value& mid = payload["sdpMid"];
        if (mid.isString()) rc.mid = mid.asString();
    } else {
        return false;
    }

    TrimWhitespace(rc.candidate);
    TrimWhitespace(rc.mid);
    // The engine tolerates both prefixes; keep the bare form on the wire.
    if (rc.candidate.rfind("a=", 0) == 0) rc.candidate.erase(0, 2);
    if (rc.candidate.empty()) rc.endOfCandidates = true;
    if (out) *out = rc;
    return true;
}

/// Position of `mid` among the "a=mid:" attributes of an SDP (the m-line
/// index), or -1 when the SDP carries no such mid.
inline int MidToMlineIndex(const std::string& sdp, const std::string& mid) {
    if (mid.empty() || sdp.empty()) return -1;
    int index = -1;
    std::string line;
    std::istringstream iss(sdp);
    static const std::string kPrefix = "a=mid:";
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.compare(0, kPrefix.size(), kPrefix) == 0) {
            ++index;
            if (line.substr(kPrefix.size()) == mid) return index;
        }
    }
    return -1;
}

} // namespace animus::kernel::calls
