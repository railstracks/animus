#include "animus_kernel/PromptLogStore.h"
#include "animus_kernel/IDataStore.h"
#include "animus_kernel/SchemaHelpers.h"

#include <iostream>
#include <json/json.h>
#include <sstream>

namespace animus::kernel {

// ============================================================================
// Log level helpers
// ============================================================================

PromptLogLevel ParsePromptLogLevel(const std::string& s) {
    if (s == "none") return PromptLogLevel::None;
    if (s == "full") return PromptLogLevel::Full;
    return PromptLogLevel::Default; // "default" or unrecognized
}

std::string PromptLogLevelToString(PromptLogLevel level) {
    switch (level) {
        case PromptLogLevel::None:    return "none";
        case PromptLogLevel::Default: return "default";
        case PromptLogLevel::Full:    return "full";
    }
    return "default";
}

// ============================================================================
// PromptLogStore
// ============================================================================

PromptLogStore::PromptLogStore(IDataStore* store)
    : m_store(store) {
    EnsureSchema();
}

void PromptLogStore::EnsureSchema() {
    if (!m_store) return;

    if (m_store->Dialect() == DataStoreDialect::PostgreSQL) {
        schema::CreateTable(m_store, R"(
            CREATE TABLE IF NOT EXISTS prompt_logs (
                id              SERIAL PRIMARY KEY,
                agent_id        TEXT NOT NULL DEFAULT '',
                session_id      BIGINT,
                provider        TEXT NOT NULL DEFAULT '',
                model           TEXT NOT NULL DEFAULT '',
                prompt_tokens   INTEGER NOT NULL DEFAULT 0,
                completion_tokens INTEGER NOT NULL DEFAULT 0,
                total_tokens    INTEGER NOT NULL DEFAULT 0,
                latency_ms      INTEGER NOT NULL DEFAULT 0,
                chain_step      INTEGER NOT NULL DEFAULT 0,
                created_at      TEXT NOT NULL DEFAULT (to_char(now() AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"')),
                prompt_content  TEXT,
                response_content TEXT,
                tool_calls      TEXT,
                tool_results    TEXT
            );
        )");

        schema::CreateTable(m_store, R"(
            CREATE INDEX IF NOT EXISTS idx_prompt_logs_agent_time
            ON prompt_logs(agent_id, created_at);
        )");

        schema::CreateTable(m_store, R"(
            CREATE INDEX IF NOT EXISTS idx_prompt_logs_session
            ON prompt_logs(session_id);
        )");
    } else {
        schema::CreateTable(m_store, R"(
            CREATE TABLE IF NOT EXISTS prompt_logs (
                id              INTEGER PRIMARY KEY AUTOINCREMENT,
                agent_id        TEXT NOT NULL DEFAULT '',
                session_id      INTEGER,
                provider        TEXT NOT NULL DEFAULT '',
                model           TEXT NOT NULL DEFAULT '',
                prompt_tokens   INTEGER NOT NULL DEFAULT 0,
                completion_tokens INTEGER NOT NULL DEFAULT 0,
                total_tokens    INTEGER NOT NULL DEFAULT 0,
                latency_ms      INTEGER NOT NULL DEFAULT 0,
                chain_step      INTEGER NOT NULL DEFAULT 0,
                created_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
                prompt_content  TEXT,
                response_content TEXT,
                tool_calls      TEXT,
                tool_results    TEXT
            );
        )");

        schema::CreateTable(m_store, R"(
            CREATE INDEX IF NOT EXISTS idx_prompt_logs_agent_time
            ON prompt_logs(agent_id, created_at);
        )");

        schema::CreateTable(m_store, R"(
            CREATE INDEX IF NOT EXISTS idx_prompt_logs_session
            ON prompt_logs(session_id);
        )");
    }
}

void PromptLogStore::Log(
    PromptLogLevel level,
    const std::string& agent_id,
    int64_t session_id,
    const std::string& provider,
    const std::string& model,
    int prompt_tokens,
    int completion_tokens,
    int latency_ms,
    int chain_step,
    const std::string& prompt_content,
    const std::string& response_content,
    const std::string& tool_calls_json,
    const std::string& tool_results_json) {

    if (level == PromptLogLevel::None) return;

    auto stmt = m_store->Prepare(R"(
        INSERT INTO prompt_logs
            (agent_id, session_id, provider, model,
             prompt_tokens, completion_tokens, total_tokens,
             latency_ms, chain_step,
             prompt_content, response_content, tool_calls, tool_results)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )");

    if (!stmt) {
        std::cerr << "[prompt-log] Failed to prepare INSERT: " << m_store->ErrMsg() << std::endl;
        return;
    }

    int idx = 1;
    stmt->BindText(idx++, agent_id);
    if (session_id > 0) {
        stmt->BindInt64(idx++, session_id);
    } else {
        stmt->BindNull(idx++);
    }
    stmt->BindText(idx++, provider);
    stmt->BindText(idx++, model);
    stmt->BindInt(idx++, prompt_tokens);
    stmt->BindInt(idx++, completion_tokens);
    stmt->BindInt(idx++, prompt_tokens + completion_tokens);
    stmt->BindInt(idx++, latency_ms);
    stmt->BindInt(idx++, chain_step);

    // Content fields: only at Full level
    if (level == PromptLogLevel::Full) {
        stmt->BindText(idx++, prompt_content);
        stmt->BindText(idx++, response_content);
        stmt->BindText(idx++, tool_calls_json.empty() ? "[]" : tool_calls_json);
        stmt->BindText(idx++, tool_results_json.empty() ? "[]" : tool_results_json);
    } else {
        stmt->BindNull(idx++);
        stmt->BindNull(idx++);
        stmt->BindNull(idx++);
        stmt->BindNull(idx++);
    }

    if (!stmt->Step()) {
        std::cerr << "[prompt-log] INSERT failed: " << m_store->ErrMsg() << std::endl;
    }
}

std::vector<PromptLogEntry> PromptLogStore::QueryByAgent(
    const std::string& agent_id,
    const std::string& from_iso,
    const std::string& to_iso,
    int limit) {

    std::vector<PromptLogEntry> results;

    std::string sql = R"(
        SELECT id, agent_id, session_id, provider, model,
               prompt_tokens, completion_tokens, total_tokens,
               latency_ms, chain_step, created_at,
               prompt_content, response_content, tool_calls, tool_results
        FROM prompt_logs
        WHERE agent_id = ?
    )";

    if (!from_iso.empty()) sql += " AND created_at >= ?";
    if (!to_iso.empty())   sql += " AND created_at <= ?";
    sql += " ORDER BY created_at DESC";
    if (limit > 0) sql += " LIMIT ?";

    auto stmt = m_store->Prepare(sql);
    if (!stmt) return results;

    int idx = 1;
    stmt->BindText(idx++, agent_id);
    if (!from_iso.empty()) stmt->BindText(idx++, from_iso);
    if (!to_iso.empty())   stmt->BindText(idx++, to_iso);
    if (limit > 0)         stmt->BindInt(idx++, limit);

    while (stmt->Step()) {
        PromptLogEntry e;
        e.id = stmt->ColumnInt64(0);
        e.agent_id = stmt->ColumnText(1);
        if (!stmt->IsColumnNull(2)) e.session_id = stmt->ColumnInt64(2);
        e.provider = stmt->ColumnText(3);
        e.model = stmt->ColumnText(4);
        e.prompt_tokens = stmt->ColumnInt64(5);
        e.completion_tokens = stmt->ColumnInt64(6);
        e.total_tokens = stmt->ColumnInt64(7);
        e.latency_ms = stmt->ColumnInt64(8);
        e.chain_step = stmt->ColumnInt64(9);
        e.created_at = stmt->ColumnText(10);
        if (!stmt->IsColumnNull(11)) e.prompt_content = stmt->ColumnText(11);
        if (!stmt->IsColumnNull(12)) e.response_content = stmt->ColumnText(12);
        if (!stmt->IsColumnNull(13)) e.tool_calls = stmt->ColumnText(13);
        if (!stmt->IsColumnNull(14)) e.tool_results = stmt->ColumnText(14);
        results.push_back(std::move(e));
    }

    return results;
}

PromptLogStore::TokenUsageSummary PromptLogStore::GetUsageSummary(
    const std::string& agent_id,
    const std::string& from_iso,
    const std::string& to_iso) {

    TokenUsageSummary summary;

    std::string sql = R"(
        SELECT COUNT(*),
               COALESCE(SUM(prompt_tokens), 0),
               COALESCE(SUM(completion_tokens), 0),
               COALESCE(SUM(total_tokens), 0),
               COALESCE(AVG(latency_ms), 0)
        FROM prompt_logs
        WHERE agent_id = ?
    )";

    if (!from_iso.empty()) sql += " AND created_at >= ?";
    if (!to_iso.empty())   sql += " AND created_at <= ?";

    auto stmt = m_store->Prepare(sql);
    if (!stmt) return summary;

    int idx = 1;
    stmt->BindText(idx++, agent_id);
    if (!from_iso.empty()) stmt->BindText(idx++, from_iso);
    if (!to_iso.empty())   stmt->BindText(idx++, to_iso);

    if (stmt->Step()) {
        summary.total_calls = stmt->ColumnInt64(0);
        summary.total_prompt_tokens = stmt->ColumnInt64(1);
        summary.total_completion_tokens = stmt->ColumnInt64(2);
        summary.total_tokens = stmt->ColumnInt64(3);
        summary.avg_latency_ms = stmt->ColumnDouble(4);
    }

    return summary;
}

} // namespace animus::kernel
