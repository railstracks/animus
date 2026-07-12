#include "animus_kernel/MemorySearch.h"

#include "animus_kernel/IDataStore.h"
#include "animus_kernel/MemoryFileStore.h"
#include "animus_kernel/MemoryStore.h"
#include "animus_kernel/SchemaHelpers.h"
#include "animus_kernel/OntologyStore.h"
#include "animus_kernel/SessionManager.h"
#include "animus_kernel/AgentStore.h"
#include "animus_kernel/admin/DiaryManager.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace animus::kernel::memory {
namespace {

double ScoreFromBm25(double bm25Value) {
    const double normalized = bm25Value < 0.0 ? 0.0 : bm25Value;
    return 1.0 / (1.0 + normalized);
}

double ScoreFromTsRank(double tsRank) {
    // ts_rank produces values typically 0.01–10+.
    // Normalize to 0–1 range similar to ScoreFromBm25.
    return 1.0 / (1.0 + tsRank);
}

} // namespace

MemorySearch::MemorySearch(
        MemoryStore* memoryStore,
        ontology::OntologyStore* ontologyStore,
        MemoryFileStore* fileStore,
        DiaryStore* diaryStore)
    : m_memoryStore(memoryStore)
    , m_ontologyStore(ontologyStore)
    , m_fileStore(fileStore)
    , m_diaryStore(diaryStore) {
    EnsureSchema();
}

MemorySearch::MemorySearch(
        MemoryStore* memoryStore,
        ontology::OntologyStore* ontologyStore,
        MemoryFileStore* fileStore,
        DiaryStore* diaryStore,
        SessionManager* sessions)
    : m_memoryStore(memoryStore)
    , m_ontologyStore(ontologyStore)
    , m_fileStore(fileStore)
    , m_diaryStore(diaryStore)
    , m_sessions(sessions) {
    EnsureSchema();
}

MemorySearch::~MemorySearch() = default;

void MemorySearch::SetSessionManager(SessionManager* sessions) {
    m_sessions = sessions;
}

