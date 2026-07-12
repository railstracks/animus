#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace animus::kernel {

class IDataStore;

// ============================================================================
// Data types
// ============================================================================

struct GallivantingThread {
    int64_t id{0};
    std::string agent_id{"default"};
    std::string name;
    std::string description;
    std::string sdt_tags_json;       // {"autonomy":0.8,"competence":0.3,...}
    std::string prompt_template;     // custom prompt override (empty = use default)
    bool enabled{true};
    int64_t created_at_unix_ms{0};
    int64_t updated_at_unix_ms{0};
};

struct GallivantingSession {
    int64_t id{0};
    int64_t thread_id{0};
    std::string agent_id{"default"};
    int64_t started_at_unix_ms{0};
    int64_t duration_ms{0};
    std::string summary;
    std::string outcome;
    std::string sdt_scores_json;     // per-session actual scores
    std::string tools_used_json;     // ["diary","web_search",...]
    int64_t created_at_unix_ms{0};
};

struct SdtBalance {
    double autonomy{0.0};
    double competence{0.0};
    double relatedness{0.0};
    double personal_development{0.0};
    double relaxation{0.0};
    double meaning{0.0};
    int64_t session_count{0};
};

// ============================================================================
// GallivantingStore — SQLite-backed gallivanting thread + session storage
// ============================================================================

class GallivantingStore {
public:
    explicit GallivantingStore(IDataStore* dataStore);
    ~GallivantingStore();

    GallivantingStore(const GallivantingStore&) = delete;
    GallivantingStore& operator=(const GallivantingStore&) = delete;

    // --- Threads ---
    std::vector<GallivantingThread> ListThreads(const std::string& agent_id);
    std::optional<GallivantingThread> GetThread(int64_t id);
    GallivantingThread CreateThread(const GallivantingThread& thread);
    bool UpdateThread(const GallivantingThread& thread);
    bool DeleteThread(int64_t id);

    // --- Sessions ---
    std::vector<GallivantingSession> ListSessionsForThread(int64_t thread_id, int64_t limit = 20);
    std::vector<GallivantingSession> ListRecentSessions(const std::string& agent_id, int64_t limit = 5);
    GallivantingSession CreateSession(const GallivantingSession& session);
    bool DeleteSessionsForThread(int64_t thread_id);

    // --- Balance ---
    SdtBalance GetSdtBalance(const std::string& agent_id, int64_t since_unix_ms);

private:
    void EnsureSchema();

    IDataStore* m_store;
};

} // namespace animus::kernel
