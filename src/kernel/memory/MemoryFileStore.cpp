
#include "animus_kernel/SchemaHelpers.h"
#include "animus_kernel/MemoryFileStore.h"

#include "animus_kernel/IDataStore.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <utility>

namespace animus::kernel::memory {
namespace {

int64_t NowUnixMs() {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

int64_t FileTypeToInt(MemoryFileType type) {
    return static_cast<int64_t>(type);
}

MemoryFileType IntToFileType(int64_t type) {
    switch (type) {
        case 0: return MemoryFileType::ExpandedMemory;
        case 1: return MemoryFileType::SessionLog;
        case 2: return MemoryFileType::DailyNote;
        case 3: return MemoryFileType::BootstrapFile;
        case 4: return MemoryFileType::Journal;
        case 5: return MemoryFileType::ExternalDoc;
        default: return MemoryFileType::ExternalDoc;
    }
}

bool DidWriteRows(IDataStore* store) {
    if (!store) return false;
    return store->Changes() > 0;
}

MemoryFile RowToMemoryFile(IStatement* stmt) {
    MemoryFile file;
    file.id = stmt->ColumnInt64(0);
    file.source_path = stmt->ColumnText(1);
    file.file_type = IntToFileType(stmt->ColumnInt64(2));
    file.content = stmt->ColumnText(3);
    file.content_mutable = stmt->ColumnInt64(4) != 0;
    file.agent_id = stmt->ColumnInt64(5);
    file.superseded = stmt->ColumnInt64(6) != 0;
    file.created_at_unix_ms = stmt->ColumnInt64(7);
    file.imported_at_unix_ms = stmt->ColumnInt64(8);
    file.status = static_cast<MemoryFileStatus>(stmt->ColumnInt64(9));
    return file;
}

} // namespace

MemoryFileStore::MemoryFileStore(IDataStore* dataStore) : m_store(dataStore) {
    EnsureSchema();
}

MemoryFileStore::~MemoryFileStore() = default;

void MemoryFileStore::EnsureSchema() {
    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS memory_files (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            source_path TEXT NOT NULL,
            file_type INTEGER NOT NULL,
            content TEXT NOT NULL DEFAULT '',
            content_mutable INTEGER NOT NULL DEFAULT 1,
            agent_id INTEGER NOT NULL DEFAULT 0,
            superseded INTEGER NOT NULL DEFAULT 0,
            created_at_unix_ms INTEGER NOT NULL,
           imported_at_unix_ms INTEGER NOT NULL
       );
   )");

