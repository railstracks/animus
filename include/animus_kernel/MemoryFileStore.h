#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace animus::kernel {

class IDataStore;

namespace memory {

enum class MemoryFileType : int32_t {
    ExpandedMemory = 0,
    SessionLog = 1,
    DailyNote = 2,
    BootstrapFile = 3,
    Journal = 4,
    ExternalDoc = 5
};

enum class MemoryFileStatus : int32_t {
    Unprocessed = 0,
    Processed = 1
};

struct MemoryFile {
    int64_t id{0};
    std::string source_path;
    MemoryFileType file_type{MemoryFileType::ExternalDoc};
    std::string content;
    bool content_mutable{true};
    int64_t agent_id{0};
    bool superseded{false};
    int64_t created_at_unix_ms{0};
    int64_t imported_at_unix_ms{0};
    MemoryFileStatus status{MemoryFileStatus::Unprocessed};
};

struct MemoryFileChunk {
    int64_t id{0};
    int64_t file_id{0};
    std::string source_path;
    std::string header_title;
    int32_t chunk_index{0};
    std::string content;
    std::string content_hash;
    int32_t start_line{0};
    int32_t end_line{0};
    std::vector<float> embedding; // Empty if not yet computed
    int32_t embedding_dim{0};
};

class MemoryFileStore {
public:
    explicit MemoryFileStore(IDataStore* dataStore);
    ~MemoryFileStore();

    MemoryFileStore(const MemoryFileStore&) = delete;
    MemoryFileStore& operator=(const MemoryFileStore&) = delete;

    MemoryFile CreateFile(const MemoryFile& file);
    std::optional<MemoryFile> GetFile(int64_t id);
    std::vector<MemoryFile> ListFiles(
        std::optional<MemoryFileType> type = std::nullopt,
        int64_t limit = 100,
        int64_t offset = 0);
    std::vector<MemoryFile> ListUnprocessedFiles(int64_t agentId, int64_t limit = 50);
    bool UpdateFile(const MemoryFile& file); // content update allowed when content_mutable=true
    bool MarkProcessed(int64_t id);
    bool DeleteFile(int64_t id); // exceptional administrative hard delete

    int64_t CountByType(MemoryFileType type);
    int64_t CountUnprocessed(int64_t agentId);

    // --- Chunk management (for pre-computed embeddings) ---

    // Replace all chunks for a file. Called when file content changes.
    void ReplaceChunks(int64_t fileId, const std::vector<MemoryFileChunk>& chunks);

    // Get all chunks for a file
    std::vector<MemoryFileChunk> GetChunks(int64_t fileId);

    // Get all chunks for an agent (across all files)
    std::vector<MemoryFileChunk> GetChunksForAgent(int64_t agentId);

    // Update embedding for a chunk
    bool UpdateChunkEmbedding(int64_t chunkId, const std::vector<float>& embedding);

    // Count chunks missing embeddings for an agent
    int64_t CountChunksNeedingEmbeddings(int64_t agentId);

    static std::string FileTypeToString(MemoryFileType type);
    static std::optional<MemoryFileType> FileTypeFromString(const std::string& type);

private:
    void EnsureSchema();
    IDataStore* m_store;
};

} // namespace memory
} // namespace animus::kernel
