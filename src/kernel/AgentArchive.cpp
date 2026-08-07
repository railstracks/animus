#include "animus_kernel/AgentArchive.h"
#include "animus_kernel/IDataStore.h"

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

// Re-use helpers from the writer's anonymous namespace by redeclaring here.
// In a real codebase these would be in a shared internal header.

namespace {

// ────────────────────────────────────────────────────────────────────────────
// Minimal tar reader (POSIX ustar)
// ────────────────────────────────────────────────────────────────────────────

struct TarFile {
    std::string name;
    std::vector<uint8_t> data;
};

// Parse a tar archive (uncompressed) into a map of name → data.
std::vector<TarFile> ParseTar(const std::vector<uint8_t>& tar) {
    std::vector<TarFile> files;
    size_t pos = 0;
    while (pos + 512 <= tar.size()) {
        const uint8_t* hdr = &tar[pos];

        // Check for end-of-archive (two zero blocks)
        bool allZero = true;
        for (int i = 0; i < 512; ++i) {
            if (hdr[i] != 0) { allZero = false; break; }
        }
        if (allZero) break;

        // Extract name (100 bytes at offset 0)
        char nameBuf[101];
        std::memcpy(nameBuf, hdr, 100);
        nameBuf[100] = '\0';
        std::string name(nameBuf);

        // Check prefix field (155 bytes at offset 345)
        char prefixBuf[156];
        std::memcpy(prefixBuf, hdr + 345, 155);
        prefixBuf[155] = '\0';
        std::string prefix(prefixBuf);
        // Trim at first null
        size_t pn = prefix.find('\0');
        if (pn != std::string::npos) prefix = prefix.substr(0, pn);
        if (!prefix.empty()) {
            name = prefix + "/" + name;
        }

        // Trim name at first null
        size_t nn = name.find('\0');
        if (nn != std::string::npos) name = name.substr(0, nn);

        // Parse size (12 octal digits at offset 124)
        char sizeBuf[13];
        std::memcpy(sizeBuf, hdr + 124, 12);
        sizeBuf[12] = '\0';
        uint64_t size = 0;
        for (int i = 0; i < 12; ++i) {
            if (sizeBuf[i] >= '0' && sizeBuf[i] <= '7') {
                size = size * 8 + (sizeBuf[i] - '0');
            }
        }

        // Skip non-regular files
        char typeflag = hdr[156];
        if (typeflag == '0' || typeflag == '\0') {
            pos += 512;
            if (pos + size > tar.size()) break;

            TarFile file;
            file.name = name;
            file.data.assign(tar.begin() + pos, tar.begin() + pos + size);
            files.push_back(std::move(file));
        }

        // Advance past data (padded to 512)
        pos += 512 + ((size + 511) / 512) * 512;
    }
    return files;
}

// Gunzip a buffer using zlib
bool GunzipBuffer(const std::vector<uint8_t>& input,
                  std::vector<uint8_t>& output) {
    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));

    if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) {
        return false;
    }

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

// Parse JSONL into a vector of JSON objects.
std::vector<Json::Value> ParseJSONL(const std::string& content) {
    std::vector<Json::Value> result;
    std::istringstream stream(content);
    std::string line;
    Json::CharReaderBuilder rb;
    std::string errs;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        Json::Value val;
        std::istringstream lineStream(line);
        if (Json::parseFromStream(rb, lineStream, &val, &errs)) {
            result.push_back(val);
        }
    }
    return result;
}

