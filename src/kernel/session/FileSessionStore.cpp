#include "animus_kernel/FileSessionStore.h"

#include <fstream>
#include <sstream>
#include <iostream>

#include <json/json.h>

namespace animus::kernel {

// Helper: serialize a session to JSON
static Json::Value SessionToJson(const Session& s) {
    Json::Value root;

    root["id"] = static_cast<Json::UInt64>(s.Id());

    auto& key = root["key"];
    key["connector"] = s.Key().connector;
    key["conversation_id"] = s.Key().conversation_id;
    key["thread_id"] = s.Key().thread_id;

    root["created_unix_ms"] = static_cast<Json::UInt64>(s.CreatedUnixMs());
    root["last_active_unix_ms"] = static_cast<Json::UInt64>(s.LastActiveUnixMs());
    root["terminated"] = s.IsTerminated();
    root["provider_id"] = s.ProviderId();
    root["summary"] = s.Summary();

    // Turns
    auto& turnsArr = root["turns"];
    turnsArr = Json::Value(Json::arrayValue);
    for (const auto& t : s.Turns()) {
        Json::Value turn;
        turn["turn_id"] = static_cast<Json::UInt64>(t.turn_id);
        turn["role"] = t.role;
        turn["content"] = t.content;
        turn["unix_ms"] = static_cast<Json::UInt64>(t.unix_ms);
        turn["is_summary"] = t.is_summary;
        turn["intake_processed"] = t.intake_processed;
        turn["intake_processed_at_unix_ms"] =
            static_cast<Json::UInt64>(t.intake_processed_at_unix_ms);
        auto& fromArr = turn["compacted_from"];
        fromArr = Json::Value(Json::arrayValue);
        for (auto id : t.compacted_from) {
            fromArr.append(static_cast<Json::UInt64>(id));
        }
        turnsArr.append(turn);
    }

    // Compaction summary
    const auto* cs = s.GetCompactionSummary();
    if (cs) {
        Json::Value& csVal = root["compaction_summary"];
        csVal["turn_id"] = static_cast<Json::UInt64>(cs->turn_id);
        csVal["role"] = cs->role;
        csVal["content"] = cs->content;
        csVal["unix_ms"] = static_cast<Json::UInt64>(cs->unix_ms);
        csVal["is_summary"] = cs->is_summary;
    }

    return root;
}

// Helper: deserialize a session from JSON
static std::shared_ptr<Session> SessionFromJson(const Json::Value& root) {
    SessionKey key;
    key.connector = root["key"]["connector"].asString();
    key.conversation_id = root["key"]["conversation_id"].asString();
    key.thread_id = root["key"]["thread_id"].asString();

    SessionId id = root["id"].asUInt64();
    auto session = std::make_shared<Session>(id, key);

    // Restore state via Session's public API
    if (root.isMember("provider_id")) {
        session->SetProviderId(root["provider_id"].asString());
    }
    if (root.isMember("summary") && root["summary"].isString()) {
        session->SetSummary(root["summary"].asString());
    }
    if (root.get("terminated", false).asBool()) {
        session->MarkTerminated();
    }

    // Restore turns
    if (root.isMember("turns") && root["turns"].isArray()) {
        for (const auto& tv : root["turns"]) {
            SessionTurn turn;
            turn.turn_id = tv["turn_id"].asUInt64();
            turn.role = tv["role"].asString();
            turn.content = tv["content"].asString();
            turn.unix_ms = tv["unix_ms"].asUInt64();
            turn.is_summary = tv.get("is_summary", false).asBool();
            turn.intake_processed = tv.get("intake_processed", false).asBool();
            if (tv.isMember("intake_processed_at_unix_ms")) {
                turn.intake_processed_at_unix_ms =
                    static_cast<std::uint64_t>(tv["intake_processed_at_unix_ms"].asUInt64());
            }
            if (tv.isMember("compacted_from") && tv["compacted_from"].isArray()) {
                for (const auto& idv : tv["compacted_from"]) {
                    turn.compacted_from.push_back(idv.asUInt64());
                }
            }
            session->AddTurn(std::move(turn));
        }
    }

    // Restore compaction summary
    if (root.isMember("compaction_summary")) {
        const auto& csVal = root["compaction_summary"];
        SessionTurn cs;
        cs.turn_id = csVal["turn_id"].asUInt64();
        cs.role = csVal["role"].asString();
        cs.content = csVal["content"].asString();
        cs.unix_ms = csVal["unix_ms"].asUInt64();
        cs.is_summary = csVal["is_summary"].asBool();
        session->SetCompactionSummary(std::move(cs));
    }

    return session;
}

// ============================================================================
// FileSessionStore
// ============================================================================

FileSessionStore::FileSessionStore(const std::string& filePath)
    : m_filePath(filePath) {
    LoadFromDisk();
}

void FileSessionStore::LoadFromDisk() {
    if (!std::filesystem::exists(m_filePath)) {
        return;
    }

    std::ifstream in(m_filePath);
    if (!in.is_open()) return;

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string err;
    if (!Json::parseFromStream(builder, in, &root, &err)) {
        std::cerr << "[session] failed to parse " << m_filePath << ": " << err << std::endl;
        return;
    }

    if (!root.isMember("sessions") || !root["sessions"].isArray()) return;

    SessionId maxId = 0;
    for (const auto& sv : root["sessions"]) {
        auto session = SessionFromJson(sv);
        if (session->Id() > maxId) maxId = session->Id();
        m_entries.push_back({session});
    }
    m_nextId = maxId + 1;

    std::cerr << "[session] loaded " << m_entries.size() << " sessions from " << m_filePath << std::endl;
}

void FileSessionStore::SaveToDisk() const {
    Json::Value root;
    root["sessions"] = Json::Value(Json::arrayValue);
    for (const auto& entry : m_entries) {
        root["sessions"].append(SessionToJson(*entry.session));
    }

    // Ensure parent directory exists
    if (m_filePath.has_parent_path()) {
        std::filesystem::create_directories(m_filePath.parent_path());
    }

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::string json = Json::writeString(builder, root);

    std::ofstream out(m_filePath);
    if (!out.is_open()) {
        std::cerr << "[session] failed to write " << m_filePath << std::endl;
        return;
    }
    out << json;
}

std::shared_ptr<Session> FileSessionStore::GetOrCreate(const SessionKey& key) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Check if session already exists with this key
    for (auto& entry : m_entries) {
        const auto& k = entry.session->Key();
        if (k.connector == key.connector &&
            k.conversation_id == key.conversation_id &&
            k.thread_id == key.thread_id) {
            return entry.session;
        }
    }

    // Create new session
    auto session = std::make_shared<Session>(m_nextId++, key);
    m_entries.push_back({session});
    SaveToDisk();
    return session;
}

std::shared_ptr<Session> FileSessionStore::GetById(SessionId id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& entry : m_entries) {
        if (entry.session->Id() == id) return entry.session;
    }
    return nullptr;
}

std::vector<std::shared_ptr<Session>> FileSessionStore::List() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::shared_ptr<Session>> result;
    for (auto& entry : m_entries) {
        result.push_back(entry.session);
    }
    return result;
}

bool FileSessionStore::DeleteById(SessionId id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->session->Id() == id) {
            m_entries.erase(it);
            SaveToDisk();
            return true;
        }
    }
    return false;
}

} // namespace animus::kernel
