#include "animus_kernel/tools/HttpClient.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>

#include <curl/curl.h>

namespace animus::kernel {

namespace {

// Curl write callback — appends data to a string.
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    std::string* str = static_cast<std::string*>(userp);
    size_t totalSize = size * nmemb;
    str->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

// Header callback — collects response headers into a map.
size_t HeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    auto* headers = static_cast<std::map<std::string, std::string>*>(userdata);
    size_t totalSize = size * nitems;
    std::string line(buffer, totalSize);

    // Remove trailing \r\n
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }

    auto colonPos = line.find(": ");
    if (colonPos != std::string::npos) {
        std::string key = line.substr(0, colonPos);
        std::string value = line.substr(colonPos + 2);
        // Convert header key to lowercase for consistent lookup
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        (*headers)[key] = value;
    }

    return totalSize;
}

// Simple glob pattern matching for URL allowlist/denylist.
// Supports * as wildcard. Case-insensitive.
bool GlobMatch(const std::string& text, const std::string& pattern) {
    std::string s = text;
    std::string p = pattern;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    std::transform(p.begin(), p.end(), p.begin(), ::tolower);

    // DP approach for glob matching
    const auto n = s.size();
    const auto m = p.size();
    std::vector<std::vector<bool>> dp(n + 1, std::vector<bool>(m + 1, false));
    dp[0][0] = true;

    // Handle leading *s
    for (size_t j = 1; j <= m && p[j - 1] == '*'; ++j) {
        dp[0][j] = true;
    }

    for (size_t i = 1; i <= n; ++i) {
        for (size_t j = 1; j <= m; ++j) {
            if (p[j - 1] == '*') {
                dp[i][j] = dp[i][j - 1] || dp[i - 1][j];
            } else if (p[j - 1] == '?' || p[j - 1] == s[i - 1]) {
                dp[i][j] = dp[i - 1][j - 1];
            }
        }
    }

    return dp[n][m];
}

} // anonymous namespace

// ============================================================================
// Execute
// ============================================================================

HttpClient::Response HttpClient::Execute(const Request& request) {
    Response response;

    // SSRF check
    if (!IsUrlAllowed(request.url)) {
        response.error = "URL blocked by security policy: " + request.url;
        return response;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "Failed to initialize curl handle";
        return response;
    }

    auto startTime = std::chrono::steady_clock::now();

    // URL
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());

    // Method
    if (request.method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
    } else if (request.method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    } else if (request.method == "PATCH") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    } else if (request.method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else if (request.method == "HEAD") {
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    } else {
        // GET (default)
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    }

    // Body (for POST/PUT/PATCH)
    if (!request.body.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
    }

    // Headers
    curl_slist* headerList = nullptr;
    for (const auto& [key, value] : request.headers) {
        std::string header = key + ": " + value;
        headerList = curl_slist_append(headerList, header.c_str());
    }
    if (headerList) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    }

    // Timeouts
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L); // 10s connect timeout
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(request.timeout_seconds) * 1000L);

    // Redirects
    if (request.follow_redirects) {
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, static_cast<long>(request.max_redirects));
    }

    // Response body
    std::string responseBody;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);

    // Response headers
    std::map<std::string, std::string> responseHeaders;
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &responseHeaders);

    // User-Agent
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Animus/1.0");

    // TLS verification — secure by default, configurable via kernel config
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, m_tlsVerify ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, m_tlsVerify ? 2L : 0L);

    // Perform
    CURLcode res = curl_easy_perform(curl);

    auto endTime = std::chrono::steady_clock::now();
    response.elapsed_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count());

    if (res != CURLE_OK) {
        response.error = "HTTP request failed: " + std::string(curl_easy_strerror(res));
        curl_slist_free_all(headerList);
        curl_easy_cleanup(curl);
        return response;
    }

    // Status code
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    response.status_code = static_cast<int>(httpCode);

    // Content-Type
    char* contentType = nullptr;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &contentType);
    if (contentType) {
        response.content_type = contentType;
    }

    // Body size limit
    if (responseBody.size() > m_maxBodySize) {
        responseBody.resize(m_maxBodySize);
        responseBody += "\n... (truncated at " + std::to_string(m_maxBodySize) + " bytes)";
    }

    response.body = std::move(responseBody);
    response.headers = std::move(responseHeaders);

    curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);

    return response;
}

// ============================================================================
// SSRF protection
// ============================================================================

void HttpClient::SetAllowlist(const std::vector<std::string>& patterns) {
    m_allowlist = patterns;
}

void HttpClient::SetDenylist(const std::vector<std::string>& patterns) {
    m_denylist = patterns;
}

