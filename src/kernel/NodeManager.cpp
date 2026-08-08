#include "animus_kernel/NodeManager.h"
#include "animus_kernel/IDataStore.h"
#include "animus_kernel/SchemaHelpers.h"
#include "animus_kernel/AgentStore.h"
#include "animus_kernel/CryptoUtils.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
#include <sstream>
#include <iomanip>

namespace animus::kernel {

// SHA-256 hash for tokens (using OpenSSL for production security)
static std::string HashToken(const std::string& token) {
    return crypto::Sha256Hex(token);
}

static std::string GenerateRandomToken() {
    // 32 bytes → 64 hex chars, prefixed
    return "an_" + crypto::RandomHex(24);
}

static std::string GenerateSigningKey() {
    // 32 bytes → 64 hex chars
    return crypto::RandomHex(32);
}

static int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

NodeManager::NodeManager(IDataStore* store)
    : m_store(store) {
    EnsureSchema();
    LoadTokens();
}

void NodeManager::EnsureSchema() {
    if (!m_store) return;

    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS node_tokens (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            token_hash TEXT NOT NULL UNIQUE,
            signing_key_hash TEXT NOT NULL DEFAULT '',
            description TEXT NOT NULL DEFAULT '',
            created_at_unix_ms INTEGER NOT NULL,
            revoked INTEGER NOT NULL DEFAULT 0
        );
    )");

    // Migration: add signing_key_hash column if missing
    if (schema::ColumnExists(m_store, "node_tokens", "signing_key_hash") == false) {
        m_store->Exec("ALTER TABLE node_tokens ADD COLUMN signing_key_hash TEXT NOT NULL DEFAULT ''");
    }

    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_node_tokens_hash "
        "ON node_tokens(token_hash)");
}

void NodeManager::LoadTokens() {
    if (!m_store) return;
    auto stmt = m_store->Prepare(
        "SELECT id, token_hash, signing_key_hash, description, created_at_unix_ms, revoked "
        "FROM node_tokens WHERE revoked = 0");
    if (!stmt) return;
    while (stmt->Step()) {
        NodeToken t;
        t.id = stmt->ColumnInt64(0);
        t.token_hash = stmt->ColumnText(1);
        t.signing_key_hash = stmt->ColumnText(2);
        t.description = stmt->ColumnText(3);
        t.created_at_unix_ms = stmt->ColumnInt64(4);
        t.revoked = stmt->ColumnInt64(5) != 0;
        m_tokensByHash[t.token_hash] = t;
    }
    std::cerr << "[node-manager] Loaded " << m_tokensByHash.size() << " active tokens\n";
}

NodeManager::GeneratedCredentials NodeManager::GenerateCredentials(const std::string& description) {
    GeneratedCredentials result;
    if (!m_store) return result;

    std::string token = GenerateRandomToken();
    std::string hash = HashToken(token);
    std::string signingKey = GenerateSigningKey();
    std::string signingKeyHash = crypto::Sha256Hex(signingKey);

    auto stmt = m_store->Prepare(
        "INSERT INTO node_tokens (token_hash, signing_key_hash, description, created_at_unix_ms, revoked) "
        "VALUES (?,?,?,?,0)");
    if (!stmt) return result;
    stmt->BindText(1, hash);
    stmt->BindText(2, signingKeyHash);
    stmt->BindText(3, description);
    stmt->BindInt64(4, NowMs());
    stmt->BindInt64(4, NowMs());
    stmt->ExecDML();

    NodeToken t;
    t.id = m_store->LastInsertRowId();
    t.token = token;
    t.token_hash = hash;
    t.signing_key = signingKey;
    t.signing_key_hash = signingKeyHash;
    t.description = description;
    t.created_at_unix_ms = NowMs();
    m_tokensByHash[hash] = t;

    // Keep plaintext signing key in memory for HMAC signing (not persisted)
    m_signingKeysByHash[hash] = signingKey;

    result.token = token;
    result.signingKey = signingKey;
    return result;
}

int64_t NodeManager::ValidateToken(const std::string& token) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string hash = HashToken(token);
    auto it = m_tokensByHash.find(hash);
    if (it == m_tokensByHash.end()) return -1;
    if (it->second.revoked) return -1;
    return it->second.id;
}