    // Backward-compatible migration if superseded is missing.
    // Backward-compatible migrations (work for both SQLite and PostgreSQL)
    {
        if (!schema::ColumnExists(m_store, "memory_files", "superseded")) {
            m_store->Exec(
                "ALTER TABLE memory_files "
                "ADD COLUMN superseded INTEGER NOT NULL DEFAULT 0");
        }
        if (!schema::ColumnExists(m_store, "memory_files", "content_mutable")) {
            m_store->Exec(
                "ALTER TABLE memory_files "
                "ADD COLUMN content_mutable INTEGER NOT NULL DEFAULT 1");
        }
        if (!schema::ColumnExists(m_store, "memory_files", "status")) {
            m_store->Exec(
                "ALTER TABLE memory_files "
                "ADD COLUMN status INTEGER NOT NULL DEFAULT 0");
        }
    }

    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_memory_files_type "
        "ON memory_files(file_type)");
    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_memory_files_agent "
        "ON memory_files(agent_id)");
    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_memory_files_imported "
        "ON memory_files(imported_at_unix_ms)");

    // Chunk table for pre-computed embeddings
    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS memory_file_chunks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            file_id INTEGER NOT NULL REFERENCES memory_files(id) ON DELETE CASCADE,
            source_path TEXT NOT NULL,
            header_title TEXT NOT NULL DEFAULT '',
            chunk_index INTEGER NOT NULL DEFAULT 0,
            content TEXT NOT NULL,
            content_hash TEXT NOT NULL DEFAULT '',
            start_line INTEGER NOT NULL DEFAULT 0,
            end_line INTEGER NOT NULL DEFAULT 0,
            embedding BLOB,
            embedding_dim INTEGER NOT NULL DEFAULT 0,
            created_at_unix_ms INTEGER NOT NULL DEFAULT 0
        );
    )");
    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_memory_file_chunks_file "
        "ON memory_file_chunks(file_id)");
    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_memory_file_chunks_hash "
        "ON memory_file_chunks(content_hash)");

    // FTS5 index for raw file search (SQLite only).
    // PostgreSQL uses tsvector column + GIN index instead.
    if (m_store->Dialect() == DataStoreDialect::SQLite) {
        m_store->Exec(R"(
            CREATE VIRTUAL TABLE IF NOT EXISTS memory_files_fts
            USING fts5(content, content='memory_files', content_rowid='id');
        )");
        m_store->Exec(R"(
            CREATE TRIGGER IF NOT EXISTS trg_memory_files_ai AFTER INSERT ON memory_files BEGIN
                INSERT INTO memory_files_fts(rowid, content) VALUES (new.id, new.content);
            END;
        )");
        m_store->Exec(R"(
            CREATE TRIGGER IF NOT EXISTS trg_memory_files_ad AFTER DELETE ON memory_files BEGIN
                INSERT INTO memory_files_fts(memory_files_fts, rowid, content)
                VALUES('delete', old.id, old.content);
            END;
        )");
        m_store->Exec(R"(
            CREATE TRIGGER IF NOT EXISTS trg_memory_files_au AFTER UPDATE OF content ON memory_files BEGIN
                INSERT INTO memory_files_fts(memory_files_fts, rowid, content)
                VALUES('delete', old.id, old.content);
                INSERT INTO memory_files_fts(rowid, content) VALUES (new.id, new.content);
            END;
        )");

        // Backfill existing rows into FTS table.
        m_store->Exec(
            "INSERT INTO memory_files_fts(rowid, content) "
            "SELECT id, content FROM memory_files "
            "WHERE id NOT IN (SELECT rowid FROM memory_files_fts)");
    } else {
        // PostgreSQL: add tsvector column + GIN index.
        if (!schema::ColumnExists(m_store, "memory_files", "search_vector")) {
            m_store->Exec("ALTER TABLE memory_files ADD COLUMN search_vector tsvector");
            m_store->Exec("UPDATE memory_files SET search_vector = to_tsvector('english', COALESCE(content, ''))");
        }
        m_store->Exec("CREATE INDEX IF NOT EXISTS idx_memory_files_search ON memory_files USING gin(search_vector)");

        // Trigger to keep tsvector in sync.
        m_store->Exec(R"(
            CREATE OR REPLACE FUNCTION trg_memory_files_tsvector() RETURNS trigger AS $$
            BEGIN
                NEW.search_vector := to_tsvector('english', COALESCE(NEW.content, ''));
                RETURN NEW;
            END;
            $$ LANGUAGE plpgsql
        )");
        m_store->Exec(
            "DROP TRIGGER IF EXISTS trg_memory_files_tsvector ON memory_files");
        m_store->Exec(
            "CREATE TRIGGER trg_memory_files_tsvector "
            "BEFORE INSERT OR UPDATE OF content ON memory_files "
            "FOR EACH ROW EXECUTE FUNCTION trg_memory_files_tsvector()");
    }
}

MemoryFile MemoryFileStore::CreateFile(const MemoryFile& file) {
    const int64_t now = NowUnixMs();
    const int64_t createdAt = file.created_at_unix_ms > 0 ? file.created_at_unix_ms : now;
    const int64_t importedAt = file.imported_at_unix_ms > 0 ? file.imported_at_unix_ms : now;

    int64_t newId = 0;
    {
        auto stmt = m_store->Prepare(
            "INSERT INTO memory_files "
            "(source_path, file_type, content, content_mutable, agent_id, superseded, created_at_unix_ms, imported_at_unix_ms, status) "
            "VALUES (?,?,?,?,?,?,?,?,?)");
        if (!stmt) return {};
        stmt->BindText(1, file.source_path);
        stmt->BindInt64(2, FileTypeToInt(file.file_type));
        stmt->BindText(3, file.content);
        stmt->BindInt(4, file.content_mutable ? 1 : 0);
        stmt->BindInt64(5, file.agent_id);
        stmt->BindInt(6, file.superseded ? 1 : 0);
        stmt->BindInt64(7, createdAt);
        stmt->BindInt64(8, importedAt);
        stmt->BindInt(9, static_cast<int64_t>(file.status));
        stmt->Step();
        if (!DidWriteRows(m_store)) {
            std::cerr << "[memory_files] create failed: " << m_store->ErrMsg() << std::endl;
            return {};
        }
        newId = m_store->LastInsertRowId();
        // stmt destroyed here — releases statement before GetFile reuses the connection
    }

    if (newId <= 0) {
        std::cerr << "[memory_files] LastInsertRowId returned " << newId
                  << " after successful insert" << std::endl;
        return {};
    }

    auto result = GetFile(newId);
    if (!result) {
        std::cerr << "[memory_files] GetFile(" << newId
                  << ") returned nullopt after insert" << std::endl;
        return {};
    }
    return *result;
}

