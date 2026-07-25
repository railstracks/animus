// WhatsAppVersionResolver.cpp — Dynamic WhatsApp Web version resolution
// ============================================================================
#include "animus_kernel/whatsapp/WhatsAppVersionResolver.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/md5.h>
#include <openssl/rand.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#include <cstring>
#include <cerrno>
#include <regex>
#include <sstream>
#include <iostream>

namespace animus::whatsapp {

// Static members
std::mutex WhatsAppVersionResolver::mutex_;
WhatsAppVersionInfo WhatsAppVersionResolver::cached_;
std::chrono::steady_clock::time_point WhatsAppVersionResolver::cacheTime_;

// ---------------------------------------------------------------------------
// get() — cached entry point (thread-safe)
// ---------------------------------------------------------------------------
const WhatsAppVersionInfo& WhatsAppVersionResolver::get() {
    std::lock_guard<std::mutex> lock(mutex_);

    auto now = std::chrono::steady_clock::now();
    bool cacheFresh = (cached_.tertiary > 0 &&
                       (now - cacheTime_) < CACHE_TTL);

    if (!cacheFresh) {
        cached_ = doFetch();
        if (cached_.fetched) {
            std::cerr << "[wa-version] Fetched version " << cached_.versionString
                      << " from CDN" << std::endl;
        } else {
            std::cerr << "[wa-version] CDN fetch failed, using fallback: "
                      << cached_.versionString << std::endl;
        }
        cacheTime_ = now;
    }

    return cached_;
}

// ---------------------------------------------------------------------------
// refresh() — force a fresh fetch
// ---------------------------------------------------------------------------
WhatsAppVersionInfo WhatsAppVersionResolver::refresh() {
    std::lock_guard<std::mutex> lock(mutex_);
    cached_ = doFetch();
    cacheTime_ = std::chrono::steady_clock::now();
    if (cached_.fetched) {
        std::cerr << "[wa-version] Refreshed version " << cached_.versionString
                  << " from CDN" << std::endl;
    } else {
        std::cerr << "[wa-version] Refresh failed, using fallback: "
                  << cached_.versionString << std::endl;
    }
    return cached_;
}

// ---------------------------------------------------------------------------
// doFetch() — fetch sw.js and extract client_revision
// ---------------------------------------------------------------------------
WhatsAppVersionInfo WhatsAppVersionResolver::doFetch() {
    constexpr const char* HOST = "web.whatsapp.com";
    constexpr const char* PATH = "/sw.js";
    constexpr int PORT = 443;

    // --- TCP connect ---
    struct addrinfo hints{}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(HOST, std::to_string(PORT).c_str(), &hints, &result);
    if (rc != 0) {
        std::cerr << "[wa-version] DNS failed: " << gai_strerror(rc) << std::endl;
        return fallback();
    }

    int fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(result);
        return fallback();
    }

    // 10s connect timeout
    struct timeval tv { .tv_sec = 10, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (::connect(fd, result->ai_addr, result->ai_addrlen) != 0) {
        std::cerr << "[wa-version] TCP connect failed: " << strerror(errno) << std::endl;
        ::close(fd);
        freeaddrinfo(result);
        return fallback();
    }
    freeaddrinfo(result);

    // --- TLS handshake ---
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) { ::close(fd); return fallback(); }
    SSL_CTX_set_default_verify_paths(ctx);
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

    SSL* ssl = SSL_new(ctx);
    if (!ssl) { SSL_CTX_free(ctx); ::close(fd); return fallback(); }

    SSL_set_tlsext_host_name(ssl, HOST);
    SSL_set_fd(ssl, fd);

    if (SSL_connect(ssl) != 1) {
        std::cerr << "[wa-version] TLS handshake failed" << std::endl;
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        ::close(fd);
        return fallback();
    }

    // --- HTTP GET ---
    std::string req =
        "GET " PATH " HTTP/1.1\r\n"
        "Host: " HOST "\r\n"
        "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36\r\n"
        "sec-fetch-site: none\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n";

    if (SSL_write(ssl, req.data(), static_cast<int>(req.size())) <= 0) {
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        ::close(fd);
        return fallback();
    }

    // --- Read response ---
    std::string body;
    char buf[4096];
    bool headersDone = false;

    while (true) {
        int n = SSL_read(ssl, buf, sizeof(buf));
        if (n <= 0) break;

        if (!headersDone) {
            body.append(buf, n);
            size_t hdrEnd = body.find("\r\n\r\n");
            if (hdrEnd != std::string::npos) {
                // Check status line
                if (body.find("200") == std::string::npos) {
                    std::cerr << "[wa-version] Non-200 response" << std::endl;
                    SSL_free(ssl);
                    SSL_CTX_free(ctx);
                    ::close(fd);
                    return fallback();
                }
                body = body.substr(hdrEnd + 4);
                headersDone = true;
            }
        } else {
            body.append(buf, n);
        }
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    ::close(fd);

    if (body.empty()) {
        std::cerr << "[wa-version] Empty response body" << std::endl;
        return fallback();
    }

    // --- Extract client_revision ---
    // Pattern: "client_revision":NUMBER  or  client_revision\":NUMBER
    std::regex revRegex(R"(client_revision["\\]*\D+(\d+))");
    std::smatch match;
    if (!std::regex_search(body, match, revRegex) || match.size() < 2) {
        std::cerr << "[wa-version] Could not find client_revision in sw.js" << std::endl;
        return fallback();
    }

    uint64_t tertiary = std::stoull(match[1].str());
    if (tertiary < 1000000000) {
        std::cerr << "[wa-version] Suspiciously low revision: " << tertiary << std::endl;
        return fallback();
    }

    // --- Build version info ---
    WhatsAppVersionInfo info;
    info.primary = 2;
    info.secondary = 3000;
    info.tertiary = tertiary;
    info.versionString = "2.3000." + std::to_string(tertiary);
    info.fetched = true;

    // MD5 of version string → buildHash
    info.buildHash.resize(MD5_DIGEST_LENGTH);
    MD5(reinterpret_cast<const unsigned char*>(info.versionString.data()),
        info.versionString.size(),
        info.buildHash.data());

    return info;
}

// ---------------------------------------------------------------------------
// fallback() — hardcoded last-known-good version
// ---------------------------------------------------------------------------
WhatsAppVersionInfo WhatsAppVersionResolver::fallback() {
    WhatsAppVersionInfo info;
    info.primary = 2;
    info.secondary = 3000;
    info.tertiary = FALLBACK_TERTIARY;
    info.versionString = "2.3000." + std::to_string(FALLBACK_TERTIARY);
    info.fetched = false;

    info.buildHash.resize(MD5_DIGEST_LENGTH);
    MD5(reinterpret_cast<const unsigned char*>(info.versionString.data()),
        info.versionString.size(),
        info.buildHash.data());

    return info;
}

} // namespace animus::whatsapp