void MemorySearch::EnsureSchema() {
    if (!m_memoryStore) return;
    auto* store = m_memoryStore->DataStore();
    if (!store) return;

    if (store->Dialect() == DataStoreDialect::SQLite) {
        // Observations FTS.
        store->Exec(R"(
            CREATE VIRTUAL TABLE IF NOT EXISTS observations_fts
            USING fts5(text, content='observations', content_rowid='id');
        )");
        store->Exec(R"(
            CREATE TRIGGER IF NOT EXISTS trg_observations_ai AFTER INSERT ON observations BEGIN
                INSERT INTO observations_fts(rowid, text) VALUES (new.id, new.text);
            END;
        )");
        store->Exec(R"(
            CREATE TRIGGER IF NOT EXISTS trg_observations_ad AFTER DELETE ON observations BEGIN
                INSERT INTO observations_fts(observations_fts, rowid, text)
                VALUES('delete', old.id, old.text);
            END;
        )");
        store->Exec(R"(
            CREATE TRIGGER IF NOT EXISTS trg_observations_au AFTER UPDATE OF text ON observations BEGIN
                INSERT INTO observations_fts(observations_fts, rowid, text)
                VALUES('delete', old.id, old.text);
                INSERT INTO observations_fts(rowid, text) VALUES (new.id, new.text);
            END;
        )");
    } else {
        // PostgreSQL: add tsvector column + GIN index for observations.
        if (!schema::ColumnExists(store, "observations", "search_vector")) {
            store->Exec("ALTER TABLE observations ADD COLUMN search_vector tsvector");
            // Backfill existing rows.
            store->Exec("UPDATE observations SET search_vector = to_tsvector('english', COALESCE(text, ''))");
        }
        store->Exec("CREATE INDEX IF NOT EXISTS idx_observations_search ON observations USING gin(search_vector)");
        // Trigger to keep tsvector in sync.
        store->Exec(R"(
            CREATE OR REPLACE FUNCTION trg_observations_tsvector() RETURNS trigger AS $$
            BEGIN
                NEW.search_vector := to_tsvector('english', COALESCE(NEW.text, ''));
                RETURN NEW;
            END;
            $$ LANGUAGE plpgsql
        )");
        store->Exec(
            "DROP TRIGGER IF EXISTS trg_observations_tsvector ON observations");
        store->Exec(
            "CREATE TRIGGER trg_observations_tsvector "
            "BEFORE INSERT OR UPDATE OF text ON observations "
            "FOR EACH ROW EXECUTE FUNCTION trg_observations_tsvector()");
    }

    // Diary FTS.
    EnsureDiaryFtsSchema();

    // Memory files FTS.
    EnsureMemoryFilesFtsSchema();

    // Ontology docs + FTS index (rebuilt periodically).
    schema::CreateTable(store, R"(
        CREATE TABLE IF NOT EXISTS ontology_search_docs (
            doc_id INTEGER PRIMARY KEY AUTOINCREMENT,
            entity_id INTEGER NOT NULL REFERENCES ontology_entities(id) ON DELETE CASCADE,
            text TEXT NOT NULL DEFAULT ''
        );
    )");
    store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_ontology_search_docs_entity "
        "ON ontology_search_docs(entity_id)");

    if (store->Dialect() == DataStoreDialect::SQLite) {
        store->Exec(R"(
            CREATE VIRTUAL TABLE IF NOT EXISTS ontology_search_fts
            USING fts5(text, content='ontology_search_docs', content_rowid='doc_id');
        )");

        // Backfill for observations FTS.
        store->Exec(
            "INSERT INTO observations_fts(rowid, text) "
            "SELECT id, text FROM observations "
            "WHERE id NOT IN (SELECT rowid FROM observations_fts)");

        // Verify FTS sync — force rebuild if counts diverge
        auto obsStats = VerifyFtsSync("observations");
        if (obsStats.source_count != obsStats.fts_count) {
            std::cerr << "[memory-search] Observations FTS out of sync: "
                      << obsStats.fts_count << " indexed vs "
                      << obsStats.source_count << " source, rebuilding" << std::endl;
            store->Exec("DELETE FROM observations_fts");
            store->Exec(
                "INSERT INTO observations_fts(rowid, text) "
                "SELECT id, text FROM observations");
        }
    } else {
        // PostgreSQL: add tsvector column + GIN index for ontology_search_docs.
        if (!schema::ColumnExists(store, "ontology_search_docs", "search_vector")) {
            store->Exec("ALTER TABLE ontology_search_docs ADD COLUMN search_vector tsvector");
            store->Exec("UPDATE ontology_search_docs SET search_vector = to_tsvector('english', COALESCE(text, ''))");
        }
        store->Exec("CREATE INDEX IF NOT EXISTS idx_ontology_search_docs_search ON ontology_search_docs USING gin(search_vector)");

        // Trigger to keep tsvector in sync.
        store->Exec(R"(
            CREATE OR REPLACE FUNCTION trg_ontology_docs_tsvector() RETURNS trigger AS $$
            BEGIN
                NEW.search_vector := to_tsvector('english', COALESCE(NEW.text, ''));
                RETURN NEW;
            END;
            $$ LANGUAGE plpgsql
        )");
        store->Exec(
            "DROP TRIGGER IF EXISTS trg_ontology_docs_tsvector ON ontology_search_docs");
        store->Exec(
            "CREATE TRIGGER trg_ontology_docs_tsvector "
            "BEFORE INSERT OR UPDATE OF text ON ontology_search_docs "
            "FOR EACH ROW EXECUTE FUNCTION trg_ontology_docs_tsvector()");
    }

    // Verify PostgreSQL backfill for observations.
    if (store->Dialect() == DataStoreDialect::PostgreSQL) {
        auto emptyObs = store->Prepare(
            "SELECT COUNT(*) FROM observations WHERE search_vector IS NULL");
        if (emptyObs && emptyObs->Step()) {
            int64_t count = emptyObs->ColumnInt64(0);
            if (count > 0) {
                std::cerr << "[memory-search] Backfilling " << count
                          << " observations with search vectors" << std::endl;
                store->Exec("UPDATE observations SET search_vector = to_tsvector('english', COALESCE(text, '')) WHERE search_vector IS NULL");
            }
        }
    }
}

