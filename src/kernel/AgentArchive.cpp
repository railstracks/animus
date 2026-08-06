#include "animus_kernel/AgentArchive.h"
#include "animus_kernel/IDataStore.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <zlib.h>

namespace animus::kernel {

// ────────────────────────────────────────────────────────────────────────────
// Minimal tar writer (POSIX ustar)
// ────────────────────────────────────────────────────────────────────────────

namespace {

#pragma pack(push, 1)
struct TarHeader {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];   // "ustar\0"
    char version[2]; // "00"
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
};
static_assert(sizeof(TarHeader) == 512);
#pragma pack(pop)

void WriteOctal(char* buf, size_t len, uint64_t val) {
    std::memset(buf, '0', len);
    buf[len - 1] = '\0';
    for (size_t i = len - 2; i > 0 && val > 0; --i) {
        buf[i] = '0' + (val & 7);
        val >>= 3;
    }
}

void WriteTarHeader(std::vector<uint8_t>& out,
                    const std::string& name,
                    uint64_t size) {
    TarHeader hdr;
    std::memset(&hdr, 0, sizeof(hdr));

    // Name field (100 bytes max)
    size_t nameLen = std::min(name.size(), size_t(100));
    std::memcpy(hdr.name, name.c_str(), nameLen);

    WriteOctal(hdr.mode, sizeof(hdr.mode), 0644);
    WriteOctal(hdr.uid, sizeof(hdr.uid), 0);
    WriteOctal(hdr.gid, sizeof(hdr.gid), 0);
    WriteOctal(hdr.size, sizeof(hdr.size), size);
    WriteOctal(hdr.mtime, sizeof(hdr.mtime), std::time(nullptr));

    hdr.typeflag = '0'; // regular file
    std::memcpy(hdr.magic, "ustar", 5);
    hdr.magic[5] = '\0';
    hdr.version[0] = '0';
    hdr.version[1] = '0';
    std::memcpy(hdr.uname, "animus", 6);
    std::memcpy(hdr.gname, "animus", 6);

    // Checksum: fill with spaces, sum all bytes, write octal
    std::memset(hdr.chksum, ' ', sizeof(hdr.chksum));
    uint32_t chksum = 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&hdr);
    for (size_t i = 0; i < sizeof(hdr); ++i) {
        chksum += p[i];
    }
    WriteOctal(hdr.chksum, sizeof(hdr.chksum), chksum);
    hdr.chksum[sizeof(hdr.chksum) - 1] = ' ';

    out.insert(out.end(), reinterpret_cast<uint8_t*>(&hdr),
               reinterpret_cast<uint8_t*>(&hdr) + sizeof(hdr));
}

void WriteTarData(std::vector<uint8_t>& out,
                  const std::string& data) {
    size_t len = data.size();
    out.insert(out.end(), data.begin(), data.end());

    // Pad to 512-byte boundary
    size_t padding = (512 - (len % 512)) % 512;
    if (padding > 0) {
        out.insert(out.end(), padding, 0);
    }
}

void WriteTarEnd(std::vector<uint8_t>& out) {
    // Two 512-byte blocks of zeros
    out.insert(out.end(), 1024, 0);
}

// Gzip a buffer using zlib
bool GzipBuffer(const std::vector<uint8_t>& input,
                std::vector<uint8_t>& output) {
    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));

    // 16 + MAX_WBITS for gzip format
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                     16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return false;
    }

    zs.next_in = const_cast<Bytef*>(input.data());
    zs.avail_in = static_cast<uInt>(input.size());

    size_t bufSize = std::max<size_t>(input.size() / 2, 4096);
    std::vector<uint8_t> buf(bufSize);

    do {
        if (zs.avail_out == 0) {
            zs.next_out = buf.data();
            zs.avail_out = static_cast<uInt>(bufSize);
        }
        int ret = deflate(&zs, Z_FINISH);
        if (ret == Z_STREAM_ERROR || ret == Z_BUF_ERROR) {
            if (zs.avail_out == 0) {
                output.insert(output.end(), buf.data(), buf.data() + bufSize);
                zs.next_out = buf.data();
                zs.avail_out = static_cast<uInt>(bufSize);
                continue;
            }
            deflateEnd(&zs);
            return false;
        }
        size_t written = bufSize - zs.avail_out;
        output.insert(output.end(), buf.data(), buf.data() + written);
    } while (zs.avail_out == 0);

    deflateEnd(&zs);
    return true;
}