bool HttpClient::IsUrlAllowed(const std::string& url) const {
    // If allowlist is set, URL must match at least one pattern
    if (!m_allowlist.empty()) {
        bool allowed = false;
        for (const auto& pattern : m_allowlist) {
            if (MatchPattern(url, pattern)) {
                allowed = true;
                break;
            }
        }
        if (!allowed) return false;
    }

    // If denylist is set, URL must not match any pattern
    for (const auto& pattern : m_denylist) {
        if (MatchPattern(url, pattern)) {
            return false;
        }
    }

    // SSRF protection: check hostname against private/internal address ranges.
    // Extract hostname from URL and validate it's not a private address.
    if (!m_allowPrivateAddresses) {
        std::string hostname = ExtractHostname(url);
        if (IsPrivateHostname(hostname)) {
            // Private address — only allowed if explicitly in allowlist
            if (!m_allowlist.empty()) {
                for (const auto& pattern : m_allowlist) {
                    if (MatchPattern(url, pattern)) return true;
                }
            }
            return false;
        }
    }

    return true;
}

bool HttpClient::MatchPattern(const std::string& url, const std::string& pattern) const {
    return GlobMatch(url, pattern);
}

void HttpClient::SetMaxBodySize(std::size_t bytes) {
    m_maxBodySize = bytes;
}

std::size_t HttpClient::GetMaxBodySize() const {
    return m_maxBodySize;
}


// ============================================================================
// TLS and private address configuration
// ============================================================================

void HttpClient::SetTlsVerify(bool verify) {
    m_tlsVerify = verify;
}

bool HttpClient::GetTlsVerify() const {
    return m_tlsVerify;
}

void HttpClient::SetAllowPrivateAddresses(bool allow) {
    m_allowPrivateAddresses = allow;
}

bool HttpClient::GetAllowPrivateAddresses() const {
    return m_allowPrivateAddresses;
}

// ============================================================================
// URL hostname extraction and private address detection
// ============================================================================

std::string HttpClient::ExtractHostname(const std::string& url) const {
    // Extract hostname from URL: scheme://host:port/path
    // Find the start of the host (after ://)
    auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) {
        // No scheme — treat entire string as host (defensive)
        return url;
    }

    std::size_t hostStart = schemeEnd + 3;

    // Skip userinfo (user:pass@host) if present
    auto atPos = url.find('@', hostStart);
    if (atPos != std::string::npos && atPos < url.find('/', hostStart)) {
        hostStart = atPos + 1;
    }

    // Find end of host (first of : / ? # or end of string)
    std::size_t hostEnd = url.size();
    for (std::size_t i = hostStart; i < url.size(); ++i) {
        char c = url[i];
        if (c == ':' || c == '/' || c == '?' || c == '#') {
            hostEnd = i;
            break;
        }
    }

    return url.substr(hostStart, hostEnd - hostStart);
}

bool HttpClient::IsPrivateHostname(const std::string& hostname) const {
    if (hostname.empty()) return false;

    // Lowercase for comparison
    std::string lower = hostname;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // Loopback
    if (lower == "localhost" || lower == "127.0.0.1" || lower == "::1" ||
        lower == "0.0.0.0" || lower == "[::1]") {
        return true;
    }

    // 127.x.x.x — entire loopback range
    if (lower.substr(0, 4) == "127.") {
        return true;
    }

    // 10.x.x.x — RFC 1918
    if (lower.substr(0, 3) == "10.") {
        return true;
    }

    // 172.16-31.x.x — RFC 1918
    if (lower.substr(0, 4) == "172.") {
        // Check second octet: 16-31
        auto parts = lower.substr(4);
        auto dotPos = parts.find('.');
        if (dotPos != std::string::npos) {
            try {
                int secondOctet = std::stoi(parts.substr(0, dotPos));
                if (secondOctet >= 16 && secondOctet <= 31) return true;
            } catch (...) {}
        }
    }

    // 192.168.x.x — RFC 1918
    if (lower.substr(0, 8) == "192.168.") {
        return true;
    }

    // 169.254.x.x — link-local / cloud metadata (AWS, GCP, Azure)
    if (lower.substr(0, 8) == "169.254.") {
        return true;
    }

    // 100.64-127.x.x — Carrier-grade NAT (RFC 6598)
    if (lower.substr(0, 4) == "100.") {
        auto parts = lower.substr(4);
        auto dotPos = parts.find('.');
        if (dotPos != std::string::npos) {
            try {
                int secondOctet = std::stoi(parts.substr(0, dotPos));
                if (secondOctet >= 64 && secondOctet <= 127) return true;
            } catch (...) {}
        }
    }

    // fc00::/7 — IPv6 unique local
    if (lower.substr(0, 3) == "fc" || lower.substr(0, 3) == "fd") {
        // More precise check: fc00-fcff or fd00-fdff
        if (lower.size() >= 4 && lower[3] == ':') {
            char hex1 = lower[2];
            if ((hex1 >= '0' && hex1 <= '9') || (hex1 >= 'a' && hex1 <= 'f')) {
                return true;
            }
        }
    }

    // fe80:: — IPv6 link-local
    if (lower.substr(0, 5) == "fe80:") {
        return true;
    }

    // ::1 — IPv6 loopback (already handled above but defensive)
    if (lower == "::1" || lower == "0:0:0:0:0:0:0:1") {
        return true;
    }

    return false;
}

} // namespace animus::kernel