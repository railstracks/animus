#include "animus_kernel/NodeManager.h"
#include "animus_kernel/IDataStore.h"
#include "animus_kernel/SchemaHelpers.h"
#include "animus_kernel/AgentStore.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
#include <sstream>
#include <iomanip>

namespace animus::kernel {

// Simple SHA-256-like hash for tokens (uses FNV-1a for simplicity — 
// tokens are server-generated random strings, not user passwords)
static std::string HashToken(const std::string& token) {
    uint64_t hash1 = 14695981039346656037ULL;
    uint64_t hash2 = 1099511628211ULL;
    for (char c : token) {
        hash1 ^= static_cast<uint64_t>(c);
        hash1 *= 1099511628211ULL;
        hash2 ^= static_cast<uint64_t>(c);
        hash2 *= 14695981039346656037ULL;
    }
    std::ostringstream ss;
    ss << std::hex << hash1 << hash2;
    return ss.str();
}

static std::string GenerateRandomToken() {
    // 32 bytes → 64 hex chars
    std::random_device rd;
    std::uniform_int_distribution<int> dist(0, 255);
    std::ostringstream ss;
    ss << "an_"; // animus node token prefix
    for (int i = 0; i < 48; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << dist(rd);
    }
    return ss.str();
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
            description TEXT NOT NULL DEFAULT '',
            created_at_unix_ms INTEGER NOT NULL,
            revoked INTEGER NOT NULL DEFAULT 0
        );
    )");

    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_node_tokens_hash "
        "ON node_tokens(token_hash)");
}

void NodeManager::LoadTokens() {
    if (!m_store) return;
    auto stmt = m_store->Prepare(
        "SELECT id, token_hash, description, created_at_unix_ms, revoked "
        "FROM node_tokens WHERE revoked = 0");
    if (!stmt) return;
    while (stmt->Step()) {
        NodeToken t;
        t.id = stmt->ColumnInt64(0);
        t.token_hash = stmt->ColumnText(1);
        t.description = stmt->ColumnText(2);
        t.created_at_unix_ms = stmt->ColumnInt64(3);
        t.revoked = stmt->ColumnInt64(4) != 0;
        m_tokensByHash[t.token_hash] = t;
    }
    std::cerr << "[node-manager] Loaded " << m_tokensByHash.size() << " active tokens\n";
}

std::string NodeManager::GenerateToken(const std::string& description) {
    if (!m_store) return "";

    std::string token = GenerateRandomToken();
    std::string hash = HashToken(token);

    auto stmt = m_store->Prepare(
        "INSERT INTO node_tokens (token_hash, description, created_at_unix_ms, revoked) "
        "VALUES (?,?,?,0)");
    if (!stmt) return "";
    stmt->BindText(1, hash);
    stmt->BindText(2, description);
    stmt->BindInt64(3, NowMs());
    stmt->Step();

    NodeToken t;
    t.id = m_store->LastInsertRowId();
    t.token = token;
    t.token_hash = hash;
    t.description = description;
    t.created_at_unix_ms = NowMs();
    m_tokensByHash[hash] = t;

    return token;
}

int64_t NodeManager::ValidateToken(const std::string& token) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string hash = HashToken(token);
    auto it = m_tokensByHash.find(hash);
    if (it == m_tokensByHash.end()) return -1;
    if (it->second.revoked) return -1;
    return it->second.id;
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
    stmt->Step();

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