// Convert a statement row to a JSON object.
// Uses positional column access with explicit column lists per table.
Json::Value RowToJson(IStatement* stmt, const std::vector<std::string>& columns) {
    Json::Value obj(Json::objectValue);
    for (size_t i = 0; i < columns.size(); ++i) {
        if (stmt->IsColumnNull(static_cast<int>(i))) {
            obj[columns[i]] = Json::nullValue;
            continue;
        }
        ColumnType ct = stmt->GetColumnType(static_cast<int>(i));
        switch (ct) {
            case ColumnType::Integer:
                obj[columns[i]] = static_cast<Json::Int64>(stmt->ColumnInt64(static_cast<int>(i)));
                break;
            case ColumnType::Float:
                obj[columns[i]] = stmt->ColumnDouble(static_cast<int>(i));
                break;
            case ColumnType::Text:
                obj[columns[i]] = stmt->ColumnText(static_cast<int>(i));
                break;
            case ColumnType::Null:
                obj[columns[i]] = Json::nullValue;
                break;
            default: {
                // Unknown — treat as blob, base64 encode
                auto blob = stmt->ColumnBlob(static_cast<int>(i));
                std::string b64;
                static const char alphabet[] =
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                int val = 0, valb = -6;
                for (uint8_t c : blob) {
                    val = (val << 8) + c;
                    valb += 8;
                    while (valb >= 0) {
                        b64.push_back(alphabet[(val >> valb) & 0x3F]);
                        valb -= 6;
                    }
                }
                if (valb > -6) {
                    b64.push_back(alphabet[((val << 8) >> (valb + 8)) & 0x3F]);
                }
                while (b64.size() % 4) b64.push_back('=');
                obj[columns[i]] = "_base64:" + b64;
                break;
            }
        }
    }
    return obj;
}

// Dump a table to JSONL, filtering by agent_id column.
std::string DumpTable(IDataStore* store,
                      const std::string& table,
                      const std::vector<std::string>& columns,
                      const std::string& agentId,
                      const std::string& agentColumn = "agent_id",
                      const std::string& extraWhere = "") {
    std::ostringstream sql;
    sql << "SELECT ";
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) sql << ", ";
        sql << columns[i];
    }
    sql << " FROM " << table;
    sql << " WHERE " << agentColumn << " = ?";
    if (!extraWhere.empty()) {
        sql << " AND (" << extraWhere << ")";
    }

    auto stmt = store->Prepare(sql.str());
    if (!stmt) return "";

    stmt->BindText(1, agentId);

    std::ostringstream jsonl;
    int64_t exportId = 0;
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";

    while (stmt->Step()) {
        Json::Value row = RowToJson(stmt.get(), columns);
        row["_export_id"] = static_cast<Json::Int64>(++exportId);
        jsonl << Json::writeString(wb, row) << "\n";
    }

    return jsonl.str();
}

// Dump a table without agent_id filtering (agent record itself, or tables
// joined through other means).
std::string DumpTableNoAgent(IDataStore* store,
                              const std::string& table,
                              const std::vector<std::string>& columns,
                              const std::string& where = "") {
    std::ostringstream sql;
    sql << "SELECT ";
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) sql << ", ";
        sql << columns[i];
    }
    sql << " FROM " << table;
    if (!where.empty()) {
        sql << " WHERE " << where;
    }

    auto stmt = store->Prepare(sql.str());
    if (!stmt) return "";

    std::ostringstream jsonl;
    int64_t exportId = 0;
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";

    while (stmt->Step()) {
        Json::Value row = RowToJson(stmt.get(), columns);
        row["_export_id"] = static_cast<Json::Int64>(++exportId);
        jsonl << Json::writeString(wb, row) << "\n";
    }

    return jsonl.str();
}

} // anonymous namespace

// ────────────────────────────────────────────────────────────────────────────
// AgentArchiveWriter implementation
// ────────────────────────────────────────────────────────────────────────────

AgentArchiveWriter::AgentArchiveWriter(IDataStore* store)
    : m_store(store) {}

