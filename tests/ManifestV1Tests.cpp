// Manifest v1 canonicalization + hash — daemon-side port pinned to the
// registry reference implementation. The fixture (alpaca.json) is byte-identical
// to animus-sop/spec/fixtures/manifests/alpaca.json; its content hash was
// computed by the Ruby side: 385a08b5bd784d0d68b94224d130d6d269ed69c9cb5513bd6585048f78ff501c

#include "animus_kernel/api/ManifestV1.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace animus::kernel;
using manifest_v1::Processed;

namespace {

int g_failures = 0;

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  ASSERT FAILED: " << msg << "\n";
        g_failures++;
    }
}

std::string ReadFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// The alpaca fixture manifest, shuffled: same content, keys in different
// insertion order. Key-order independence is the portability contract.
const char* kShuffledAlpaca = R"JSON({
  "commands": [
    {"kind": "action", "name": "get_account", "description": "Fetch the trading account",
     "request": {"url": "{{state.base_url}}/v2/account", "method": "GET"},
     "script": "function run(ctx) return {output = 'ok'} end"},
    {"kind": "action", "name": "list_positions", "description": "List open positions",
     "request": {"url": "{{state.base_url}}/v2/positions", "method": "GET"},
     "script": "function run(ctx) return {output = 'ok'} end"}
  ],
  "keywords": ["trading", "stocks", "market"],
  "state_schema": {
    "base_url": {"type": "string", "default": "https://paper-api.alpaca.markets"},
    "token": {"type": "string", "secret": true}
  },
  "description": "Alpaca paper + live trading API",
  "version": "0.1.0",
  "name": "alpaca",
  "kind": "api_package",
  "dispatch_cooldown_ms": 10000,
  "files_quota_mb": 256
})JSON";

void TestFixtureHash() {
    std::cerr << "  fixture alpaca.json hash agreement...\n";
    std::string raw = ReadFile("tests/fixtures/manifests/alpaca.json");
    Assert(!raw.empty(), "fixture readable");
    Processed p;
    std::string err;
    Assert(manifest_v1::Process(raw, p, err), "process ok: " + err);
    Assert(p.content_hash == "385a08b5bd784d0d68b94224d130d6d269ed69c9cb5513bd6585048f78ff501c",
           "hash matches Ruby reference: got " + p.content_hash);
}

void TestKeyOrderIndependence() {
    std::cerr << "  key-order independence (shuffled manifest, same hash)...\n";
    Processed a, b;
    std::string err;
    std::string raw = ReadFile("tests/fixtures/manifests/alpaca.json");
    Assert(manifest_v1::Process(raw, a, err), "original processes");
    Assert(manifest_v1::Process(kShuffledAlpaca, b, err), "shuffled processes: " + err);
    Assert(a.canonical == b.canonical, "canonical bytes identical");
    Assert(a.content_hash == b.content_hash, "hashes identical");
}

void TestFloatRejection() {
    std::cerr << "  float rejection...\n";
    Processed p;
    std::string err;
    const char* withFloat = R"JSON({"kind":"api_package","name":"x","version":"0.1.0","description":"d","bad":1.5})JSON";
    Assert(!manifest_v1::Process(withFloat, p, err), "float rejected");
    Assert(err.find("float") != std::string::npos, "error mentions float: " + err);
}

void TestCompactAndSorted() {
    std::cerr << "  compact output + key sorting...\n";
    const char* messy = R"JSON({ "b" : 2 , "a" : { "z" : [ 3 , 1 , 2 ] , "y" : null } , "c" : true })JSON";
    Json::CharReaderBuilder rb;
    Json::Value messyValue;
    std::istringstream messyStream(messy);
    std::string messyErr;
    Json::parseFromStream(rb, messyStream, &messyValue, &messyErr);
    std::string canonical = manifest_v1::Canonicalize(messyValue);
    Assert(canonical == R"({"a":{"y":null,"z":[3,1,2]},"b":2,"c":true})",
           "canonical form exact: " + canonical);
}

void TestUtf8PassThrough() {
    std::cerr << "  UTF-8 pass-through (emitUTF8)...\n";
    // é (U+00E9, C3 A9) and 🪶 (U+1F9B0? kestrel feather U+1F986, F0 9F A6 86)
    const char* utf8 = "{\"desc\":\"caf\\u00e9 \\u1F986\"}";
    Json::CharReaderBuilder rb;
    Json::Value v;
    std::istringstream ss(utf8);
    std::string e;
    Json::parseFromStream(rb, ss, &v, &e);
    std::string canonical = manifest_v1::Canonicalize(v);
    Assert(canonical.find("\xC3\xA9") != std::string::npos, "latin-1 supplement raw UTF-8");
    Assert(canonical.find("\xF0\x9F\xA6\x86") != std::string::npos, "astral plane raw UTF-8");
}

void TestLargeIntegers() {
    std::cerr << "  large integer fidelity (int64)...\n";
    const char* big = R"JSON({"n":9223372036854775807,"m":-9223372036854775808})JSON";
    Json::CharReaderBuilder rb;
    Json::Value v;
    std::istringstream ss(big);
    std::string e;
    Json::parseFromStream(rb, ss, &v, &e);
    std::string canonical = manifest_v1::Canonicalize(v);
    Assert(canonical.find("9223372036854775807") != std::string::npos, "int64 max preserved");
    Assert(canonical.find("-9223372036854775808") != std::string::npos, "int64 min preserved");
}

void TestSha256KnownVector() {
    std::cerr << "  sha256 known vector...\n";
    // sha256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    Assert(manifest_v1::Sha256Hex("abc") ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
           "sha256(abc) correct");
    Assert(manifest_v1::Sha256Hex("") ==
           "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
           "sha256(empty) correct");
}

}  // namespace

int main() {
    std::cerr << "Manifest v1 tests:\n";
    TestSha256KnownVector();
    TestCompactAndSorted();
    TestFixtureHash();
    TestKeyOrderIndependence();
    TestFloatRejection();
    TestUtf8PassThrough();
    TestLargeIntegers();
    if (g_failures == 0) std::cerr << "All manifest v1 tests passed.\n";
    else std::cerr << g_failures << " failures.\n";
    return g_failures == 0 ? 0 : 1;
}