std::string NodeManager::GetSigningKeyForToken(const std::string& token) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string hash = HashToken(token);
    auto it = m_signingKeysByHash.find(hash);
    if (it == m_signingKeysByHash.end()) return "";
    return it->second;
}

std::vector<NodeToken> NodeManager::ListTokens() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<NodeToken> result;
    for (const auto& [hash, t] : m_tokensByHash) {
        NodeToken copy = t;
        copy.token.clear(); // never expose plaintext
        result.push_back(copy);
    }
    return result;
}

bool NodeManager::RevokeToken(int64_t tokenId) {
    if (!m_store) return false;
    auto stmt = m_store->Prepare(
        "UPDATE node_tokens SET revoked = 1 WHERE id = ?");
    if (!stmt) return false;
    stmt->BindInt64(1, tokenId);
    stmt->ExecDML();

    // Remove from cache
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_tokensByHash.begin(); it != m_tokensByHash.end(); ++it) {
        if (it->second.id == tokenId) {
            m_tokensByHash.erase(it);
            break;
        }
    }
    return true;
}

void NodeManager::RegisterNode(const std::string& name, const NodeInfo& info, NodeExecuteFn execFn) {
    std::lock_guard<std::mutex> lock(m_mutex);
    ConnectedNode node;
    node.info = info;
    node.info.name = name;
    node.info.connected = true;
    node.info.connected_at_unix_ms = NowMs();
    node.info.last_seen_unix_ms = node.info.connected_at_unix_ms;
    node.execFn = std::move(execFn);
    m_nodes[name] = std::move(node);
    std::cerr << "[node-manager] Node registered: " << name
              << " (" << info.tools.size() << " tools)\n";
}

void NodeManager::RemoveNode(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_nodes.erase(name);
    std::cerr << "[node-manager] Node removed: " << name << "\n";
}

std::vector<NodeInfo> NodeManager::ListNodes() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<NodeInfo> result;
    for (const auto& [name, node] : m_nodes) {
        result.push_back(node.info);
    }
    return result;
}

std::optional<NodeInfo> NodeManager::GetNode(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_nodes.find(name);
    if (it == m_nodes.end()) return std::nullopt;
    return it->second.info;
}

ToolResult NodeManager::ExecuteOnNode(const std::string& nodeName, const ToolCall& call) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_nodes.find(nodeName);
    if (it == m_nodes.end() || !it->second.execFn) {
        ToolResult result;
        result.call_id = call.id;
        result.success = false;
        result.error = "Node not connected: " + nodeName;
        return result;
    }
    it->second.info.last_seen_unix_ms = NowMs();
    return it->second.execFn(call);
}

bool NodeManager::IsNodeAllowedForAgent(const std::string& nodeName, const std::string& agentId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_nodes.count(nodeName) == 0) return false;

    // If no AgentStore wired, allow all (legacy behavior)
    if (!m_agentStore) return true;

    auto agentOpt = m_agentStore->GetById(agentId);
    if (!agentOpt) return true;  // Unknown agent — default allow

    // Empty allowed_nodes means all nodes allowed
    if (agentOpt->allowed_nodes.empty()) return true;

    // Check if nodeName is in the allowed list
    for (const auto& allowed : agentOpt->allowed_nodes) {
        if (allowed == nodeName) return true;
    }
    return false;
}

std::vector<NodeInfo> NodeManager::ListNodesForAgent(const std::string& agentId) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    // If no AgentStore wired, return all nodes (legacy behavior)
    if (!m_agentStore) {
        std::vector<NodeInfo> result;
        for (const auto& [name, node] : m_nodes) {
            result.push_back(node.info);
        }
        return result;
    }

    auto agentOpt = m_agentStore->GetById(agentId);
    if (!agentOpt || agentOpt->allowed_nodes.empty()) {
        // Unknown agent or empty list — return all
        std::vector<NodeInfo> result;
        for (const auto& [name, node] : m_nodes) {
            result.push_back(node.info);
        }
        return result;
    }

    // Filter to allowed nodes only
    std::vector<NodeInfo> result;
    for (const auto& [name, node] : m_nodes) {
        for (const auto& allowed : agentOpt->allowed_nodes) {
            if (allowed == name) {
                result.push_back(node.info);
                break;
            }
        }
    }
    return result;
}

} // namespace animus::kernel