std::string AgentArchiveWriter::Write(const std::string& agentId,
                                       const AgentArchiveComponentFlags& flags,
                                       const std::string& outputPath) {
    if (!m_store) return "no data store";

    // Collect all files for the tarball
    std::vector<std::pair<std::string, std::string>> files;

    // ── manifest.json ──
    files.push_back({"manifest.json", BuildManifest(agentId, flags)});

    // ── agent.json (single record) ──
    {
        auto stmt = m_store->Prepare(
            "SELECT agent_id, name, description, identity, avatar, "
            "default_provider, default_model, default_vision_model, "
            "intake_interval, intake_prompt, context_window, temperature, "
            "reasoning_enabled, reasoning_effort, max_chain_steps, "
            "max_tool_calls_per_chain, timeout_seconds, episodic_token_budget, "
            "semantic_token_budget, perspectives_token_budget, "
            "consolidation_tool_budget, enabled_tools, tool_configs, "
            "diary_secret, pad_context "
            "FROM agents WHERE agent_id = ?");
        if (!stmt) return "failed to query agent";
        stmt->BindText(1, agentId);
        if (!stmt->Step()) return "agent not found: " + agentId;

        static const std::vector<std::string> agentCols = {
            "agent_id", "name", "description", "identity", "avatar",
            "default_provider", "default_model", "default_vision_model",
            "intake_interval", "intake_prompt", "context_window", "temperature",
            "reasoning_enabled", "reasoning_effort", "max_chain_steps",
            "max_tool_calls_per_chain", "timeout_seconds", "episodic_token_budget",
            "semantic_token_budget", "perspectives_token_budget",
            "consolidation_tool_budget", "enabled_tools", "tool_configs",
            "diary_secret", "pad_context"
        };

        Json::Value agentObj = RowToJson(stmt.get(), agentCols);
        Json::StreamWriterBuilder wb;
        wb["indentation"] = "  ";
        files.push_back({"agent.json", Json::writeString(wb, agentObj)});
    }

    // ── memory/ ──
    {
        auto layers = DumpTable(m_store, "memory_layers",
            {"id", "agent_id", "name", "horizon", "sort_order",
             "intake_interval", "intake_prompt", "consolidation_prompt",
             "consolidation_interval", "sdt_value_threshold",
             "last_consolidation_unix_ms", "created_at_unix_ms"},
            agentId);
        files.push_back({"memory/layers.jsonl", layers});
    }
    {
        auto obs = DumpTable(m_store, "observations",
            {"id", "layer_id", "agent_id", "content", "source",
             "created_at_unix_ms", "updated_at_unix_ms",
             "intake_processed", "intake_processed_at_unix_ms",
             "is_compacted", "importance_score", "embedding"},
            agentId);
        files.push_back({"memory/observations.jsonl", obs});
    }
    {
        auto persp = DumpTable(m_store, "layer_perspectives",
            {"id", "layer_id", "retrospective", "retrospective_valence",
             "current_perspective", "current_valence",
             "future_perspective", "future_valence", "updated_at_unix_ms"},
            agentId, "layer_id" /* dummy — no agent_id column */);
        // layer_perspectives doesn't have agent_id, so we need a different approach
        // For now, dump all and let import filter. TODO: join through layer_id.
    }
    {
        auto mut = DumpTable(m_store, "memory_mutations",
            {"id", "mutation_type", "target_type", "target_id",
             "from_layer_id", "to_layer_id", "previous_state",
             "motivation", "unix_ms"},
            agentId, "unix_ms" /* dummy */);
        // memory_mutations also doesn't have agent_id — skip for now or join
    }

    // ── memory-files/ ──
    if (flags.memoryFiles) {
        auto mf = DumpTable(m_store, "memory_files",
            {"id", "source_path", "file_type", "content",
             "content_mutable", "agent_id", "superseded",
             "created_at_unix_ms", "imported_at_unix_ms"},
            agentId);
        files.push_back({"memory-files/files.jsonl", mf});

        // memory_file_chunks — join through file_id → memory_files.agent_id
        if (flags.embeddings) {
            auto stmt = m_store->Prepare(
                "SELECT c.id, c.file_id, c.source_path, c.header_title, "
                "c.chunk_index, c.content, c.content_hash, "
                "c.start_line, c.end_line, c.embedding, c.embedding_dim, "
                "c.created_at_unix_ms "
                "FROM memory_file_chunks c "
                "INNER JOIN memory_files f ON c.file_id = f.id "
                "WHERE f.agent_id = ?");
            if (stmt) {
                stmt->BindText(1, agentId);
                std::ostringstream jsonl;
                int64_t exportId = 0;
                Json::StreamWriterBuilder wb;
                wb["indentation"] = "";
                static const std::vector<std::string> cols = {
                    "id", "file_id", "source_path", "header_title",
                    "chunk_index", "content", "content_hash",
                    "start_line", "end_line", "embedding", "embedding_dim",
                    "created_at_unix_ms"
                };
                while (stmt->Step()) {
                    Json::Value row = RowToJson(stmt.get(), cols);
                    row["_export_id"] = static_cast<Json::Int64>(++exportId);
                    jsonl << Json::writeString(wb, row) << "\n";
                }
                files.push_back({"memory-files/chunks.jsonl", jsonl.str()});
            }
        }
    }

    // ── ontology/ ──
    if (flags.ontology) {
        files.push_back({"ontology/entities.jsonl",
            DumpTable(m_store, "ontology_entities",
                {"id", "parent_id", "root_category", "name", "full_path",
                 "sort_order", "agent_id", "created_at_unix_ms", "updated_at_unix_ms"},
                agentId)});

        // ontology_properties — join through entity_id
        {
            auto stmt = m_store->Prepare(
                "SELECT p.id, p.entity_id, p.key, p.value, p.value_type, "
                "p.memory_state, p.confidence, p.source, p.created_at_unix_ms, "
                "p.updated_at_unix_ms "
                "FROM ontology_properties p "
                "INNER JOIN ontology_entities e ON p.entity_id = e.id "
                "WHERE e.agent_id = ?");
            if (stmt) {
                stmt->BindText(1, agentId);
                std::ostringstream jsonl;
                int64_t exportId = 0;
                Json::StreamWriterBuilder wb;
                wb["indentation"] = "";
                static const std::vector<std::string> cols = {
                    "id", "entity_id", "key", "value", "value_type",
                    "memory_state", "confidence", "source",
                    "created_at_unix_ms", "updated_at_unix_ms"
                };
                while (stmt->Step()) {
                    Json::Value row = RowToJson(stmt.get(), cols);
                    row["_export_id"] = static_cast<Json::Int64>(++exportId);
                    jsonl << Json::writeString(wb, row) << "\n";
                }
                files.push_back({"ontology/properties.jsonl", jsonl.str()});
            }
        }
    }

    // ── gallivanting/ ──
    if (flags.gallivanting) {
        files.push_back({"gallivanting/threads.jsonl",
            DumpTable(m_store, "gallivanting_threads",
                {"id", "agent_id", "title", "description", "status",
                 "prompt_template", "sdt_tags", "created_at_unix_ms",
                 "updated_at_unix_ms"},
                agentId)});

        if (flags.gallivantingHistory) {
            files.push_back({"gallivanting/sessions.jsonl",
                DumpTable(m_store, "gallivanting_sessions",
                    {"id", "thread_id", "agent_id", "sdt_scores",
                     "summary", "artifacts", "created_at_unix_ms"},
                    agentId)});
        }
    }

    // ── diary/ ──
    if (flags.diary) {
        files.push_back({"diary/entries.jsonl",
            DumpTable(m_store, "diary_entries",
                {"id", "agent_id", "timestamp_unix_ms", "layer",
                 "content", "tags", "session_id"},
                agentId)});
    }

    // ── schedules ──
    if (flags.schedules) {
        files.push_back({"schedules.jsonl",
            DumpTable(m_store, "schedules",
                {"id", "agent_id", "tag", "message", "cron_expr",
                 "enabled", "next_fire_unix_ms", "last_fire_unix_ms",
                 "max_fires", "fire_count", "created_at_unix_ms"},
                agentId)});
    }

    // ── consolidation ──
    if (flags.schedules) {
        files.push_back({"consolidation/runs.jsonl",
            DumpTable(m_store, "consolidation_runs",
                {"id", "agent_id", "phase", "started_unix_ms",
                 "completed_unix_ms", "observations_created", "status"},
                agentId)});
        files.push_back({"consolidation/watermarks.jsonl",
            DumpTable(m_store, "consolidation_watermarks",
                {"id", "agent_id", "source", "last_processed_id",
                 "last_run_unix_ms"},
                agentId)});
    }

    // ── lua_scripts ──
    if (flags.luaScripts) {
        files.push_back({"lua-scripts.jsonl",
            DumpTable(m_store, "lua_scripts",
                {"id", "agent_id", "name", "source", "description",
                 "enabled", "created_at", "updated_at"},
                agentId)});
    }

    // ── sessions (optional) ──
    if (flags.sessions) {
        // Sessions don't have a direct agent_id column — they're scoped
        // via the connector field which maps through channel config.
        // For now, we dump sessions that belong to this agent via
        // the session_key pattern or through session_reports join.
        // TODO: resolve session → agent mapping properly
    }

    // ── prompt_logs (optional) ──
    if (flags.promptLogs) {
        files.push_back({"prompt-logs.jsonl",
            DumpTable(m_store, "prompt_logs",
                {"id", "agent_id", "session_key", "turn_id",
                 "system_prompt", "user_message", "response",
                 "model", "provider", "input_tokens", "output_tokens",
                 "created_at_unix_ms"},
                agentId)});
    }

    // ── Build tar ──
    std::vector<uint8_t> tarball;
    for (const auto& [name, content] : files) {
        WriteTarHeader(tarball, name, content.size());
        WriteTarData(tarball, content);
    }
    WriteTarEnd(tarball);

    // ── Gzip ──
    std::vector<uint8_t> compressed;
    if (!GzipBuffer(tarball, compressed)) {
        return "gzip compression failed";
    }

    // ── Write to file ──
    std::ofstream out(outputPath, std::ios::binary);
    if (!out) return "cannot open output file: " + outputPath;
    out.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
    out.close();

    if (!out) return "write failed: " + outputPath;

    return "";  // success
}

