#include "animus_kernel/AuthStore.h"
#include "animus_kernel/IDataStore.h"
#include "animus_kernel/SchemaHelpers.h"

#include <cstring>

namespace animus::kernel {

// ============================================================================
// AuthStore
// ============================================================================

AuthStore::AuthStore(IDataStore* store)
    : m_store(store) {
    EnsureSchema();
}

void AuthStore::EnsureSchema() {
    if (!m_store) return;

    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS auth_users (
            id              TEXT PRIMARY KEY,
            username        TEXT UNIQUE NOT NULL,
            password_hash   TEXT NOT NULL,
            role            TEXT NOT NULL DEFAULT 'admin',
            created_at      BIGINT NOT NULL DEFAULT 0
        );
    )");

    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS auth_tokens (
            token_hash      TEXT PRIMARY KEY,
            user_id         TEXT NOT NULL,
            expires_at      BIGINT NOT NULL,
            created_at      BIGINT NOT NULL
        );
    )");

    schema::CreateTable(m_store, R"(
        CREATE INDEX IF NOT EXISTS idx_auth_tokens_user
        ON auth_tokens(user_id);
    )");

    schema::CreateTable(m_store, R"(
        CREATE INDEX IF NOT EXISTS idx_auth_tokens_expires
        ON auth_tokens(expires_at);
    )");
}

// ---- Users ----

std::optional<AuthUser> AuthStore::GetUserByUsername(const std::string& username) {
    if (!m_store) return std::nullopt;
    auto stmt = m_store->Prepare("SELECT id, username, password_hash, role, created_at FROM auth_users WHERE username = ?");
    stmt->BindText(1, username);
    if (!stmt->Step()) return std::nullopt;

    AuthUser u;
    u.id = stmt->ColumnText(0);
    u.username = stmt->ColumnText(1);
    u.password_hash = stmt->ColumnText(2);
    u.role = stmt->ColumnText(3);
    u.created_at = stmt->ColumnInt64(4);
    return u;
}

std::optional<AuthUser> AuthStore::GetUserById(const std::string& id) {
    if (!m_store) return std::nullopt;
    auto stmt = m_store->Prepare("SELECT id, username, password_hash, role, created_at FROM auth_users WHERE id = ?");
    stmt->BindText(1, id);
    if (!stmt->Step()) return std::nullopt;

    AuthUser u;
    u.id = stmt->ColumnText(0);
    u.username = stmt->ColumnText(1);
    u.password_hash = stmt->ColumnText(2);
    u.role = stmt->ColumnText(3);
    u.created_at = stmt->ColumnInt64(4);
    return u;
}

std::vector<AuthUser> AuthStore::ListUsers() {
    std::vector<AuthUser> result;
    if (!m_store) return result;
    auto stmt = m_store->Prepare("SELECT id, username, password_hash, role, created_at FROM auth_users ORDER BY created_at");
    while (stmt->Step()) {
        AuthUser u;
        u.id = stmt->ColumnText(0);
        u.username = stmt->ColumnText(1);
        u.password_hash = stmt->ColumnText(2);
        u.role = stmt->ColumnText(3);
        u.created_at = stmt->ColumnInt64(4);
        result.push_back(std::move(u));
    }
    return result;
}

bool AuthStore::CreateUser(const AuthUser& user) {
    if (!m_store) return false;
    auto stmt = m_store->Prepare(
        "INSERT INTO auth_users (id, username, password_hash, role, created_at) VALUES (?, ?, ?, ?, ?)");
    stmt->BindText(1, user.id);
    stmt->BindText(2, user.username);
    stmt->BindText(3, user.password_hash);
    stmt->BindText(4, user.role);
    stmt->BindInt64(5, user.created_at);
    if (!stmt->Exec()) return false;
    return schema::DidAffectRows(m_store);
}

