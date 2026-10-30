#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace animus::kernel {
class IDataStore;
}

namespace animus::migrate {

// Options carried from the tool's command line into the shared logic.
struct MigrationOptions {
    bool dryRun = false;
    bool verbose = false;
};

// Ordered migration manifest: referenced tables first (dependency order,
// matching the stores' EnsureSchema() creation order). Column lists are
// NOT hardcoded — they are derived at runtime from the source schema and
// verified against the target schema (issue #217: hardcoded lists
// silently skipped tables or dropped columns).
const std::vector<std::string>& MigrationTables();

struct PreflightReport {
    bool ok = true;
    std::vector<std::string> problems;  // fatal — abort before any data moves
    std::vector<std::string> notes;     // informational
};

// Verifies every manifest table's source columns against the target
// schema BEFORE any rows are copied, and scans the source for user
// tables the manifest does not know about (a stale manifest would
// silently skip whole tables — the table-level version of #217).
// Derived search-index tables (FTS5 and shadow tables) are excluded on
// the SQLite side; the PostgreSQL side rebuilds its text-search vectors
// in the backfill phase.
PreflightReport Preflight(animus::kernel::IDataStore* src,
                          animus::kernel::IDataStore* dst);

// Copies one table using the column list derived from the source schema
// at runtime. Returns rows migrated (>= 0), or -1 on error. Prepare
// failures are reported loudly — never skipped silently.
int64_t MigrateTable(animus::kernel::IDataStore* src,
                     animus::kernel::IDataStore* dst,
                     const std::string& table,
                     const MigrationOptions& options);

} // namespace animus::migrate