std::string AgentArchiveWriter::BuildManifest(const std::string& agentId,
                                               const AgentArchiveComponentFlags& flags) {
    Json::Value manifest(Json::objectValue);
    manifest["format_version"] = "1.0";
    manifest["format_name"] = "animus-agent-archive";

    // Timestamp
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    std::ostringstream ts;
    ts << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    manifest["exported_at"] = ts.str();
    manifest["source_system"] = "animus";

    // Agent identity
    Json::Value agentInfo(Json::objectValue);
    agentInfo["agent_id"] = agentId;
    manifest["agent"] = agentInfo;

    // Components
    Json::Value comps(Json::objectValue);
    comps["agent"] = flags.agent;
    comps["memory"] = flags.memory;
    comps["memory_files"] = flags.memoryFiles;
    comps["ontology"] = flags.ontology;
    comps["schedules"] = flags.schedules;
    comps["gallivanting"] = flags.gallivanting;
    comps["gallivanting_history"] = flags.gallivantingHistory;
    comps["diary"] = flags.diary;
    comps["sessions"] = flags.sessions;
    comps["reports"] = flags.reports;
    comps["attachments"] = flags.attachments;
    comps["lua_scripts"] = flags.luaScripts;
    comps["prompt_logs"] = flags.promptLogs;
    comps["embeddings"] = flags.embeddings;
    manifest["components"] = comps;

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "  ";
    return Json::writeString(wb, manifest);
}

// ────────────────────────────────────────────────────────────────────────────
// Reader stub — implementation in Phase 2
// ────────────────────────────────────────────────────────────────────────────

AgentArchiveReader::AgentArchiveReader(IDataStore* store)
    : m_store(store) {}

std::string AgentArchiveReader::Read(const std::string& archivePath,
                                      ImportMode mode,
                                      const std::string& targetAgentId) {
    // Phase 2 — to be implemented
    (void)archivePath;
    (void)mode;
    (void)targetAgentId;
    return "import not yet implemented";
}

std::string AgentArchiveReader::Inspect(const std::string& archivePath) {
    // Phase 2 — to be implemented
    (void)archivePath;
    return "{}";
}

} // namespace animus::kernel
