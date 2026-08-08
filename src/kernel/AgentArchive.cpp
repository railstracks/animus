#include "animus_kernel/AgentArchive.h"
#include "animus_kernel/IDataStore.h"
#include "animus_kernel/Log.h"

#include <json/json.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <zlib.h>

namespace animus::kernel {

// ============================================================================
// Shared helpers (anonymous namespace)
// ============================================================================
namespace {

// ── Tar writing ────────────────────────────────────────────────────────────

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
    char magic[6];
    char version[2];
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
    size_t nameLen = std::min(name.size(), size_t(100));
    std::memcpy(hdr.name, name.c_str(), nameLen);
    WriteOctal(hdr.mode, sizeof(hdr.mode), 0644);
    WriteOctal(hdr.uid, sizeof(hdr.uid), 0);
    WriteOctal(hdr.gid, sizeof(hdr.gid), 0);
    WriteOctal(hdr.size, sizeof(hdr.size), size);
    WriteOctal(hdr.mtime, sizeof(hdr.mtime), std::time(nullptr));
    hdr.typeflag = '0';
    std::memcpy(hdr.magic, "ustar", 5);
    hdr.magic[5] = '\0';
    hdr.version[0] = '0';
    hdr.version[1] = '0';
    std::memcpy(hdr.uname, "animus", 6);
    std::memcpy(hdr.gname, "animus", 6);
    std::memset(hdr.chksum, ' ', sizeof(hdr.chksum));
    uint32_t chksum = 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&hdr);
    for (size_t i = 0; i < sizeof(hdr); ++i) chksum += p[i];
    WriteOctal(hdr.chksum, sizeof(hdr.chksum), chksum);
    hdr.chksum[sizeof(hdr.chksum) - 1] = ' ';
    out.insert(out.end(), reinterpret_cast<uint8_t*>(&hdr),
               reinterpret_cast<uint8_t*>(&hdr) + sizeof(hdr));
}

void WriteTarData(std::vector<uint8_t>& out, const std::string& data) {
    out.insert(out.end(), data.begin(), data.end());
    size_t padding = (512 - (data.size() % 512)) % 512;
    if (padding > 0) out.insert(out.end(), padding, 0);
}

void WriteTarEnd(std::vector<uint8_t>& out) {
    out.insert(out.end(), 1024, 0);
}

// ── Tar reading ────────────────────────────────────────────────────────────

struct TarFile {
    std::string name;
    std::vector<uint8_t> data;
};

std::vector<TarFile> ParseTar(const std::vector<uint8_t>& tar) {
    std::vector<TarFile> files;
    size_t pos = 0;
    while (pos + 512 <= tar.size()) {
        const uint8_t* hdr = &tar[pos];
        bool allZero = true;
        for (int i = 0; i < 512; ++i) {
            if (hdr[i] != 0) { allZero = false; break; }
        }
        if (allZero) break;

        char nameBuf[101];
        std::memcpy(nameBuf, hdr, 100);
        nameBuf[100] = '\0';
        std::string name(nameBuf);

        char prefixBuf[156];
        std::memcpy(prefixBuf, hdr + 345, 155);
        prefixBuf[155] = '\0';
        std::string prefix(prefixBuf);
        size_t pn = prefix.find('\0');
        if (pn != std::string::npos) prefix = prefix.substr(0, pn);
        if (!prefix.empty()) name = prefix + "/" + name;

        size_t nn = name.find('\0');
        if (nn != std::string::npos) name = name.substr(0, nn);

        char sizeBuf[13];
        std::memcpy(sizeBuf, hdr + 124, 12);
        sizeBuf[12] = '\0';
        uint64_t size = 0;
        for (int i = 0; i < 12; ++i) {
            if (sizeBuf[i] >= '0' && sizeBuf[i] <= '7')
                size = size * 8 + (sizeBuf[i] - '0');
        }

        char typeflag = hdr[156];
        if (typeflag == '0' || typeflag == '\0') {
            if (pos + 512 + size > tar.size()) break;
            TarFile file;
            file.name = name;
            file.data.assign(tar.begin() + pos + 512, tar.begin() + pos + 512 + size);
            files.push_back(std::move(file));
        }
        // Advance past header (512) + data (padded to 512)
        pos += 512 + ((size + 511) / 512) * 512;
    }
    return files;
}

