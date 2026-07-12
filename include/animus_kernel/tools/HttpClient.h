#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace animus::kernel {

// ============================================================================
// HttpClient — shared HTTP client for web tools
// ============================================================================

/// Simple HTTP client built on libcurl.
/// Used by http, web_fetch, and web_search tools.
/// Thread-safe: each request creates its own curl handle.
class HttpClient {
public:
    struct Response {
        int status_code{0};
        std::map<std::string, std::string> headers;
        std::string body;
        uint64_t elapsed_ms{0};
        std::string content_type;
        std::string error;
    };

    struct Request {
        std::string method{"GET"};
        std::string url;
        std::map<std::string, std::string> headers;
        std::string body;
        int timeout_seconds{30};
        int max_redirects{5};
        bool follow_redirects{true};
    };

    HttpClient() = default;
    ~HttpClient() = default;

    /// Execute an HTTP request. Returns a Response with status code,
    /// headers, body, and timing. On connection failure, status_code is 0
    /// and error is set.
    Response Execute(const Request& request);

    /// URL allowlist/denylist for SSRF prevention.
    /// Patterns use simple glob matching: * for wildcard.
    void SetAllowlist(const std::vector<std::string>& patterns);
    void SetDenylist(const std::vector<std::string>& patterns);
    bool IsUrlAllowed(const std::string& url) const;

    /// Maximum response body size (default 1MB).
    void SetMaxBodySize(std::size_t bytes);
    std::size_t GetMaxBodySize() const;

    /// TLS certificate verification.
    /// Default true — verify peer and host certificates.
    void SetTlsVerify(bool verify);
    bool GetTlsVerify() const;

    /// Whether to allow requests to private/internal addresses.
    /// Default false — blocks SSRF attacks.
    void SetAllowPrivateAddresses(bool allow);
    bool GetAllowPrivateAddresses() const;

private:
    std::vector<std::string> m_allowlist;
    std::vector<std::string> m_denylist;
    std::size_t m_maxBodySize{1024 * 1024}; // 1MB
    bool m_tlsVerify{true};
    bool m_allowPrivateAddresses{false};

    bool MatchPattern(const std::string& url, const std::string& pattern) const;

    /// Extract hostname from a URL string.
    std::string ExtractHostname(const std::string& url) const;

    /// Check if a hostname refers to a private/internal address.
    bool IsPrivateHostname(const std::string& hostname) const;
};

} // namespace animus::kernel