std::optional<MemoryFile> MemoryFileStore::GetFile(int64_t id) {
    auto stmt = m_store->Prepare(
        "SELECT id, source_path, file_type, content, content_mutable, agent_id, superseded, "
        "created_at_unix_ms, imported_at_unix_ms, status "
        "FROM memory_files WHERE id=?");
    if (!stmt) return std::nullopt;
    stmt->BindInt64(1, id);
    if (!stmt->Step()) return std::nullopt;
    return RowToMemoryFile(stmt.get());
}

std::vector<MemoryFile> MemoryFileStore::ListFiles(
        std::optional<MemoryFileType> type, int64_t limit, int64_t offset) {
    std::vector<MemoryFile> result;
    if (limit <= 0) limit = 100;
    if (offset < 0) offset = 0;

    std::unique_ptr<IStatement> stmt;
    if (type.has_value()) {
        stmt = m_store->Prepare(
            "SELECT id, source_path, file_type, content, content_mutable, agent_id, superseded, "
            "created_at_unix_ms, imported_at_unix_ms, status "
            "FROM memory_files WHERE file_type=? "
            "ORDER BY imported_at_unix_ms DESC LIMIT ? OFFSET ?");
        if (!stmt) return result;
        stmt->BindInt64(1, FileTypeToInt(*type));
        stmt->BindInt64(2, limit);
        stmt->BindInt64(3, offset);
    } else {
        stmt = m_store->Prepare(
            "SELECT id, source_path, file_type, content, content_mutable, agent_id, superseded, "
            "created_at_unix_ms, imported_at_unix_ms, status "
            "FROM memory_files ORDER BY imported_at_unix_ms DESC LIMIT ? OFFSET ?");
        if (!stmt) return result;
        stmt->BindInt64(1, limit);
        stmt->BindInt64(2, offset);
    }

    while (stmt->Step()) {
        result.push_back(RowToMemoryFile(stmt.get()));
    }
    return result;
}

bool MemoryFileStore::UpdateFile(const MemoryFile& file) {
    const auto existing = GetFile(file.id);
    if (!existing.has_value()) {
        return false;
    }
    if (file.content != existing->content
        && !existing->content_mutable
        && !file.content_mutable) {
        return false;
    }

    const int64_t importedAt = file.imported_at_unix_ms > 0
        ? file.imported_at_unix_ms
        : NowUnixMs();
    auto stmt = m_store->Prepare(
        "UPDATE memory_files SET source_path=?, file_type=?, content=?, content_mutable=?, "
        "agent_id=?, superseded=?, created_at_unix_ms=?, imported_at_unix_ms=?, status=? WHERE id=?");
    if (!stmt) return false;
    stmt->BindText(1, file.source_path);
    stmt->BindInt64(2, FileTypeToInt(file.file_type));
    stmt->BindText(3, file.content);
    stmt->BindInt(4, file.content_mutable ? 1 : 0);
    stmt->BindInt64(5, file.agent_id);
    stmt->BindInt(6, file.superseded ? 1 : 0);
    stmt->BindInt64(7, file.created_at_unix_ms);
    stmt->BindInt64(8, importedAt);
    stmt->BindInt(9, static_cast<int32_t>(file.status));
    stmt->BindInt64(10, file.id);
    stmt->Step();
    return DidWriteRows(m_store);
}

bool MemoryFileStore::DeleteFile(int64_t id) {
    auto stmt = m_store->Prepare("DELETE FROM memory_files WHERE id=?");
    if (!stmt) return false;
    stmt->BindInt64(1, id);
    stmt->Step();
    return DidWriteRows(m_store);
}

int64_t MemoryFileStore::CountByType(MemoryFileType type) {
    auto stmt = m_store->Prepare("SELECT COUNT(*) FROM memory_files WHERE file_type=?");
    if (!stmt) return 0;
    stmt->BindInt64(1, FileTypeToInt(type));
    if (!stmt->Step()) return 0;
    return stmt->ColumnInt64(0);
}