bool AuthStore::UpdateUserPassword(const std::string& id, const std::string& passwordHash) {
    if (!m_store) return false;
    auto stmt = m_store->Prepare("UPDATE auth_users SET password_hash = ? WHERE id = ?");
    stmt->BindText(1, passwordHash);
    stmt->BindText(2, id);
    return stmt->Exec();
}

bool AuthStore::UpdateUserRole(const std::string& id, const std::string& role) {
    if (!m_store) return false;
    auto stmt = m_store->Prepare("UPDATE auth_users SET role = ? WHERE id = ?");
    stmt->BindText(1, role);
    stmt->BindText(2, id);
    return stmt->Exec();
}

bool AuthStore::DeleteUser(const std::string& id) {
    if (!m_store) return false;
    // Delete tokens first
    DeleteTokensForUser(id);
    auto stmt = m_store->Prepare("DELETE FROM auth_users WHERE id = ?");
    stmt->BindText(1, id);
    return stmt->Exec();
}

// ---- Tokens ----

bool AuthStore::CreateToken(const AuthToken& token) {
    if (!m_store) return false;
    auto stmt = m_store->Prepare(
        "INSERT INTO auth_tokens (token_hash, user_id, expires_at, created_at) VALUES (?, ?, ?, ?)");
    stmt->BindText(1, token.token_hash);
    stmt->BindText(2, token.user_id);
    stmt->BindInt64(3, token.expires_at);
    stmt->BindInt64(4, token.created_at);
    return stmt->Exec();
}

std::optional<AuthToken> AuthStore::GetToken(const std::string& tokenHash) {
    if (!m_store) return std::nullopt;
    auto stmt = m_store->Prepare(
        "SELECT token_hash, user_id, expires_at, created_at FROM auth_tokens WHERE token_hash = ?");
    stmt->BindText(1, tokenHash);
    if (!stmt->Step()) return std::nullopt;

    AuthToken t;
    t.token_hash = stmt->ColumnText(0);
    t.user_id = stmt->ColumnText(1);
    t.expires_at = stmt->ColumnInt64(2);
    t.created_at = stmt->ColumnInt64(3);
    return t;
}

bool AuthStore::DeleteToken(const std::string& tokenHash) {
    if (!m_store) return false;
    auto stmt = m_store->Prepare("DELETE FROM auth_tokens WHERE token_hash = ?");
    stmt->BindText(1, tokenHash);
    return stmt->Exec();
}

bool AuthStore::DeleteTokensForUser(const std::string& userId) {
    if (!m_store) return false;
    auto stmt = m_store->Prepare("DELETE FROM auth_tokens WHERE user_id = ?");
    stmt->BindText(1, userId);
    return stmt->Exec();
}

std::vector<AuthToken> AuthStore::ListTokensForUser(const std::string& userId) {
    std::vector<AuthToken> result;
    if (!m_store) return result;
    auto stmt = m_store->Prepare(
        "SELECT token_hash, user_id, expires_at, created_at FROM auth_tokens WHERE user_id = ? ORDER BY created_at DESC");
    stmt->BindText(1, userId);
    while (stmt->Step()) {
        AuthToken t;
        t.token_hash = stmt->ColumnText(0);
        t.user_id = stmt->ColumnText(1);
        t.expires_at = stmt->ColumnInt64(2);
        t.created_at = stmt->ColumnInt64(3);
        result.push_back(std::move(t));
    }
    return result;
}

bool AuthStore::CleanExpiredTokens() {
    if (!m_store) return false;
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto stmt = m_store->Prepare("DELETE FROM auth_tokens WHERE expires_at < ?");
    stmt->BindInt64(1, now);
    return stmt->Exec();
}

int AuthStore::UserCount() {
    if (!m_store) return 0;
    auto stmt = m_store->Prepare("SELECT COUNT(*) FROM auth_users");
    if (!stmt->Step()) return 0;
    return static_cast<int>(stmt->ColumnInt64(0));
}

bool AuthStore::HasUsers() {
    return UserCount() > 0;
}

} // namespace animus::kernel