// ============================================================================
// Diary FTS schema — extracted so RebuildDiaryIndex can reuse it.
// ============================================================================

void MemorySearch::EnsureDiaryFtsSchema() {
    if (!m_memoryStore) return;
    auto* store = m_memoryStore->DataStore();
    if (!store) return;

    if (store->Dialect() == DataStoreDialect::SQLite) {
        store->Exec(R"(
            CREATE VIRTUAL TABLE IF NOT EXISTS diary_entries_fts
            USING fts5(content, content='diary_entries', content_rowid='id');
        )");
        store->Exec(R"(
            CREATE TRIGGER IF NOT EXISTS trg_diary_entries_ai AFTER INSERT ON diary_entries BEGIN
                INSERT INTO diary_entries_fts(rowid, content) VALUES (new.id, new.content);
            END;
        )");
        store->Exec(R"(
            CREATE TRIGGER IF NOT EXISTS trg_diary_entries_ad AFTER DELETE ON diary_entries BEGIN
                INSERT INTO diary_entries_fts(diary_entries_fts, rowid, content)
                VALUES('delete', old.id, old.content);
            END;
        )");
        store->Exec(R"(
            CREATE TRIGGER IF NOT EXISTS trg_diary_entries_au AFTER UPDATE OF content ON diary_entries BEGIN
                INSERT INTO diary_entries_fts(diary_entries_fts, rowid, content)
                VALUES('delete', old.id, old.content);
                INSERT INTO diary_entries_fts(rowid, content) VALUES (new.id, new.content);
            END;
        )");

        // Backfill any rows that exist but aren't indexed yet.
        store->Exec(
            "INSERT INTO diary_entries_fts(rowid, content) "
            "SELECT id, content FROM diary_entries "
            "WHERE id NOT IN (SELECT rowid FROM diary_entries_fts)");
    } else {
        // PostgreSQL: add tsvector column + GIN index for diary_entries.
        if (!schema::ColumnExists(store, "diary_entries", "search_vector")) {
            store->Exec("ALTER TABLE diary_entries ADD COLUMN search_vector tsvector");
            store->Exec("UPDATE diary_entries SET search_vector = to_tsvector('english', COALESCE(content, ''))");
        }
        store->Exec("CREATE INDEX IF NOT EXISTS idx_diary_entries_search ON diary_entries USING gin(search_vector)");

        // Trigger to keep tsvector in sync.
        store->Exec(R"(
            CREATE OR REPLACE FUNCTION trg_diary_tsvector() RETURNS trigger AS $$
            BEGIN
                NEW.search_vector := to_tsvector('english', COALESCE(NEW.content, ''));
                RETURN NEW;
            END;
            $$ LANGUAGE plpgsql
        )");
        store->Exec(
            "DROP TRIGGER IF EXISTS trg_diary_tsvector ON diary_entries");
        store->Exec(
            "CREATE TRIGGER trg_diary_tsvector "
            "BEFORE INSERT OR UPDATE OF content ON diary_entries "
            "FOR EACH ROW EXECUTE FUNCTION trg_diary_tsvector()");

        // Backfill any rows that exist but aren't indexed yet.
        auto emptyDiary = store->Prepare(
            "SELECT COUNT(*) FROM diary_entries WHERE search_vector IS NULL");
        if (emptyDiary && emptyDiary->Step()) {
            int64_t count = emptyDiary->ColumnInt64(0);
            if (count > 0) {
                std::cerr << "[memory-search] Backfilling " << count
                          << " diary entries with search vectors" << std::endl;
                store->Exec("UPDATE diary_entries SET search_vector = to_tsvector('english', COALESCE(content, '')) WHERE search_vector IS NULL");
            }
        }
    }
}

// ============================================================================
// Memory files FTS (PostgreSQL tsvector for memory_files table)
// ============================================================================

