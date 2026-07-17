#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <optional>
#include <cstdint>

namespace animus::kernel {

// ============================================================================
// AuthUser — a user account row
// ============================================================================
struct AuthUser {
    std::string id;
    std::string username;
    std::string password_hash;
    std::string role;       // "admin", "viewer"
    int64_t created_at;     // unix ms
};

// ============================================================================
// AuthToken — a session token row
// ============================================================================
struct AuthToken {
    std::string token_hash;     // SHA-256 hex of the raw token
    std::string user_id;
    int64_t expires_at;         // unix ms
    int64_t created_at;         // unix ms
};

// ============================================================================
// AuthStore — persistence for users and tokens
// ============================================================================
class IDataStore;

class AuthStore {
public:
    explicit AuthStore(IDataStore* store);

    // Schema
    void EnsureSchema();

    // ---- Users ----
    std::optional<AuthUser> GetUserByUsername(const std::string& username);
    std::optional<AuthUser> GetUserById(const std::string& id);
    std::vector<AuthUser> ListUsers();
    bool CreateUser(const AuthUser& user);
    bool UpdateUserPassword(const std::string& id, const std::string& passwordHash);
    bool UpdateUserRole(const std::string& id, const std::string& role);
    bool DeleteUser(const std::string& id);

    // ---- Tokens ----
    bool CreateToken(const AuthToken& token);
    std::optional<AuthToken> GetToken(const std::string& tokenHash);
    bool DeleteToken(const std::string& tokenHash);
    bool DeleteTokensForUser(const std::string& userId);
    std::vector<AuthToken> ListTokensForUser(const std::string& userId);
    bool CleanExpiredTokens();

    // ---- Utility ----
    int UserCount();
    bool HasUsers();

private:
    IDataStore* m_store;
};

} // namespace animus::kernel