// ── Gzip / Gunzip ──────────────────────────────────────────────────────────

bool GzipBuffer(const std::vector<uint8_t>& input,
                std::vector<uint8_t>& output) {
    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                     16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return false;
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

bool GunzipBuffer(const std::vector<uint8_t>& input,
                  std::vector<uint8_t>& output) {
    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) return false;
    zs.next_in = const_cast<Bytef*>(input.data());
    zs.avail_in = static_cast<uInt>(input.size());
    const size_t bufSize = 65536;
    std::vector<uint8_t> buf(bufSize);
    do {
        zs.next_out = buf.data();
        zs.avail_out = static_cast<uInt>(bufSize);
        int ret = inflate(&zs, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || (ret == Z_BUF_ERROR && zs.avail_out == bufSize)) {
            inflateEnd(&zs);
            return false;
        }
        size_t written = bufSize - zs.avail_out;
        output.insert(output.end(), buf.data(), buf.data() + written);
        if (ret == Z_STREAM_END) break;
    } while (true);
    inflateEnd(&zs);
    return true;
}

// ── JSON helpers ───────────────────────────────────────────────────────────

std::string EncodeBase64(const std::vector<uint8_t>& blob) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (uint8_t c : blob) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(alphabet[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6)
        out.push_back(alphabet[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::vector<uint8_t> DecodeBase64(const std::string& b64) {
    static const int dt[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    };
    std::vector<uint8_t> out;
    int val = 0, valb = -8;
    for (unsigned char c : b64) {
        int d = dt[c];
        if (d == -1) break;
        val = (val << 6) + d;
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

Json::Value RowToJson(IStatement* stmt, const std::vector<std::string>& cols) {
    Json::Value obj(Json::objectValue);
    for (size_t i = 0; i < cols.size(); ++i) {
        if (stmt->IsColumnNull(static_cast<int>(i))) {
            obj[cols[i]] = Json::nullValue;
            continue;
        }
        switch (stmt->GetColumnType(static_cast<int>(i))) {
            case ColumnType::Integer:
                obj[cols[i]] = static_cast<Json::Int64>(stmt->ColumnInt64(static_cast<int>(i)));
                break;
            case ColumnType::Float:
                obj[cols[i]] = stmt->ColumnDouble(static_cast<int>(i));
                break;
            case ColumnType::Text:
                obj[cols[i]] = stmt->ColumnText(static_cast<int>(i));
                break;
            case ColumnType::Null:
                obj[cols[i]] = Json::nullValue;
                break;
            default: {
                auto blob = stmt->ColumnBlob(static_cast<int>(i));
                obj[cols[i]] = "_base64:" + EncodeBase64(blob);
                break;
            }
        }
    }
    return obj;
}

std::string DumpTable(IDataStore* store,
                      const std::string& table,
                      const std::vector<std::string>& cols,
                      const std::string& agentId,
                      const std::string& agentCol = "agent_id",
                      const std::string& extraWhere = "") {
    std::ostringstream sql;
    sql << "SELECT ";
    for (size_t i = 0; i < cols.size(); ++i) {
        if (i > 0) sql << ", ";
        sql << cols[i];
    }
    sql << " FROM " << table << " WHERE " << agentCol << " = ?";
    if (!extraWhere.empty()) sql << " AND (" << extraWhere << ")";

    auto stmt = store->Prepare(sql.str());
    if (!stmt) return "";
    stmt->BindText(1, agentId);

    std::ostringstream jsonl;
    int64_t exportId = 0;
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    while (stmt->Step()) {
        Json::Value row = RowToJson(stmt.get(), cols);
        row["_export_id"] = static_cast<Json::Int64>(++exportId);
        jsonl << Json::writeString(wb, row) << "\n";
    }
    return jsonl.str();
}

std::string DumpTableJoin(IDataStore* store,
                          const std::string& selectSql,
                          const std::string& agentId,
                          const std::vector<std::string>& cols) {
    auto stmt = store->Prepare(selectSql);
    if (!stmt) return "";
    stmt->BindText(1, agentId);

    std::ostringstream jsonl;
    int64_t exportId = 0;
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    while (stmt->Step()) {
        Json::Value row = RowToJson(stmt.get(), cols);
        row["_export_id"] = static_cast<Json::Int64>(++exportId);
        jsonl << Json::writeString(wb, row) << "\n";
    }
    return jsonl.str();
}

std::vector<Json::Value> ParseJSONL(const std::string& content) {
    std::vector<Json::Value> result;
    std::istringstream stream(content);
    std::string line;
    Json::CharReaderBuilder rb;
    std::string errs;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        Json::Value val;
        std::istringstream ls(line);
        if (Json::parseFromStream(rb, ls, &val, &errs)) result.push_back(val);
    }
    return result;
}

// ── Import field extraction ────────────────────────────────────────────────

struct FieldValue {
    bool isNull = false;
    bool isInt = false;
    int64_t intVal = 0;
    bool isDouble = false;
    double doubleVal = 0;
    std::string strVal;
    bool isBlob = false;
    std::vector<uint8_t> blobVal;
};

FieldValue ExtractField(const Json::Value& val) {
    FieldValue fv;
    if (val.isNull()) { fv.isNull = true; }
    else if (val.isInt64()) { fv.isInt = true; fv.intVal = val.asInt64(); }
    else if (val.isDouble()) { fv.isDouble = true; fv.doubleVal = val.asDouble(); }
    else if (val.isString()) {
        std::string s = val.asString();
        if (s.rfind("_base64:", 0) == 0) {
            fv.isBlob = true;
            fv.blobVal = DecodeBase64(s.substr(8));
        } else { fv.strVal = s; }
    } else if (val.isBool()) { fv.isInt = true; fv.intVal = val.asBool() ? 1 : 0; }
    return fv;
}

void BindField(IStatement* stmt, int idx, const FieldValue& fv) {
    if (fv.isNull) stmt->BindNull(idx);
    else if (fv.isInt) stmt->BindInt64(idx, fv.intVal);
    else if (fv.isDouble) stmt->BindDouble(idx, fv.doubleVal);
    else if (fv.isBlob) stmt->BindBlob(idx, fv.blobVal.data(), fv.blobVal.size());
    else stmt->BindText(idx, fv.strVal);
}

} // anonymous namespace

// ============================================================================
// AgentArchiveWriter
// ============================================================================

AgentArchiveWriter::AgentArchiveWriter(IDataStore* store) : m_store(store) {}

std::string AgentArchiveWriter::Write(const std::string& agentId,
                                       const AgentArchiveComponentFlags& flags,
                                       const std::string& outputPath) {
    if (!m_store) return "no data store";

    std::vector<std::pair<std::string, std::string>> files;

    // manifest.json
    files.push_back({"manifest.json", BuildManifest(agentId, flags)});

    // agent.json
    {
        auto stmt = m_store->Prepare(
            "SELECT agent_id, name, description, identity, avatar, "
            "default_provider, default_model, default_vision_model, "
            "intake_interval, intake_prompt, context_window, temperature, "
            "reasoning_enabled, reasoning_effort, max_chain_steps, "
            "max_tool_calls_per_chain, timeout_seconds, episodic_token_budget, "
            "semantic_token_budget, perspectives_token_budget, "
            "consolidation_tool_budget, enabled_tools, tool_configs, "
            "diary_secret, pad_context, created_at_unix_ms, updated_at_unix_ms "
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
            "diary_secret", "pad_context", "created_at_unix_ms", "updated_at_unix_ms"
        };
        Json::Value agentObj = RowToJson(stmt.get(), agentCols);
        Json::StreamWriterBuilder wb;
        wb["indentation"] = "  ";
        files.push_back({"agent.json", Json::writeString(wb, agentObj)});
    }

    // memory/
    files.push_back({"memory/layers.jsonl",
        DumpTable(m_store, "memory_layers",
            {"id", "agent_id", "name", "horizon", "sort_order",
             "evaluation_interval_seconds", "cron_expr",
             "consolidation_prompt", "consolidation_intake_prompt",
             "intake_interval", "token_budget", "enabled",
             "created_at_unix_ms", "updated_at_unix_ms"}, agentId)});
    files.push_back({"memory/observations.jsonl",
        DumpTable(m_store, "observations",
            {"id", "layer_id", "agent_id", "text", "weight",
             "decay_rate", "tags", "source",
             "created_at_unix_ms", "updated_at_unix_ms",
             "last_evaluated_at_ms", "next_review_at_ms",
             "memory_state", "superseded_by"}, agentId)});

    // memory-files/
    if (flags.memoryFiles) {
        files.push_back({"memory-files/files.jsonl",
            DumpTable(m_store, "memory_files",
                {"id", "source_path", "file_type", "content",
                 "content_mutable", "agent_id", "superseded",
                 "created_at_unix_ms", "imported_at_unix_ms"}, agentId)});
        if (flags.embeddings) {
            files.push_back({"memory-files/chunks.jsonl",
                DumpTableJoin(m_store,
                    "SELECT c.id, c.file_id, c.source_path, c.header_title, "
                    "c.chunk_index, c.content, c.content_hash, "
                    "c.start_line, c.end_line, c.embedding, c.embedding_dim, "
                    "c.created_at_unix_ms "
                    "FROM memory_file_chunks c "
                    "INNER JOIN memory_files f ON c.file_id = f.id "
                    "WHERE f.agent_id = ?", agentId,
                    {"id", "file_id", "source_path", "header_title",
                     "chunk_index", "content", "content_hash",
                     "start_line", "end_line", "embedding", "embedding_dim",
                     "created_at_unix_ms"})});
        }
    }

    // ontology/
    if (flags.ontology) {
        files.push_back({"ontology/entities.jsonl",
            DumpTable(m_store, "ontology_entities",
                {"id", "parent_id", "root_category", "name", "full_path",
                 "sort_order", "agent_id", "created_at_unix_ms", "updated_at_unix_ms"},
                agentId)});
        files.push_back({"ontology/properties.jsonl",
            DumpTableJoin(m_store,
                "SELECT p.id, p.entity_id, p.key, p.value, p.value_type, "
                "p.memory_state, p.agent_id, p.linked_observation_id, "
                "p.created_at_unix_ms, p.updated_at_unix_ms "
                "FROM ontology_properties p "
                "INNER JOIN ontology_entities e ON p.entity_id = e.id "
                "WHERE e.agent_id = ?", agentId,
                {"id", "entity_id", "key", "value", "value_type",
                 "memory_state", "agent_id", "linked_observation_id",
                 "created_at_unix_ms", "updated_at_unix_ms"})});
    }

    // gallivanting/
    if (flags.gallivanting) {
        files.push_back({"gallivanting/threads.jsonl",
            DumpTable(m_store, "gallivanting_threads",
                {"id", "agent_id", "name", "description", "sdt_tags",
                 "prompt_template", "enabled",
                 "created_at_unix_ms", "updated_at_unix_ms"}, agentId)});
        if (flags.gallivantingHistory) {
            files.push_back({"gallivanting/sessions.jsonl",
                DumpTable(m_store, "gallivanting_sessions",
                    {"id", "thread_id", "agent_id", "started_at_unix_ms",
                     "duration_ms", "summary", "outcome",
                     "sdt_scores", "tools_used", "created_at_unix_ms"}, agentId)});
        }
    }

    // diary/
    if (flags.diary) {
        files.push_back({"diary/entries.jsonl",
            DumpTable(m_store, "diary_entries",
                {"id", "entry_id", "agent_id", "timestamp_unix_ms",
                 "layer", "content", "tags", "session_id"}, agentId)});
    }

    // schedules
    if (flags.schedules) {
        files.push_back({"schedules.jsonl",
            DumpTable(m_store, "schedules",
                {"id", "agent_id", "tag", "type", "next_fire",
                 "cron_expr", "timezone", "message", "enabled",
                 "created_at", "last_fire", "fire_count", "max_fires"}, agentId)});
        files.push_back({"consolidation/runs.jsonl",
            DumpTable(m_store, "consolidation_runs",
                {"id", "agent_id", "phase", "started_unix_ms",
                 "finished_unix_ms", "status", "summary_json", "error"}, agentId)});
        files.push_back({"consolidation/watermarks.jsonl",
            DumpTable(m_store, "consolidation_watermarks",
                {"agent_id", "source", "last_processed_id",
                 "last_run_unix_ms"}, agentId)});
    }

    // lua_scripts
    if (flags.luaScripts) {
        files.push_back({"lua-scripts.jsonl",
            DumpTable(m_store, "lua_scripts",
                {"id", "agent_id", "name", "source", "description",
                 "enabled", "created_at", "updated_at"}, agentId)});
    }

    // Build tar
    std::vector<uint8_t> tarball;
    for (const auto& [name, content] : files) {
        WriteTarHeader(tarball, name, content.size());
        WriteTarData(tarball, content);
    }
    WriteTarEnd(tarball);

    // Gzip
    std::vector<uint8_t> compressed;
    if (!GzipBuffer(tarball, compressed)) return "gzip failed";

    // Write to file
    std::ofstream out(outputPath, std::ios::binary);
    if (!out) return "cannot open output file: " + outputPath;
    out.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
    out.close();
    return out ? "" : "write failed: " + outputPath;
}

std::string AgentArchiveWriter::BuildManifest(const std::string& agentId,
                                               const AgentArchiveComponentFlags& flags) {
    Json::Value manifest(Json::objectValue);
    manifest["format_version"] = "1.0";
    manifest["format_name"] = "animus-agent-archive";
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    std::ostringstream ts;
    ts << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    manifest["exported_at"] = ts.str();
    manifest["source_system"] = "animus";
    Json::Value agentInfo(Json::objectValue);
    agentInfo["agent_id"] = agentId;
    manifest["agent"] = agentInfo;
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

// ============================================================================
// AgentArchiveReader
// ============================================================================

AgentArchiveReader::AgentArchiveReader(IDataStore* store) : m_store(store) {}

std::string AgentArchiveReader::Read(const std::string& archivePath,
                                      ImportMode mode,
                                      const std::string& targetAgentId) {
    if (!m_store) return "no data store";

    ALOG_INFO("archive", "import started: " << archivePath << " mode=" << static_cast<int>(mode) << " target=" << (targetAgentId.empty() ? "(default)" : targetAgentId));

    // Read and decompress
    std::ifstream file(archivePath, std::ios::binary);
    if (!file) return "cannot open archive: " + archivePath;
    std::vector<uint8_t> compressed(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    file.close();

    std::vector<uint8_t> tarData;
    if (!GunzipBuffer(compressed, tarData)) return "failed to decompress archive";

    auto tarFiles = ParseTar(tarData);
    if (tarFiles.empty()) return "empty or invalid archive";

    std::unordered_map<std::string, std::string> fileMap;
    for (auto& tf : tarFiles) {
        ALOG_INFO("archive", "found file in archive: " << tf.name << " (" << tf.data.size() << " bytes)");
        fileMap[tf.name] = std::string(tf.data.begin(), tf.data.end());
    }

    // Parse manifest
    auto mit = fileMap.find("manifest.json");
    if (mit == fileMap.end()) return "missing manifest.json";
    Json::CharReaderBuilder rb;
    std::string errs;
    Json::Value manifest;
    std::istringstream ms(mit->second);
    if (!Json::parseFromStream(rb, ms, &manifest, &errs))
        return "failed to parse manifest.json: " + errs;

    std::string sourceAgentId;
    if (manifest.isMember("agent") && manifest["agent"].isMember("agent_id"))
        sourceAgentId = manifest["agent"]["agent_id"].asString();
    if (sourceAgentId.empty()) return "manifest missing agent_id";

    std::string agentId = targetAgentId.empty() ? sourceAgentId : targetAgentId;

    // Handle import mode
    if (mode == ImportMode::Replace) {
        m_store->Exec("DELETE FROM observations WHERE agent_id = '" + agentId + "'");
        m_store->Exec("DELETE FROM memory_layers WHERE agent_id = '" + agentId + "'");
        m_store->Exec("DELETE FROM memory_files WHERE agent_id = '" + agentId + "'");
        m_store->Exec("DELETE FROM diary_entries WHERE agent_id = '" + agentId + "'");
        m_store->Exec("DELETE FROM gallivanting_threads WHERE agent_id = '" + agentId + "'");
        m_store->Exec("DELETE FROM schedules WHERE agent_id = '" + agentId + "'");
        m_store->Exec("DELETE FROM lua_scripts WHERE agent_id = '" + agentId + "'");
        m_store->Exec("DELETE FROM ontology_entities WHERE agent_id = '" + agentId + "'");
        m_store->Exec("DELETE FROM consolidation_runs WHERE agent_id = '" + agentId + "'");
        m_store->Exec("DELETE FROM consolidation_watermarks WHERE agent_id = '" + agentId + "'");
        m_idMap.clear();
    }

    if (mode == ImportMode::New) {
        auto stmt = m_store->Prepare("SELECT agent_id FROM agents WHERE agent_id = ?");
        if (stmt) {
            stmt->BindText(1, agentId);
            if (stmt->Step())
                return "agent already exists: " + agentId + " (use merge or replace mode)";
        }
        m_idMap.clear();
    }

    // Import agent record
    auto ait = fileMap.find("agent.json");
    ALOG_INFO("archive", "agent.json lookup: " << (ait != fileMap.end() ? "found" : "not found"));
    if (ait != fileMap.end()) {
        Json::Value agentJson;
        std::istringstream as(ait->second);
        if (Json::parseFromStream(rb, as, &agentJson, &errs)) {
            ALOG_INFO("archive", "agent.json parsed, member names: " << agentJson.getMemberNames().size());
            agentJson["agent_id"] = agentId;

            auto check = m_store->Prepare("SELECT agent_id FROM agents WHERE agent_id = ?");
            bool exists = false;
            if (check) {
                check->BindText(1, agentId);
                exists = check->Step();
            }

            if (!exists) {
                std::ostringstream cols, vals;
                std::vector<FieldValue> fv;
                for (const auto& key : agentJson.getMemberNames()) {
                    if (key == "_export_id" || key == "_original_id") continue;
                    if (!cols.str().empty()) { cols << ", "; vals << ", "; }
                    cols << key; vals << "?";
                    fv.push_back(ExtractField(agentJson[key]));
                }
                auto stmt = m_store->Prepare(
                    "INSERT INTO agents (" + cols.str() + ") VALUES (" + vals.str() + ")");
                if (stmt) {
                    for (size_t i = 0; i < fv.size(); ++i)
                        BindField(stmt.get(), static_cast<int>(i + 1), fv[i]);
                    if (!stmt->ExecDML()) {
                        return "failed to insert agent record: " + m_store->ErrMsg();
                    }
                    ALOG_INFO("archive", "inserted agent record for " << agentId);
                } else {
                    return "failed to prepare agent insert: " + m_store->ErrMsg();
                }
            } else if (mode == ImportMode::Merge) {
                std::ostringstream sets;
                std::vector<FieldValue> fv;
                for (const auto& key : agentJson.getMemberNames()) {
                    if (key == "_export_id" || key == "_original_id" || key == "agent_id") continue;
                    if (!sets.str().empty()) sets << ", ";
                    sets << key << " = ?";
                    fv.push_back(ExtractField(agentJson[key]));
                }
                if (!fv.empty()) {
                    auto stmt = m_store->Prepare(
                        "UPDATE agents SET " + sets.str() + " WHERE agent_id = ?");
                    if (stmt) {
                        for (size_t i = 0; i < fv.size(); ++i)
                            BindField(stmt.get(), static_cast<int>(i + 1), fv[i]);
                        stmt->BindText(static_cast<int>(fv.size() + 1), agentId);
                        stmt->ExecDML();
                    }
                }
            }
        } else {
            ALOG_ERROR("archive", "failed to parse agent.json: " << errs);
            return "failed to parse agent.json: " + errs;
        }
    }

    // Helper: import a JSONL table
    auto importTable = [&](const std::string& filePath,
                           const std::string& tableName,
                           const std::string& idCol,
                           const std::unordered_map<std::string, std::string>& fkRemap = {}) -> void {
        auto fit = fileMap.find(filePath);
        if (fit == fileMap.end()) return;
        auto rows = ParseJSONL(fit->second);
        for (const auto& row : rows) {
            int64_t exportId = row.isMember("_export_id") ? row["_export_id"].asInt64() : 0;
            std::ostringstream cols, vals;
            std::vector<FieldValue> fv;
            for (const auto& key : row.getMemberNames()) {
                if (key == "_export_id" || key == "_original_id" || key == idCol) continue;
                if (!cols.str().empty()) { cols << ", "; vals << ", "; }
                cols << key; vals << "?";
                FieldValue val = ExtractField(row[key]);
                auto fkIt = fkRemap.find(key);
                if (fkIt != fkRemap.end() && !val.isNull && val.isInt) {
                    auto mapIt = m_idMap.find(fkIt->second + ":" + std::to_string(val.intVal));
                    if (mapIt != m_idMap.end()) val.intVal = mapIt->second;
                }
                if (key == "agent_id") val.strVal = agentId;
                fv.push_back(val);
            }
            auto stmt = m_store->Prepare(
                "INSERT INTO " + tableName + " (" + cols.str() + ") VALUES (" + vals.str() + ")");
            if (!stmt) {
                ALOG_WARNING("archive", "failed to prepare insert into " << tableName << ": " << m_store->ErrMsg());
                continue;
            }
            for (size_t i = 0; i < fv.size(); ++i)
                BindField(stmt.get(), static_cast<int>(i + 1), fv[i]);
            if (!stmt->ExecDML()) {
                ALOG_WARNING("archive", "insert into " << tableName << " failed: " << m_store->ErrMsg());
                continue;
            }
            int64_t newId = m_store->LastInsertRowId();
            if (exportId > 0)
                m_idMap[tableName + ":" + std::to_string(exportId)] = newId;
        }
    };

    // Memory layers (before observations)
    {
        auto fit = fileMap.find("memory/layers.jsonl");
        if (fit != fileMap.end()) {
            auto rows = ParseJSONL(fit->second);
            for (const auto& row : rows) {
                int64_t exportId = row.isMember("_export_id") ? row["_export_id"].asInt64() : 0;
                std::string layerName = row.get("name", "").asString();

                // Merge: check existing
                bool exists = false;
                int64_t existingId = -1;
                if (mode == ImportMode::Merge) {
                    auto chk = m_store->Prepare("SELECT id FROM memory_layers WHERE agent_id = ? AND name = ?");
                    if (chk) {
                        chk->BindText(1, agentId);
                        chk->BindText(2, layerName);
                        if (chk->Step()) { existingId = chk->ColumnInt64(0); exists = true; }
                    }
                }
                if (exists) {
                    if (exportId > 0)
                        m_idMap["memory_layers:" + std::to_string(exportId)] = existingId;
                    continue;
                }

                std::ostringstream cols, vals;
                std::vector<FieldValue> fv;
                for (const auto& key : row.getMemberNames()) {
                    if (key == "_export_id" || key == "_original_id" || key == "id") continue;
                    if (!cols.str().empty()) { cols << ", "; vals << ", "; }
                    cols << key; vals << "?";
                    FieldValue val = ExtractField(row[key]);
                    if (key == "agent_id") val.strVal = agentId;
                    fv.push_back(val);
                }
                auto stmt = m_store->Prepare(
                    "INSERT INTO memory_layers (" + cols.str() + ") VALUES (" + vals.str() + ")");
                if (stmt) {
                    for (size_t i = 0; i < fv.size(); ++i)
                        BindField(stmt.get(), static_cast<int>(i + 1), fv[i]);
                    if (stmt->ExecDML()) {
                        int64_t newId = m_store->LastInsertRowId();
                        if (exportId > 0)
                            m_idMap["memory_layers:" + std::to_string(exportId)] = newId;
                    }
                }
            }
        }
    }

    // Observations (FK: layer_id → memory_layers)
    importTable("memory/observations.jsonl", "observations", "id",
                {{"layer_id", "memory_layers"}});

    // Memory files
    importTable("memory-files/files.jsonl", "memory_files", "id", {});

    // Memory file chunks (FK: file_id → memory_files)
    importTable("memory-files/chunks.jsonl", "memory_file_chunks", "id",
                {{"file_id", "memory_files"}});

    // Ontology entities (FK: parent_id → ontology_entities, self-referential)
    {
        auto fit = fileMap.find("ontology/entities.jsonl");
        if (fit != fileMap.end()) {
            auto rows = ParseJSONL(fit->second);
            for (const auto& row : rows) {
                int64_t exportId = row.isMember("_export_id") ? row["_export_id"].asInt64() : 0;
                std::ostringstream cols, vals;
                std::vector<FieldValue> fv;
                for (const auto& key : row.getMemberNames()) {
                    if (key == "_export_id" || key == "_original_id" || key == "id") continue;
                    if (!cols.str().empty()) { cols << ", "; vals << ", "; }
                    cols << key; vals << "?";
                    FieldValue val = ExtractField(row[key]);
                    if (key == "parent_id" && !val.isNull && val.isInt && val.intVal > 0) {
                        auto it = m_idMap.find("ontology_entities:" + std::to_string(val.intVal));
                        if (it != m_idMap.end()) val.intVal = it->second;
                    }
                    if (key == "agent_id") val.strVal = agentId;
                    fv.push_back(val);
                }
                auto stmt = m_store->Prepare(
                    "INSERT INTO ontology_entities (" + cols.str() + ") VALUES (" + vals.str() + ")");
                if (stmt) {
                    for (size_t i = 0; i < fv.size(); ++i)
                        BindField(stmt.get(), static_cast<int>(i + 1), fv[i]);
                    if (stmt->ExecDML()) {
                        int64_t newId = m_store->LastInsertRowId();
                        if (exportId > 0)
                            m_idMap["ontology_entities:" + std::to_string(exportId)] = newId;
                    }
                }
            }
        }
    }

    // Ontology properties (FK: entity_id → ontology_entities)
    importTable("ontology/properties.jsonl", "ontology_properties", "id",
                {{"entity_id", "ontology_entities"}});

    // Gallivanting
    importTable("gallivanting/threads.jsonl", "gallivanting_threads", "id", {});
    importTable("gallivanting/sessions.jsonl", "gallivanting_sessions", "id",
                {{"thread_id", "gallivanting_threads"}});

    // Diary
    importTable("diary/entries.jsonl", "diary_entries", "id", {});

    // Schedules
    importTable("schedules.jsonl", "schedules", "id", {});

    // Consolidation
    importTable("consolidation/runs.jsonl", "consolidation_runs", "id", {});
    importTable("consolidation/watermarks.jsonl", "consolidation_watermarks", "id", {});

    // Lua scripts
    importTable("lua-scripts.jsonl", "lua_scripts", "id", {});

    ALOG_INFO("archive", "import complete: " << agentId << " (" << m_idMap.size() << " id mappings)");

    return "";
}

std::string AgentArchiveReader::Inspect(const std::string& archivePath) {
    std::ifstream file(archivePath, std::ios::binary);
    if (!file) return "{\"error\": \"cannot open file\"}";
    std::vector<uint8_t> compressed(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    file.close();
    std::vector<uint8_t> tarData;
    if (!GunzipBuffer(compressed, tarData))
        return "{\"error\": \"decompression failed\"}";
    auto tarFiles = ParseTar(tarData);
    for (const auto& tf : tarFiles) {
        if (tf.name == "manifest.json")
            return std::string(tf.data.begin(), tf.data.end());
    }
    return "{\"error\": \"manifest not found\"}";
}

} // namespace animus::kernel