void MemorySearch::EnsureMemoryFilesFtsSchema() {
    if (!m_memoryStore) return;
    auto* store = m_memoryStore->DataStore();
    if (!store) return;

    if (store->Dialect() == DataStoreDialect::PostgreSQL) {
        // Add tsvector column for memory_files content search
        if (!schema::ColumnExists(store, "memory_files", "search_vector")) {
            store->Exec("ALTER TABLE memory_files ADD COLUMN search_vector tsvector");
            store->Exec("UPDATE memory_files SET search_vector = to_tsvector('english', COALESCE(content, ''))");
        }
        store->Exec("CREATE INDEX IF NOT EXISTS idx_memory_files_search ON memory_files USING gin(search_vector)");

        // Trigger to keep tsvector in sync
        store->Exec(R"(
            CREATE OR REPLACE FUNCTION trg_memory_files_tsvector() RETURNS trigger AS $$
            BEGIN
                NEW.search_vector := to_tsvector('english', COALESCE(NEW.content, ''));
                RETURN NEW;
            END;
            $$ LANGUAGE plpgsql
        )");
        store->Exec("DROP TRIGGER IF EXISTS trg_memory_files_tsvector ON memory_files");
        store->Exec(
            "CREATE TRIGGER trg_memory_files_tsvector "
            "BEFORE INSERT OR UPDATE OF content ON memory_files "
            "FOR EACH ROW EXECUTE FUNCTION trg_memory_files_tsvector()");

        // Backfill any rows missing search_vector
        auto empty = store->Prepare(
            "SELECT COUNT(*) FROM memory_files WHERE search_vector IS NULL");
        if (empty && empty->Step()) {
            int64_t count = empty->ColumnInt64(0);
            if (count > 0) {
                std::cerr << "[memory-search] Backfilling " << count
                          << " memory files with search vectors\n";
                store->Exec("UPDATE memory_files SET search_vector = to_tsvector('english', COALESCE(content, '')) WHERE search_vector IS NULL");
            }
        }
    }
}

bool MemorySearch::RebuildDiaryIndex() {
    if (!m_memoryStore) return false;
    auto* store = m_memoryStore->DataStore();
    if (!store) return false;

    if (store->Dialect() == DataStoreDialect::SQLite) {
        // Drop existing triggers (ignore errors — they may not exist).
        store->Exec("DROP TRIGGER IF EXISTS trg_diary_entries_ai");
        store->Exec("DROP TRIGGER IF EXISTS trg_diary_entries_ad");
        store->Exec("DROP TRIGGER IF EXISTS trg_diary_entries_au");

        // Drop and recreate the FTS table.
        store->Exec("DROP TABLE IF EXISTS diary_entries_fts");

        // Recreate FTS + triggers + backfill.
        EnsureDiaryFtsSchema();
    } else {
        // PostgreSQL: rebuild tsvector column.
        store->Exec("UPDATE diary_entries SET search_vector = to_tsvector('english', COALESCE(content, ''))");
    }

    // Verify.
    auto stats = VerifyFtsSync("diary");
    std::cerr << "[memory-search] Diary index rebuilt: "
              << stats.fts_count << "/" << stats.source_count
              << " entries indexed.\n";

    return stats.source_count == stats.fts_count;
}

// ============================================================================
// Verify FTS sync — compare source table row count with FTS/tsvector count.
// ============================================================================