std::vector<MemoryFile> MemoryFileStore::ListUnprocessedFiles(int64_t agentId, int64_t limit) {
    std::vector<MemoryFile> result;
    if (limit <= 0) limit = 50;

    auto stmt = m_store->Prepare(
        "SELECT id, source_path, file_type, content, content_mutable, agent_id, superseded, "
        "created_at_unix_ms, imported_at_unix_ms, status "
        "FROM memory_files WHERE status = 0 AND superseded = 0 "
        "AND (agent_id = ? OR agent_id = 0) "
        "ORDER BY created_at_unix_ms ASC LIMIT ?");
    if (!stmt) return result;
    stmt->BindInt64(1, agentId);
    stmt->BindInt64(2, limit);
    while (stmt->Step()) {
        result.push_back(RowToMemoryFile(stmt.get()));
    }
    return result;
}

bool MemoryFileStore::MarkProcessed(int64_t id) {
    auto stmt = m_store->Prepare(
        "UPDATE memory_files SET status = 1 WHERE id = ?");
    if (!stmt) return false;
    stmt->BindInt64(1, id);
    stmt->Step();
    return DidWriteRows(m_store);
}

int64_t MemoryFileStore::CountUnprocessed(int64_t agentId) {
    auto stmt = m_store->Prepare(
        "SELECT COUNT(*) FROM memory_files WHERE status = 0 AND superseded = 0 "
        "AND (agent_id = ? OR agent_id = 0)");
    if (!stmt) {
        std::cerr << "[memory-file-store] CountUnprocessed: Prepare failed: " << m_store->ErrMsg() << std::endl;
        return 0;
    }
    stmt->BindInt64(1, agentId);
    if (!stmt->Step()) {
        std::cerr << "[memory-file-store] CountUnprocessed: Step failed: " << m_store->ErrMsg() << std::endl;
        return 0;
    }
    int64_t count = stmt->ColumnInt64(0);
    std::cerr << "[memory-file-store] CountUnprocessed(" << agentId << ") = " << count << std::endl;
    return count;
}

// ============================================================================
// Chunk management
// ============================================================================

static std::string ComputeContentHash(const std::string& content) {
    // Simple FNV-1a hash for change detection
    uint64_t hash = 14695981039346656037ULL;
    for (char c : content) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        hash *= 1099511628211ULL;
    }
    return std::to_string(hash);
}

static std::vector<float> DeserializeEmbedding(const std::vector<uint8_t>& blob, int32_t dim) {
    std::vector<float> result;
    if (blob.empty() || dim <= 0) return result;
    result.resize(dim);
    // Copy bytes into floats
    size_t copySize = std::min(blob.size(), dim * sizeof(float));
    std::memcpy(result.data(), blob.data(), copySize);
    return result;
}

static std::vector<uint8_t> SerializeEmbedding(const std::vector<float>& embedding) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(embedding.data());
    size_t size = embedding.size() * sizeof(float);
    return std::vector<uint8_t>(bytes, bytes + size);
}

void MemoryFileStore::ReplaceChunks(int64_t fileId, const std::vector<MemoryFileChunk>& chunks) {
    // Delete existing chunks
    auto del = m_store->Prepare("DELETE FROM memory_file_chunks WHERE file_id=?");
    if (del) { del->BindInt64(1, fileId); del->Step(); }

    // Insert new chunks
    for (size_t i = 0; i < chunks.size(); ++i) {
        const auto& c = chunks[i];
        auto stmt = m_store->Prepare(
            "INSERT INTO memory_file_chunks "
            "(file_id, source_path, header_title, chunk_index, content, content_hash, "
            "start_line, end_line, embedding, embedding_dim, created_at_unix_ms) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?)");
        if (!stmt) continue;
        stmt->BindInt64(1, fileId);
        stmt->BindText(2, c.source_path);
        stmt->BindText(3, c.header_title);
        stmt->BindInt(4, static_cast<int32_t>(i));
        stmt->BindText(5, c.content);
        stmt->BindText(6, ComputeContentHash(c.content));
        stmt->BindInt(7, c.start_line);
        stmt->BindInt(8, c.end_line);
        // Embedding stored separately via UpdateChunkEmbedding
        stmt->BindNull(9); // embedding = NULL initially
        stmt->BindInt(10, 0);
        stmt->BindInt64(11, c.start_line > 0 ? c.start_line : 0);
        stmt->Step();
    }
}

