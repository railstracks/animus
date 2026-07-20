#pragma once

#include <string>
#include <unordered_map>
#include <chrono>
#include <random>
#include <mutex>

namespace animus::kernel {

// ============================================================================
// AttachmentTokenManager — short-lived per-attachment access tokens
// Prevents permanent admin tokens from leaking via <img src> URLs.
// In-memory, single-process. Tokens expire after a configurable TTL.
// ============================================================================

class AttachmentTokenManager {
public:
    explicit AttachmentTokenManager(std::chrono::seconds ttl = std::chrono::minutes(30))
        : m_ttl(ttl) {}

    /// Generate a short-lived token bound to a specific attachment ID
    std::string GenerateToken(const std::string& attachmentId) {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Lazy cleanup: sweep expired entries
        CleanupExpired();

        std::string token = GenerateRandomToken();
        auto expires = std::chrono::steady_clock::now() + m_ttl;
        m_tokens[token] = {attachmentId, expires};
        return token;
    }

    /// Validate a token against an attachment ID.
    /// Returns true if the token exists, hasn't expired, and matches the attachment.
    bool ValidateToken(const std::string& token, const std::string& attachmentId) {
        std::lock_guard<std::mutex> lock(m_mutex);

        auto it = m_tokens.find(token);
        if (it == m_tokens.end()) return false;

        // Check expiry
        if (std::chrono::steady_clock::now() >= it->second.expires_at) {
            m_tokens.erase(it);
            return false;
        }

        // Check binding
        return it->second.attachment_id == attachmentId;
    }

    /// Remove all expired tokens (can be called periodically)
    void Cleanup() {
        std::lock_guard<std::mutex> lock(m_mutex);
        CleanupExpired();
    }

private:
    struct TokenEntry {
        std::string attachment_id;
        std::chrono::steady_clock::time_point expires_at;
    };

    void CleanupExpired() {
        auto now = std::chrono::steady_clock::now();
        for (auto it = m_tokens.begin(); it != m_tokens.end();) {
            if (now >= it->second.expires_at) {
                it = m_tokens.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::string GenerateRandomToken() {
        static const char hex[] = "0123456789abcdef";
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);

        std::string token;
        token.reserve(32);
        for (int i = 0; i < 32; ++i) {
            token.push_back(hex[dis(gen)]);
        }
        return token;
    }

    std::mutex m_mutex;
    std::unordered_map<std::string, TokenEntry> m_tokens;
    std::chrono::seconds m_ttl;
};

} // namespace animus::kernel