MemorySearch::FtsSyncStats MemorySearch::VerifyFtsSync(const std::string& domain) {
    FtsSyncStats stats{};
    if (!m_memoryStore) return stats;
    auto* store = m_memoryStore->DataStore();
    if (!store) return stats;

    if (store->Dialect() == DataStoreDialect::SQLite) {
        if (domain == "diary") {
            auto srcStmt = store->Prepare("SELECT COUNT(*) FROM diary_entries");
            if (srcStmt && srcStmt->Step()) {
                stats.source_count = srcStmt->ColumnInt64(0);
            }
            auto ftsStmt = store->Prepare("SELECT COUNT(*) FROM diary_entries_fts");
            if (ftsStmt && ftsStmt->Step()) {
                stats.fts_count = ftsStmt->ColumnInt64(0);
            }
        } else if (domain == "observations") {
            auto srcStmt = store->Prepare("SELECT COUNT(*) FROM observations");
            if (srcStmt && srcStmt->Step()) {
                stats.source_count = srcStmt->ColumnInt64(0);
            }
            auto ftsStmt = store->Prepare("SELECT COUNT(*) FROM observations_fts");
            if (ftsStmt && ftsStmt->Step()) {
                stats.fts_count = ftsStmt->ColumnInt64(0);
            }
        }
    } else {
        // PostgreSQL: count rows with populated search_vector.
        if (domain == "diary") {
            auto srcStmt = store->Prepare("SELECT COUNT(*) FROM diary_entries");
            if (srcStmt && srcStmt->Step()) {
                stats.source_count = srcStmt->ColumnInt64(0);
            }
            auto ftsStmt = store->Prepare("SELECT COUNT(*) FROM diary_entries WHERE search_vector IS NOT NULL");
            if (ftsStmt && ftsStmt->Step()) {
                stats.fts_count = ftsStmt->ColumnInt64(0);
            }
        } else if (domain == "observations") {
            auto srcStmt = store->Prepare("SELECT COUNT(*) FROM observations");
            if (srcStmt && srcStmt->Step()) {
                stats.source_count = srcStmt->ColumnInt64(0);
            }
            auto ftsStmt = store->Prepare("SELECT COUNT(*) FROM observations WHERE search_vector IS NOT NULL");
            if (ftsStmt && ftsStmt->Step()) {
                stats.fts_count = ftsStmt->ColumnInt64(0);
            }
        }
    }

    return stats;
}

void MemorySearch::RefreshOntologyDocs() {
    if (!m_memoryStore || !m_ontologyStore) return;
    auto* store = m_memoryStore->DataStore();
    if (!store) return;

    store->Exec("DELETE FROM ontology_search_docs");
    const auto entities = m_ontologyStore->ListEntities();
    for (const auto& entity : entities) {
        // Entity path/name doc.
        {
            auto stmt = store->Prepare(
                "INSERT INTO ontology_search_docs(entity_id, text) VALUES (?, ?)");
            if (stmt) {
                stmt->BindInt64(1, entity.id);
                stmt->BindText(2, entity.full_path + " " + entity.name);
                stmt->Step();
            }
        }

        // Property docs.
        const auto properties = m_ontologyStore->ListProperties(entity.id);
        for (const auto& property : properties) {
            auto stmt = store->Prepare(
                "INSERT INTO ontology_search_docs(entity_id, text) VALUES (?, ?)");
            if (!stmt) continue;
            stmt->BindInt64(1, entity.id);
            stmt->BindText(2, property.key + " " + property.value);
            stmt->Step();
        }
    }

    if (store->Dialect() == DataStoreDialect::SQLite) {
        store->Exec("INSERT INTO ontology_search_fts(ontology_search_fts) VALUES('rebuild')");
    } else {
        // PostgreSQL: refresh the tsvector column.
        store->Exec("UPDATE ontology_search_docs SET search_vector = to_tsvector('english', COALESCE(text, ''))");
    }
}