static MemoryFileChunk RowToChunk(IStatement* stmt) {
    MemoryFileChunk c;
    c.id = stmt->ColumnInt64(0);
    c.file_id = stmt->ColumnInt64(1);
    c.source_path = stmt->ColumnText(2);
    c.header_title = stmt->ColumnText(3);
    c.chunk_index = static_cast<int32_t>(stmt->ColumnInt64(4));
    c.content = stmt->ColumnText(5);
    c.content_hash = stmt->ColumnText(6);
    c.start_line = static_cast<int32_t>(stmt->ColumnInt64(7));
    c.end_line = static_cast<int32_t>(stmt->ColumnInt64(8));
    // Column 9 = embedding (BLOB), 10 = embedding_dim
    int32_t dim = static_cast<int32_t>(stmt->ColumnInt64(10));
    c.embedding_dim = dim;
    if (dim > 0 && !stmt->IsColumnNull(9)) {
        auto blob = stmt->ColumnBlob(9);
        c.embedding = DeserializeEmbedding(blob, dim);
    }
    return c;
}

std::vector<MemoryFileChunk> MemoryFileStore::GetChunks(int64_t fileId) {
    std::vector<MemoryFileChunk> result;
    auto stmt = m_store->Prepare(
        "SELECT id, file_id, source_path, header_title, chunk_index, content, "
        "content_hash, start_line, end_line, embedding, embedding_dim "
        "FROM memory_file_chunks WHERE file_id=? ORDER BY chunk_index ASC");
    if (!stmt) return result;
    stmt->BindInt64(1, fileId);
    while (stmt->Step()) {
        result.push_back(RowToChunk(stmt.get()));
    }
    return result;
}

std::vector<MemoryFileChunk> MemoryFileStore::GetChunksForAgent(int64_t agentId) {
    std::vector<MemoryFileChunk> result;
    auto stmt = m_store->Prepare(
        "SELECT mc.id, mc.file_id, mc.source_path, mc.header_title, mc.chunk_index, "
        "mc.content, mc.content_hash, mc.start_line, mc.end_line, mc.embedding, mc.embedding_dim "
        "FROM memory_file_chunks mc "
        "JOIN memory_files mf ON mf.id = mc.file_id "
        "WHERE mf.agent_id = ? AND mf.superseded = 0 "
        "ORDER BY mc.file_id, mc.chunk_index ASC");
    if (!stmt) return result;
    stmt->BindInt64(1, agentId);
    while (stmt->Step()) {
        result.push_back(RowToChunk(stmt.get()));
    }
    return result;
}

bool MemoryFileStore::UpdateChunkEmbedding(int64_t chunkId, const std::vector<float>& embedding) {
    auto blob = SerializeEmbedding(embedding);
    auto stmt = m_store->Prepare(
        "UPDATE memory_file_chunks SET embedding=?, embedding_dim=? WHERE id=?");
    if (!stmt) return false;
    stmt->BindBlob(1, blob.data(), blob.size());
    stmt->BindInt(2, static_cast<int32_t>(embedding.size()));
    stmt->BindInt64(3, chunkId);
    stmt->Step();
    return true;
}

int64_t MemoryFileStore::CountChunksNeedingEmbeddings(int64_t agentId) {
    auto stmt = m_store->Prepare(
        "SELECT COUNT(*) FROM memory_file_chunks mc "
        "JOIN memory_files mf ON mf.id = mc.file_id "
        "WHERE mf.agent_id = ? AND mf.superseded = 0 "
        "AND (mc.embedding IS NULL OR mc.embedding_dim = 0)");
    if (!stmt) return 0;
    stmt->BindInt64(1, agentId);
    if (!stmt->Step()) return 0;
    return stmt->ColumnInt64(0);
}

std::string MemoryFileStore::FileTypeToString(MemoryFileType type) {
    switch (type) {
        case MemoryFileType::ExpandedMemory: return "expanded_memory";
        case MemoryFileType::SessionLog: return "session_log";
        case MemoryFileType::DailyNote: return "daily_note";
        case MemoryFileType::BootstrapFile: return "bootstrap_file";
        case MemoryFileType::Journal: return "journal";
        case MemoryFileType::ExternalDoc: return "external_doc";
        default: return "external_doc";
    }
}

std::optional<MemoryFileType> MemoryFileStore::FileTypeFromString(const std::string& type) {
    if (type == "expanded_memory") return MemoryFileType::ExpandedMemory;
    if (type == "session_log") return MemoryFileType::SessionLog;
    if (type == "daily_note") return MemoryFileType::DailyNote;
    if (type == "bootstrap_file") return MemoryFileType::BootstrapFile;
    if (type == "journal") return MemoryFileType::Journal;
    if (type == "external_doc") return MemoryFileType::ExternalDoc;
    return std::nullopt;
}

} // namespace animus::kernel::memory
