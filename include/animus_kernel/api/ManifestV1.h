#pragma once
// ============================================================================
// Manifest v1 — daemon-side port of the registry's canonicalization + hash.
//
// MUST produce output identical to the registry reference implementation
// (animus-sop/app/services/manifest_v1.rb):
//   - JSON objects: keys sorted lexicographically (byte order)
//   - Compact separators, no insignificant whitespace
//   - Integers only — floats are rejected (Ruby <-> jsoncpp float formatting differs)
//   - UTF-8 pass-through, standard JSON string escaping
// Fixtures in tests/fixtures/manifests pin both sides (same file, same hash).
// ============================================================================

#include <json/json.h>
#include <string>

namespace animus::kernel::manifest_v1 {

struct Processed {
    Json::Value manifest;      // parsed manifest (original key order)
    std::string canonical;     // canonical serialization (sorted, compact)
    std::string content_hash;  // sha256 hex of canonical
};

// Parse + minimal lint (object root, no floats) + canonicalize + hash.
// Returns false and fills `err` on any failure.
bool Process(const std::string& raw, Processed& out, std::string& err);

// Canonicalize an already-parsed JSON value (recursive key sort, compact).
std::string Canonicalize(const Json::Value& value);

// SHA-256 hex digest (64 lowercase hex chars).
std::string Sha256Hex(const std::string& data);

}  // namespace animus::kernel::manifest_v1
