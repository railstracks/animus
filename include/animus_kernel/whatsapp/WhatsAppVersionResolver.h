#pragma once
// WhatsAppVersionResolver.h — Dynamic WhatsApp Web version resolution
//
// Fetches the current WhatsApp Web client revision from web.whatsapp.com/sw.js
// (the service worker script). The script contains a JSON blob with
// "client_revision": <number> which is the tertiary version component.
//
// This replaces the fragile approach of hardcoding a version that becomes
// stale within hours. The sw.js endpoint is lightweight (~11KB), doesn't
// require cookies, and isn't geoblocked (unlike the main page).
//
// Fallback: if the fetch fails, returns the last known good version.
// ============================================================================
#include <string>
#include <vector>
#include <cstdint>
#include <mutex>
#include <chrono>

namespace animus::whatsapp {

struct WhatsAppVersionInfo {
    int primary = 2;
    int secondary = 3000;
    uint64_t tertiary = 0;
    std::string versionString;         // "2.3000.1043853937"
    std::vector<uint8_t> buildHash;    // MD5 of versionString (16 bytes)
    bool fetched = false;              // true if successfully fetched from CDN
};

class WhatsAppVersionResolver {
public:
    /// Returns the current version, fetching from CDN on first call (thread-safe).
    /// Subsequent calls return cached value. Cache TTL: 4 hours.
    static const WhatsAppVersionInfo& get();

    /// Force a fresh fetch (e.g., on reconnect after rejection).
    static WhatsAppVersionInfo refresh();

private:
    static WhatsAppVersionInfo doFetch();
    static WhatsAppVersionInfo fallback();

    static std::mutex mutex_;
    static WhatsAppVersionInfo cached_;
    static std::chrono::steady_clock::time_point cacheTime_;
    static constexpr auto CACHE_TTL = std::chrono::hours(4);

    // Last known good version (updated manually when we learn of changes)
    static constexpr uint64_t FALLBACK_TERTIARY = 1043853937;
};

} // namespace animus::whatsapp
