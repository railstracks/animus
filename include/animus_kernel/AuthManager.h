#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <optional>
#include "animus_kernel/AuthStore.h"

namespace animus::kernel {

// ============================================================================
// AuthResult — outcome of an authentication attempt
// ============================================================================
enum class AuthResult {
    Ok,
    InvalidToken,
    NoTokenProvided,
    AuthNotRequired,
    RateLimited,
    InternalError,
};

// ============================================================================
// AuthManager — business logic layer above AuthStore
//
// Handles:
//   - Static token validation (ANIMUS_AUTH_TOKEN env var)
//   - User session token validation
//   - Password hashing (SHA-256 + salt, not bcrypt — avoids dependency)
//   - Rate limiting (in-memory, per-IP)
//   - Token generation
// ============================================================================
class AuthManager {
public:
    AuthManager();

    // Configure
    void SetStaticToken(const std::string& token);
    void SetAuthStore(AuthStore* store);
    void SetRequireAuth(bool required);

    // Check if authentication is needed at all
    bool IsAuthRequired() const;

    // Validate a bearer token from Authorization header.
    // Returns AuthResult and optionally the user ID (if session token).
    std::pair<AuthResult, std::string> ValidateToken(const std::string& bearerToken);

    // Rate limiting — call on failed auth.
    // Returns true if the IP is now blocked.
    bool RecordFailure(const std::string& ip);
    bool IsRateLimited(const std::string& ip);
    void RecordSuccess(const std::string& ip);

    // User management
    std::string GenerateUserId();
    std::string GenerateSessionToken();  // raw token (return to client)
    std::string HashToken(const std::string& rawToken);  // for storage/lookup
    std::string HashPassword(const std::string& password, const std::string& salt = "");
    std::string GenerateSalt();
    bool VerifyPassword(const std::string& password, const std::string& salt, const std::string& hash);

    // User CRUD (delegates to store)
    std::optional<AuthUser> GetUserByUsername(const std::string& username);
    std::optional<AuthUser> GetUserById(const std::string& id);
    std::vector<AuthUser> ListUsers();
    bool CreateUser(const std::string& username, const std::string& password, const std::string& role);
    bool UpdateUserPassword(const std::string& id, const std::string& newPassword);
    bool UpdateUserRole(const std::string& id, const std::string& role);
    bool DeleteUser(const std::string& id);

    // Token management
    bool CreateSessionToken(const std::string& userId, int64_t ttlMs, std::string& outRawToken);
    bool RevokeToken(const std::string& tokenHash);
    bool RevokeAllTokensForUser(const std::string& userId);
    std::vector<AuthToken> ListTokensForUser(const std::string& userId);

    // Status
    bool HasUsers() const;

private:
    std::string m_staticToken;
    AuthStore* m_store{nullptr};
    bool m_requireAuth{false};

    // Rate limiting: IP → {failures timestamps, blocked_until}
    struct RateLimitEntry {
        std::vector<std::chrono::steady_clock::time_point> failures;
        std::chrono::steady_clock::time_point blockedUntil;
    };
    mutable std::mutex m_rateLimitMutex;
    std::unordered_map<std::string, RateLimitEntry> m_rateLimit;

    static constexpr int kMaxFailures = 50;
    static constexpr int kRateLimitWindowSec = 60;
    static constexpr int kRateLimitBlockSec = 60;
};

} // namespace animus::kernel