std::vector<MemorySearchResult> MemorySearch::Search(
        const std::string& query,
        const std::string& agent_id,
        MemorySearchDomain domains,
        int64_t limit) {
    std::vector<MemorySearchResult> results;
    if (query.empty() || limit <= 0) return results;
    if (!m_memoryStore || !m_ontologyStore || !m_fileStore || !m_diaryStore) return results;
    auto* store = m_memoryStore->DataStore();
    if (!store) return results;

    const std::string& agentKey = agent_id;
    int64_t numericAgentId = 0;
    if (m_agentStore && !agent_id.empty()) {
        auto agent = m_agentStore->GetById(agent_id);
        if (agent) numericAgentId = agent->numeric_id;
    }
    if (numericAgentId == 0) {
        try { numericAgentId = std::stoll(agent_id); } catch (...) {}
    }
    const int64_t perDomainLimit = std::max<int64_t>(limit, 20);
    const bool isPostgres = (store->Dialect() == DataStoreDialect::PostgreSQL);

    if (domains.observations) {
        if (isPostgres) {
            // PostgreSQL: tsvector + ts_rank search.
            auto stmt = store->Prepare(
                "SELECT o.id, o.layer_id, o.text, ts_rank(o.search_vector, plainto_tsquery(?)), "
                "o.memory_state, o.superseded_by, o.created_at_unix_ms, o.updated_at_unix_ms "
                "FROM observations o "
                "JOIN memory_layers ml ON ml.id = o.layer_id "
                "WHERE o.search_vector @@ plainto_tsquery(?) AND ml.agent_id=? "
                "ORDER BY ts_rank(o.search_vector, plainto_tsquery(?)) DESC LIMIT ?");
            if (stmt) {
                stmt->BindText(1, query);
                stmt->BindText(2, agentKey);
                stmt->BindText(3, query);
                stmt->BindInt64(4, perDomainLimit);
                while (stmt->Step()) {
                    MemorySearchResult r;
                    r.domain = "observation";
                    r.id = stmt->ColumnInt64(0);
                    r.layer_id = stmt->ColumnInt64(1);
                    r.text = stmt->ColumnText(2);
                    r.relevance = ScoreFromTsRank(stmt->ColumnDouble(3));
                    int64_t memState = stmt->ColumnInt64(4);
                    int64_t supersededBy = stmt->ColumnInt64(5);
                    if (supersededBy != 0) r.status = "superseded";
                    else if (memState == static_cast<int64_t>(memory::MemoryState::Deprecated)) r.status = "retired";
                    else r.status = "active";
                    r.created_at_unix_ms = stmt->ColumnInt64(6);
                    r.updated_at_unix_ms = stmt->ColumnInt64(7);
                    results.push_back(std::move(r));
                }
            }
        } else {
            // SQLite: FTS5 + bm25.
            auto stmt = store->Prepare(
                "SELECT o.id, o.layer_id, o.text, bm25(observations_fts), "
                "o.memory_state, o.superseded_by, o.created_at_unix_ms, o.updated_at_unix_ms "
                "FROM observations_fts "
                "JOIN observations o ON o.id = observations_fts.rowid "
                "JOIN memory_layers ml ON ml.id = o.layer_id "
                "WHERE observations_fts MATCH ? AND ml.agent_id=? "
                "ORDER BY bm25(observations_fts) LIMIT ?");
            if (stmt) {
                stmt->BindText(1, query);
                stmt->BindText(2, agentKey);
                stmt->BindInt64(3, perDomainLimit);
                while (stmt->Step()) {
                    MemorySearchResult r;
                    r.domain = "observation";
                    r.id = stmt->ColumnInt64(0);
                    r.layer_id = stmt->ColumnInt64(1);
                    r.text = stmt->ColumnText(2);
                    r.relevance = ScoreFromBm25(stmt->ColumnDouble(3));
                    int64_t memState = stmt->ColumnInt64(4);
                    int64_t supersededBy = stmt->ColumnInt64(5);
                    if (supersededBy != 0) r.status = "superseded";
                    else if (memState == static_cast<int64_t>(memory::MemoryState::Deprecated)) r.status = "retired";
                    else r.status = "active";
                    r.created_at_unix_ms = stmt->ColumnInt64(6);
                    r.updated_at_unix_ms = stmt->ColumnInt64(7);
                    results.push_back(std::move(r));
                }
            }
        }
    }

    if (domains.ontology) {
        RefreshOntologyDocs();

        if (isPostgres) {
            // PostgreSQL: tsvector search on ontology_search_docs.
            auto stmt = store->Prepare(
                "SELECT d.entity_id, d.text, ts_rank(d.search_vector, plainto_tsquery(?)) "
                "FROM ontology_search_docs d "
                "WHERE d.search_vector @@ plainto_tsquery(?) "
                "ORDER BY ts_rank(d.search_vector, plainto_tsquery(?)) DESC LIMIT ?");
            if (stmt) {
                stmt->BindText(1, query);
                stmt->BindText(2, query);
                stmt->BindInt64(3, perDomainLimit * 3);

                std::unordered_map<int64_t, MemorySearchResult> bestByEntity;
                while (stmt->Step()) {
                    const int64_t entityId = stmt->ColumnInt64(0);
                    const std::string text = stmt->ColumnText(1);
                    const double relevance = ScoreFromTsRank(stmt->ColumnDouble(2));
                    auto entity = m_ontologyStore->GetEntity(entityId);
                    if (!entity) continue;

                    auto it = bestByEntity.find(entityId);
                    if (it == bestByEntity.end() || relevance > it->second.relevance) {
                        MemorySearchResult r;
                        r.domain = "ontology";
                        r.id = entityId;
                        r.entity_path = entity->full_path;
                        r.text = text;
                        r.relevance = relevance;
                        bestByEntity[entityId] = std::move(r);
                    }
                }

                for (auto& [_, value] : bestByEntity) {
                    results.push_back(std::move(value));
                }
            }
        } else {
            // SQLite: FTS5 + bm25.
            auto stmt = store->Prepare(
                "SELECT d.entity_id, d.text, bm25(ontology_search_fts) "
                "FROM ontology_search_fts "
                "JOIN ontology_search_docs d ON d.doc_id = ontology_search_fts.rowid "
                "WHERE ontology_search_fts MATCH ? "
                "ORDER BY bm25(ontology_search_fts) LIMIT ?");
            if (stmt) {
                stmt->BindText(1, query);
                stmt->BindInt64(2, perDomainLimit * 3);

                std::unordered_map<int64_t, MemorySearchResult> bestByEntity;
                while (stmt->Step()) {
                    const int64_t entityId = stmt->ColumnInt64(0);
                    const std::string text = stmt->ColumnText(1);
                    const double relevance = ScoreFromBm25(stmt->ColumnDouble(2));
                    auto entity = m_ontologyStore->GetEntity(entityId);
                    if (!entity) continue;

                    auto it = bestByEntity.find(entityId);
                    if (it == bestByEntity.end() || relevance > it->second.relevance) {
                        MemorySearchResult r;
                        r.domain = "ontology";
                        r.id = entityId;
                        r.entity_path = entity->full_path;
                        r.text = text;
                        r.relevance = relevance;
                        bestByEntity[entityId] = std::move(r);
                    }
                }

                for (auto& [_, value] : bestByEntity) {
                    results.push_back(std::move(value));
                }
            }
        }
    }

    if (domains.raw_files) {
        if (isPostgres) {
            // PostgreSQL: tsvector search on memory_files.
            auto stmt = store->Prepare(
                "SELECT f.id, f.source_path, f.content, ts_rank(f.search_vector, plainto_tsquery(?)) "
                "FROM memory_files f "
                "WHERE f.search_vector @@ plainto_tsquery(?) "
                "AND (f.agent_id = 0 OR f.agent_id = ?) "
                "AND f.superseded = 0 "
                "ORDER BY ts_rank(f.search_vector, plainto_tsquery(?)) DESC LIMIT ?");
            if (stmt) {
                stmt->BindText(1, query);
                stmt->BindText(2, query);
                stmt->BindInt64(3, numericAgentId);
                stmt->BindText(4, query);
                stmt->BindInt64(5, perDomainLimit);
                while (stmt->Step()) {
                    MemorySearchResult r;
                    r.domain = "memory_file";
                    r.id = stmt->ColumnInt64(0);
                    r.source_path = stmt->ColumnText(1);
                    r.text = stmt->ColumnText(2).substr(0, 280);
                    r.relevance = ScoreFromTsRank(stmt->ColumnDouble(3));
                    results.push_back(std::move(r));
                }
            }
        } else {
            // SQLite: FTS5 + bm25.
            auto stmt = store->Prepare(
                "SELECT f.id, f.source_path, f.content, bm25(memory_files_fts) "
                "FROM memory_files_fts "
                "JOIN memory_files f ON f.id = memory_files_fts.rowid "
                "WHERE memory_files_fts MATCH ? "
                "AND (f.agent_id = 0 OR f.agent_id = ?) "
                "AND f.superseded = 0 "
                "ORDER BY bm25(memory_files_fts) LIMIT ?");
            if (stmt) {
                stmt->BindText(1, query);
                stmt->BindInt64(2, numericAgentId);
                stmt->BindInt64(3, perDomainLimit);
                while (stmt->Step()) {
                    MemorySearchResult r;
                    r.domain = "memory_file";
                    r.id = stmt->ColumnInt64(0);
                    r.source_path = stmt->ColumnText(1);
                    r.text = stmt->ColumnText(2).substr(0, 280);
                    r.relevance = ScoreFromBm25(stmt->ColumnDouble(3));
                    results.push_back(std::move(r));
                }
            }
        }
    }

    if (domains.diary) {
        if (isPostgres) {
            // PostgreSQL: tsvector search on diary_entries.
            auto stmt = store->Prepare(
                "SELECT d.id, d.layer, d.content, ts_rank(d.search_vector, plainto_tsquery(?)) "
                "FROM diary_entries d "
                "WHERE d.search_vector @@ plainto_tsquery(?) AND d.agent_id=? "
                "ORDER BY ts_rank(d.search_vector, plainto_tsquery(?)) DESC LIMIT ?");
            if (stmt) {
                stmt->BindText(1, query);
                stmt->BindText(2, agentKey);
                stmt->BindText(3, query);
                stmt->BindInt64(4, perDomainLimit);
                while (stmt->Step()) {
                    MemorySearchResult r;
                    r.domain = "diary";
                    r.id = stmt->ColumnInt64(0);
                    r.layer = stmt->ColumnText(1);
                    r.text = stmt->ColumnText(2);
                    r.relevance = ScoreFromTsRank(stmt->ColumnDouble(3));
                    results.push_back(std::move(r));
                }
            }
        } else {
            // SQLite: FTS5 + bm25.
            auto stmt = store->Prepare(
                "SELECT d.id, d.layer, d.content, bm25(diary_entries_fts) "
                "FROM diary_entries_fts "
                "JOIN diary_entries d ON d.id = diary_entries_fts.rowid "
                "WHERE diary_entries_fts MATCH ? AND d.agent_id=? "
                "ORDER BY bm25(diary_entries_fts) LIMIT ?");
            if (stmt) {
                stmt->BindText(1, query);
                stmt->BindText(2, agentKey);
                stmt->BindInt64(3, perDomainLimit);
                while (stmt->Step()) {
                    MemorySearchResult r;
                    r.domain = "diary";
                    r.id = stmt->ColumnInt64(0);
                    r.layer = stmt->ColumnText(1);
                    r.text = stmt->ColumnText(2);
                    r.relevance = ScoreFromBm25(stmt->ColumnDouble(3));
                    results.push_back(std::move(r));
                }
            }
        }
    }

    if (domains.sessions && m_sessions) {
        // In-memory session search — iterate sessions for this agent,
        // match query terms against turn content.
        const auto allSessions = m_sessions->ListSessions();
        std::string lowerQuery = query;
        std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);

        for (const auto& session : allSessions) {
            if (!session) continue;
            if (session->AgentId() != agentKey) continue;

            const std::string sessionKey = session->Key().ToString();
            int64_t matchCount = 0;
            std::string bestText;

            for (const auto& turn : session->Turns()) {
                if (turn.content.empty()) continue;
                std::string lowerContent = turn.content;
                std::transform(lowerContent.begin(), lowerContent.end(), lowerContent.begin(), ::tolower);
                if (lowerContent.find(lowerQuery) != std::string::npos) {
                    matchCount++;
                    if (bestText.empty() || turn.content.size() < bestText.size()) {
                        bestText = turn.content.substr(0, 280);
                    }
                }
            }

            if (matchCount > 0) {
                MemorySearchResult r;
                r.domain = "session";
                r.id = static_cast<int64_t>(session->Id());
                r.session_key = sessionKey;
                r.text = bestText;
                r.relevance = std::min(1.0, static_cast<double>(matchCount) / 10.0);
                results.push_back(std::move(r));
            }
        }
    }

    std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) {
        return a.relevance > b.relevance;
    });
    if (results.size() > static_cast<std::size_t>(limit)) {
        results.resize(static_cast<std::size_t>(limit));
    }
    return results;
}

} // namespace animus::kernel::memory