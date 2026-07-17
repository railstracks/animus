#include "animus_kernel/AuthManager.h"

#include <sstream>
#include <iomanip>
#include <random>
#include <cstring>

// SHA-256 via OpenSSL (available on all Linux/Docker targets)
#include <openssl/sha.h>

namespace animus::kernel {

// ============================================================================
// Helpers
// ============================================================================

static std::string ToHex(const unsigned char* data, size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

static std::string Sha256Hex(const std::string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), hash);
    return ToHex(hash, SHA256_DIGEST_LENGTH);
}

static std::string RandomHex(size_t bytes) {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);

    std::string raw;
    raw.reserve(bytes);
    for (size_t i = 0; i < bytes; ++i) {
        raw.push_back(static_cast<char>(dis(gen)));
    }
    // Hash to get hex output
    return Sha256Hex(raw);
}

static std::string ConstantTimeCompare(const std::string& a, const std::string& b) {
    // Returns empty string if equal, non-empty if different
    if (a.size() != b.size()) return "mismatch";
    int result = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        result |= a[i] ^ b[i];
    }
    return result == 0 ? "" : "mismatch";
}

// ============================================================================
// AuthManager
// ============================================================================

AuthManager::AuthManager() = default;

void AuthManager::SetStaticToken(const std::string& token) {
    m_staticToken = token;
    m_requireAuth = !token.empty();
}

void AuthManager::SetAuthStore(AuthStore* store) {
    m_store = store;
}

void AuthManager::SetRequireAuth(bool required) {
    m_requireAuth = required;
}

bool AuthManager::IsAuthRequired() const {
    return m_requireAuth;
}

std::pair<AuthResult, std::string> AuthManager::ValidateToken(const std::string& bearerToken) {
    if (!m_requireAuth) {
        return {AuthResult::AuthNotRequired, ""};
    }

    if (bearerToken.empty()) {
        return {AuthResult::NoTokenProvided, ""};
    }

    // Check static token first
    if (!m_staticToken.empty()) {
        if (ConstantTimeCompare(bearerToken, m_staticToken).empty()) {
            return {AuthResult::Ok, ""};  // Static token — no user ID
        }
    }

    // Check session token
    if (m_store) {
        std::string tokenHash = Sha256Hex(bearerToken);
        auto token = m_store->GetToken(tokenHash);
        if (token) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            if (token->expires_at > now) {
                return {AuthResult::Ok, token->user_id};
            }
            // Expired — clean up
            m_store->DeleteToken(tokenHash);
        }
    }

    return {AuthResult::InvalidToken, ""};
}

// ---- Rate limiting ----

bool AuthManager::RecordFailure(const std::string& ip) {
    std::lock_guard<std::mutex> lock(m_rateLimitMutex);
    auto& entry = m_rateLimit[ip];
    auto now = std::chrono::steady_clock::now();

    // Remove failures outside the window
    auto windowStart = now - std::chrono::seconds(kRateLimitWindowSec);
    entry.failures.erase(
        std::remove_if(entry.failures.begin(), entry.failures.end(),
                        [windowStart](const auto& ts) { return ts < windowStart; }),
        entry.failures.end());

    entry.failures.push_back(now);

    if (static_cast<int>(entry.failures.size()) >= kMaxFailures) {
        entry.blockedUntil = now + std::chrono::seconds(kRateLimitBlockSec);
        return true;
    }
    return false;
}

bool AuthManager::IsRateLimited(const std::string& ip) {
    std::lock_guard<std::mutex> lock(m_rateLimitMutex);
    auto it = m_rateLimit.find(ip);
    if (it == m_rateLimit.end()) return false;

    auto now = std::chrono::steady_clock::now();
    if (now < it->second.blockedUntil) {
        return true;
    }

    // Block expired — clear
    if (now >= it->second.blockedUntil && it->second.blockedUntil.time_since_epoch().count() > 0) {
        it->second.failures.clear();
        it->second.blockedUntil = {};
    }

    return false;
}

void AuthManager::RecordSuccess(const std::string& ip) {
    std::lock_guard<std::mutex> lock(m_rateLimitMutex);
    m_rateLimit.erase(ip);
}

// ---- ID/Token generation ----

std::string AuthManager::GenerateUserId() {
    return RandomHex(16);
}

std::string AuthManager::GenerateSessionToken() {
    return RandomHex(32);  // 64 hex chars
}

std::string AuthManager::HashToken(const std::string& rawToken) {
    return Sha256Hex(rawToken);
}

std::string AuthManager::HashPassword(const std::string& password, const std::string& salt) {
    return Sha256Hex(salt + password);
}

std::string AuthManager::GenerateSalt() {
    return RandomHex(8);  // 16 hex chars
}

bool AuthManager::VerifyPassword(const std::string& password, const std::string& salt, const std::string& hash) {
    auto computed = HashPassword(password, salt);
    return ConstantTimeCompare(computed, hash).empty();
}

// ---- User CRUD ----

std::optional<AuthUser> AuthManager::GetUserByUsername(const std::string& username) {
    if (!m_store) return std::nullopt;
    return m_store->GetUserByUsername(username);
}

std::optional<AuthUser> AuthManager::GetUserById(const std::string& id) {
    if (!m_store) return std::nullopt;
    return m_store->GetUserById(id);
}

std::vector<AuthUser> AuthManager::ListUsers() {
    if (!m_store) return {};
    return m_store->ListUsers();
}

bool AuthManager::CreateUser(const std::string& username, const std::string& password, const std::string& role) {
    if (!m_store) return false;

    auto salt = GenerateSalt();
    auto hash = HashPassword(password, salt);

    AuthUser user;
    user.id = GenerateUserId();
    user.username = username;
    user.password_hash = salt + ":" + hash;  // Store salt with hash
    user.role = role.empty() ? "admin" : role;
    user.created_at = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    return m_store->CreateUser(user);
}

bool AuthManager::UpdateUserPassword(const std::string& id, const std::string& newPassword) {
    if (!m_store) return false;
    auto salt = GenerateSalt();
    auto hash = HashPassword(newPassword, salt);
    return m_store->UpdateUserPassword(id, salt + ":" + hash);
}

bool AuthManager::UpdateUserRole(const std::string& id, const std::string& role) {
    if (!m_store) return false;
    return m_store->UpdateUserRole(id, role);
}

bool AuthManager::DeleteUser(const std::string& id) {
    if (!m_store) return false;
    return m_store->DeleteUser(id);
}

// ---- Token management ----

bool AuthManager::CreateSessionToken(const std::string& userId, int64_t ttlMs, std::string& outRawToken) {
    if (!m_store) return false;

    outRawToken = GenerateSessionToken();
    auto hash = HashToken(outRawToken);

    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    AuthToken token;
    token.token_hash = hash;
    token.user_id = userId;
    token.created_at = now;
    token.expires_at = now + ttlMs;

    return m_store->CreateToken(token);
}

bool AuthManager::RevokeToken(const std::string& tokenHash) {
    if (!m_store) return false;
    return m_store->DeleteToken(tokenHash);
}

bool AuthManager::RevokeAllTokensForUser(const std::string& userId) {
    if (!m_store) return false;
    return m_store->DeleteTokensForUser(userId);
}

std::vector<AuthToken> AuthManager::ListTokensForUser(const std::string& userId) {
    if (!m_store) return {};
    return m_store->ListTokensForUser(userId);
}

bool AuthManager::HasUsers() const {
    return m_store && m_store->HasUsers();
}

} // namespace animus::kernel