// Decode base64 prefixed values back to blobs
std::vector<uint8_t> DecodeBase64(const std::string& b64) {
    static const int decodeTable[256] = {
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

    std::vector<uint8_t> output;
    int val = 0, valb = -8;
    for (unsigned char c : b64) {
        int d = decodeTable[c];
        if (d == -1) break;
        val = (val << 6) + d;
        valb += 6;
        if (valb >= 0) {
            output.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return output;
}

// Resolve "_base64:" prefixed strings back to blob data for binding
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
    if (val.isNull()) {
        fv.isNull = true;
    } else if (val.isInt64()) {
        fv.isInt = true;
        fv.intVal = val.asInt64();
    } else if (val.isDouble()) {
        fv.isDouble = true;
        fv.doubleVal = val.asDouble();
    } else if (val.isString()) {
        std::string s = val.asString();
        if (s.rfind("_base64:", 0) == 0) {
            fv.isBlob = true;
            fv.blobVal = DecodeBase64(s.substr(8));
        } else {
            fv.strVal = s;
        }
    } else if (val.isBool()) {
        fv.isInt = true;
        fv.intVal = val.asBool() ? 1 : 0;
    }
    return fv;
}

// Bind a field to a prepared statement at position idx
void BindField(IStatement* stmt, int idx, const FieldValue& fv) {
    if (fv.isNull) {
        stmt->BindNull(idx);
    } else if (fv.isInt) {
        stmt->BindInt64(idx, fv.intVal);
    } else if (fv.isDouble) {
        stmt->BindDouble(idx, fv.doubleVal);
    } else if (fv.isBlob) {
        stmt->BindBlob(idx, fv.blobVal.data(), fv.blobVal.size());
    } else {
        stmt->BindText(idx, fv.strVal);
    }
}

} // anonymous namespace

// ────────────────────────────────────────────────────────────────────────────
// AgentArchiveReader implementation
// ────────────────────────────────────────────────────────────────────────────

AgentArchiveReader::AgentArchiveReader(IDataStore* store)
    : m_store(store) {}

std::string AgentArchiveReader::Read(const std::string& archivePath,
                                      ImportMode mode,
                                      const std::string& targetAgentId) {
    if (!m_store) return "no data store";

    // ── Read and decompress the archive ──
    std::ifstream file(archivePath, std::ios::binary);
    if (!file) return "cannot open archive: " + archivePath;

    std::vector<uint8_t> compressed(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    file.close();

    std::vector<uint8_t> tarData;
    if (!GunzipBuffer(compressed, tarData)) {
        return "failed to decompress archive";
    }

    auto tarFiles = ParseTar(tarData);
    if (tarFiles.empty()) return "empty or invalid archive";

    // Build a map of name → content for easy lookup
    std::unordered_map<std::string, std::string> fileMap;
    for (auto& tf : tarFiles) {
        fileMap[tf.name] = std::string(tf.data.begin(), tf.data.end());
    }

    // ── Parse manifest ──
    auto mit = fileMap.find("manifest.json");
    if (mit == fileMap.end()) return "missing manifest.json";

    Json::CharReaderBuilder rb;
    std::string errs;
    Json::Value manifest;
    std::istringstream ms(mit->second);
    if (!Json::parseFromStream(rb, ms, &manifest, &errs)) {
        return "failed to parse manifest.json: " + errs;
    }

    std::string sourceAgentId;
    if (manifest.isMember("agent") && manifest["agent"].isMember("agent_id")) {
        sourceAgentId = manifest["agent"]["agent_id"].asString();
    }
    if (sourceAgentId.empty()) return "manifest missing agent_id";

    // Determine target agent ID
    std::string agentId = targetAgentId.empty() ? sourceAgentId : targetAgentId;

    // ── Handle import mode ──
    if (mode == ImportMode::Replace) {
        // Delete existing agent data (but not the agent record itself if it's default)
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
        // Check if agent already exists
        auto stmt = m_store->Prepare("SELECT agent_id FROM agents WHERE agent_id = ?");
        if (stmt) {
            stmt->BindText(1, agentId);
            if (stmt->Step()) {
                return "agent already exists: " + agentId + " (use merge or replace mode)";
            }
        }
        m_idMap.clear();
    }

    // ── Import agent record ──
    auto ait = fileMap.find("agent.json");
    if (ait != fileMap.end()) {
        Json::Value agentJson;
        std::istringstream as(ait->second);
        if (Json::parseFromStream(rb, as, &agentJson, &errs)) {
            // Rewrite agent_id if different target
            agentJson["agent_id"] = agentId;

            // Check if agent exists
            auto check = m_store->Prepare("SELECT agent_id FROM agents WHERE agent_id = ?");
            bool exists = false;
            if (check) {
                check->BindText(1, agentId);
                exists = check->Step();
            }

            if (!exists) {
                // Insert new agent record
                // We need to build the INSERT dynamically from the JSON fields
                std::ostringstream cols, vals;
                std::vector<std::string> colNames;
                std::vector<FieldValue> fieldVals;

                for (const auto& key : agentJson.getMemberNames()) {
                    if (key == "_export_id" || key == "_original_id") continue;
                    if (!cols.str().empty()) { cols << ", "; vals << ", "; }
                    cols << key;
                    vals << "?";
                    colNames.push_back(key);
                    fieldVals.push_back(ExtractField(agentJson[key]));
                }

                auto stmt = m_store->Prepare(
                    "INSERT INTO agents (" + cols.str() + ") VALUES (" + vals.str() + ")");
                if (stmt) {
                    for (size_t i = 0; i < fieldVals.size(); ++i) {
                        BindField(stmt.get(), static_cast<int>(i + 1), fieldVals[i]);
                    }
                    stmt->ExecDML();
                }
            } else if (mode == ImportMode::Merge) {
                // Update existing agent fields (skip agent_id)
                std::ostringstream sets;
                std::vector<FieldValue> fieldVals;
                for (const auto& key : agentJson.getMemberNames()) {
                    if (key == "_export_id" || key == "_original_id" || key == "agent_id") continue;
                    if (!sets.str().empty()) sets << ", ";
                    sets << key << " = ?";
                    fieldVals.push_back(ExtractField(agentJson[key]));
                }
                if (!fieldVals.empty()) {
                    auto stmt = m_store->Prepare(
                        "UPDATE agents SET " + sets.str() + " WHERE agent_id = ?");
                    if (stmt) {
                        for (size_t i = 0; i < fieldVals.size(); ++i) {
                            BindField(stmt.get(), static_cast<int>(i + 1), fieldVals[i]);
                        }
                        stmt->BindText(static_cast<int>(fieldVals.size() + 1), agentId);
                        stmt->ExecDML();
                    }
                }
            }
        }
    }

    // ── Helper: import a JSONL table with ID remapping ──
    auto importTable = [&](const std::string& filePath,
                           const std::string& tableName,
                           const std::string& idCol,
                           const std::vector<std::string>& fkCols = {},
                           const std::unordered_map<std::string, std::string>& fkRemap = {}) -> std::string {
        auto fit = fileMap.find(filePath);
        if (fit == fileMap.end()) return "";  // file not in archive — skip

        auto rows = ParseJSONL(fit->second);

        for (const auto& row : rows) {
            // Get export_id for mapping
            int64_t exportId = 0;
            if (row.isMember("_export_id")) {
                exportId = row["_export_id"].asInt64();
            }

            // Build column list and values, skipping internal fields
            std::ostringstream cols, vals;
            std::vector<FieldValue> fieldVals;
            std::string mapKey = tableName;

            int bindIdx = 1;
            for (const auto& key : row.getMemberNames()) {
                if (key == "_export_id" || key == "_original_id") continue;

                // Check if this is the ID column (skip — let auto-increment assign)
                if (key == idCol) continue;

                // Check if this is a FK column that needs remapping
                std::string actualKey = key;
                FieldValue fv = ExtractField(row[key]);

                // Remap FK references
                auto fkIt = fkRemap.find(key);
                if (fkIt != fkRemap.end() && !fv.isNull) {
                    // Look up in id map: fkRemap column → table
                    std::string fkMapKey = fkIt->second + ":" + std::to_string(fv.intVal);
                    auto mapIt = m_idMap.find(fkMapKey);
                    if (mapIt != m_idMap.end()) {
                        fv.intVal = mapIt->second;
                        fv.isInt = true;
                    }
                }

                if (!cols.str().empty()) { cols << ", "; vals << ", "; }
                cols << actualKey;
                vals << "?";
                fieldVals.push_back(fv);
                bindIdx++;
            }

            auto stmt = m_store->Prepare(
                "INSERT INTO " + tableName + " (" + cols.str() + ") VALUES (" + vals.str() + ")");
            if (!stmt) continue;

            for (size_t i = 0; i < fieldVals.size(); ++i) {
                BindField(stmt.get(), static_cast<int>(i + 1), fieldVals[i]);
            }

            if (stmt->ExecDML()) {
                // Store the new ID in the map
                int64_t newId = m_store->LastInsertRowId();
                if (exportId > 0) {
                    m_idMap[tableName + ":" + std::to_string(exportId)] = newId;
                }
            }
        }

        return "";
    };

    // ── Import memory layers (must come before observations) ──
    {
        auto fit = fileMap.find("memory/layers.jsonl");
        if (fit != fileMap.end()) {
            auto rows = ParseJSONL(fit->second);
            for (const auto& row : rows) {
                int64_t exportId = row.isMember("_export_id") ? row["_export_id"].asInt64() : 0;

                // For merge mode, check if layer exists by (agent_id, name)
                std::string layerName = row.get("name", "").asString();
                bool exists = false;
                int64_t existingId = -1;
                {
                    auto chk = m_store->Prepare("SELECT id FROM memory_layers WHERE agent_id = ? AND name = ?");
                    if (chk) {
                        chk->BindText(1, agentId);
                        chk->BindText(2, layerName);
                        if (chk->Step()) {
                            existingId = chk->ColumnInt64(0);
                            exists = true;
                        }
                    }
                }

                if (exists && mode == ImportMode::Merge) {
                    // Map export_id → existing id, skip insert
                    if (exportId > 0) {
                        m_idMap["memory_layers:" + std::to_string(exportId)] = existingId;
                    }
                    continue;
                }

                // Insert
                std::ostringstream cols, vals;
                std::vector<FieldValue> fieldVals;
                for (const auto& key : row.getMemberNames()) {
                    if (key == "_export_id" || key == "_original_id" || key == "id") continue;
                    if (!cols.str().empty()) { cols << ", "; vals << ", "; }
                    cols << key;
                    vals << "?";
                    FieldValue fv = ExtractField(row[key]);
                    // Rewrite agent_id if different
                    if (key == "agent_id") fv.strVal = agentId;
                    fieldVals.push_back(fv);
                }

                auto stmt = m_store->Prepare(
                    "INSERT INTO memory_layers (" + cols.str() + ") VALUES (" + vals.str() + ")");
                if (stmt) {
                    for (size_t i = 0; i < fieldVals.size(); ++i) {
                        BindField(stmt.get(), static_cast<int>(i + 1), fieldVals[i]);
                    }
                    if (stmt->ExecDML()) {
                        int64_t newId = m_store->LastInsertRowId();
                        if (exportId > 0) {
                            m_idMap["memory_layers:" + std::to_string(exportId)] = newId;
                        }
                    }
                }
            }
        }
    }

    // ── Import observations (depend on layer_id remapping) ──
    importTable("memory/observations.jsonl", "observations", "id",
                {}, {{"layer_id", "memory_layers"}});

    // ── Import memory files ──
    importTable("memory-files/files.jsonl", "memory_files", "id", {}, {});

    // ── Import memory file chunks (depend on file_id remapping) ──
    importTable("memory-files/chunks.jsonl", "memory_file_chunks", "id",
                {}, {{"file_id", "memory_files"}});

    // ── Import ontology entities (tree structure with parent_id) ──
    {
        auto fit = fileMap.find("ontology/entities.jsonl");
        if (fit != fileMap.end()) {
            auto rows = ParseJSONL(fit->second);
            // Sort: parent_id null/0 first, then by depth
            // The export order should already be correct (BFS from roots)

            for (const auto& row : rows) {
                int64_t exportId = row.isMember("_export_id") ? row["_export_id"].asInt64() : 0;

                // Build insert, remapping parent_id
                std::ostringstream cols, vals;
                std::vector<FieldValue> fieldVals;
                for (const auto& key : row.getMemberNames()) {
                    if (key == "_export_id" || key == "_original_id" || key == "id") continue;
                    if (!cols.str().empty()) { cols << ", "; vals << ", "; }
                    cols << key;
                    vals << "?";
                    FieldValue fv = ExtractField(row[key]);

                    // Remap parent_id
                    if (key == "parent_id" && !fv.isNull && fv.intVal > 0) {
                        auto it = m_idMap.find("ontology_entities:" + std::to_string(fv.intVal));
                        if (it != m_idMap.end()) {
                            fv.intVal = it->second;
                        }
                    }

                    // Rewrite agent_id
                    if (key == "agent_id") fv.strVal = agentId;

                    fieldVals.push_back(fv);
                }

                auto stmt = m_store->Prepare(
                    "INSERT INTO ontology_entities (" + cols.str() + ") VALUES (" + vals.str() + ")");
                if (stmt) {
                    for (size_t i = 0; i < fieldVals.size(); ++i) {
                        BindField(stmt.get(), static_cast<int>(i + 1), fieldVals[i]);
                    }
                    if (stmt->ExecDML()) {
                        int64_t newId = m_store->LastInsertRowId();
                        if (exportId > 0) {
                            m_idMap["ontology_entities:" + std::to_string(exportId)] = newId;
                        }
                    }
                }
            }
        }
    }

    // ── Import ontology properties (depend on entity_id remapping) ──
    importTable("ontology/properties.jsonl", "ontology_properties", "id",
                {}, {{"entity_id", "ontology_entities"}});

    // ── Import gallivanting threads ──
    importTable("gallivanting/threads.jsonl", "gallivanting_threads", "id", {}, {});

    // ── Import gallivanting sessions ──
    importTable("gallivanting/sessions.jsonl", "gallivanting_sessions", "id",
                {}, {{"thread_id", "gallivanting_threads"}});

    // ── Import diary entries ──
    importTable("diary/entries.jsonl", "diary_entries", "id", {}, {});

    // ── Import schedules ──
    importTable("schedules.jsonl", "schedules", "id", {}, {});

    // ── Import consolidation runs ──
    importTable("consolidation/runs.jsonl", "consolidation_runs", "id", {}, {});

    // ── Import consolidation watermarks ──
    importTable("consolidation/watermarks.jsonl", "consolidation_watermarks", "id", {}, {});

    // ── Import lua scripts ──
    importTable("lua-scripts.jsonl", "lua_scripts", "id", {}, {});

    return "";  // success
}

std::string AgentArchiveReader::Inspect(const std::string& archivePath) {
    // Read and decompress
    std::ifstream file(archivePath, std::ios::binary);
    if (!file) return "{\"error\": \"cannot open file\"}";

    std::vector<uint8_t> compressed(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    file.close();

    std::vector<uint8_t> tarData;
    if (!GunzipBuffer(compressed, tarData)) {
        return "{\"error\": \"decompression failed\"}";
    }

    auto tarFiles = ParseTar(tarData);
    for (const auto& tf : tarFiles) {
        if (tf.name == "manifest.json") {
            return std::string(tf.data.begin(), tf.data.end());
        }
    }
    return "{\"error\": \"manifest not found\"}";
}

} // namespace animus::kernel
