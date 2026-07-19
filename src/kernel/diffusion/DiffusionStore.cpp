#include "animus_kernel/DiffusionStore.h"
#include "animus_kernel/SchemaHelpers.h"

#include <json/json.h>
#include <iostream>

namespace animus::kernel {

DiffusionStore::DiffusionStore(IDataStore* store)
    : m_store(store) {
    EnsureSchema();
}

void DiffusionStore::EnsureSchema() {
    if (!m_store) return;

    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS diffusion_providers (
            id TEXT PRIMARY KEY,
            type TEXT NOT NULL DEFAULT '',
            base_url TEXT NOT NULL DEFAULT '',
            api_key TEXT NOT NULL DEFAULT '',
            default_model TEXT NOT NULL DEFAULT '',
            default_aspect_ratio TEXT NOT NULL DEFAULT '',
            created_at INTEGER NOT NULL DEFAULT 0,
            updated_at INTEGER NOT NULL DEFAULT 0
        );
    )");
}

bool DiffusionStore::CreateProvider(const DiffusionProviderConfig& config) {
    if (!m_store) return false;
    std::lock_guard<std::mutex> lock(m_mutex);

    auto stmt = m_store->Prepare(
        "INSERT INTO diffusion_providers "
        "(id, type, base_url, api_key, default_model, default_aspect_ratio, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");

    if (!stmt) return false;
    stmt->BindText(1, config.id);
    stmt->BindText(2, config.type);
    stmt->BindText(3, config.base_url);
    stmt->BindText(4, config.api_key);
    stmt->BindText(5, config.default_model);
    stmt->BindText(6, config.default_aspect_ratio);
    stmt->BindInt64(7, config.created_at);
    stmt->BindInt64(8, config.updated_at);
    stmt->Step();
    return schema::DidAffectRows(m_store);
}

bool DiffusionStore::UpdateProvider(const DiffusionProviderConfig& config) {
    if (!m_store) return false;
    std::lock_guard<std::mutex> lock(m_mutex);

    auto stmt = m_store->Prepare(
        "UPDATE diffusion_providers SET "
        "type = ?, base_url = ?, api_key = ?, default_model = ?, "
        "default_aspect_ratio = ?, updated_at = ? WHERE id = ?");

    if (!stmt) return false;
    stmt->BindText(1, config.type);
    stmt->BindText(2, config.base_url);
    stmt->BindText(3, config.api_key);
    stmt->BindText(4, config.default_model);
    stmt->BindText(5, config.default_aspect_ratio);
    stmt->BindInt64(6, config.updated_at);
    stmt->BindText(7, config.id);
    stmt->Step();
    return schema::DidAffectRows(m_store);
}

bool DiffusionStore::DeleteProvider(const std::string& id) {
    if (!m_store) return false;
    std::lock_guard<std::mutex> lock(m_mutex);

    auto stmt = m_store->Prepare("DELETE FROM diffusion_providers WHERE id = ?");
    if (!stmt) return false;
    stmt->BindText(1, id);
    stmt->Step();
    return schema::DidAffectRows(m_store);
}

std::optional<DiffusionProviderConfig> DiffusionStore::GetProvider(const std::string& id) {
    if (!m_store) return std::nullopt;
    std::lock_guard<std::mutex> lock(m_mutex);

    auto stmt = m_store->Prepare(
        "SELECT id, type, base_url, api_key, default_model, default_aspect_ratio, created_at, updated_at "
        "FROM diffusion_providers WHERE id = ?");
    if (!stmt) return std::nullopt;
    stmt->BindText(1, id);
    if (!stmt->Step()) return std::nullopt;

    DiffusionProviderConfig config;
    config.id = stmt->ColumnText(0);
    config.type = stmt->ColumnText(1);
    config.base_url = stmt->ColumnText(2);
    config.api_key = stmt->ColumnText(3);
    config.default_model = stmt->ColumnText(4);
    config.default_aspect_ratio = stmt->ColumnText(5);
    config.created_at = stmt->ColumnInt64(6);
    config.updated_at = stmt->ColumnInt64(7);
    return config;
}

std::vector<DiffusionProviderConfig> DiffusionStore::ListProviders() {
    std::vector<DiffusionProviderConfig> result;
    if (!m_store) return result;
    std::lock_guard<std::mutex> lock(m_mutex);

    auto stmt = m_store->Prepare(
        "SELECT id, type, base_url, api_key, default_model, default_aspect_ratio, created_at, updated_at "
        "FROM diffusion_providers ORDER BY created_at ASC");
    if (!stmt) return result;

    while (stmt->Step()) {
        DiffusionProviderConfig config;
        config.id = stmt->ColumnText(0);
        config.type = stmt->ColumnText(1);
        config.base_url = stmt->ColumnText(2);
        config.api_key = stmt->ColumnText(3);
        config.default_model = stmt->ColumnText(4);
        config.default_aspect_ratio = stmt->ColumnText(5);
        config.created_at = stmt->ColumnInt64(6);
        config.updated_at = stmt->ColumnInt64(7);
        result.push_back(std::move(config));
    }
    return result;
}

} // namespace animus::kernel