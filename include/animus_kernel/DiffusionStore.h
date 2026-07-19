#pragma once

#include "animus_kernel/IDataStore.h"
#include "animus_kernel/DiffusionProvider.h"

#include <string>
#include <vector>
#include <optional>
#include <mutex>

namespace animus::kernel {

// ============================================================================
// DiffusionStore — DB persistence for diffusion provider configs
// ============================================================================

class DiffusionStore {
public:
    explicit DiffusionStore(IDataStore* store);

    // CRUD
    bool CreateProvider(const DiffusionProviderConfig& config);
    bool UpdateProvider(const DiffusionProviderConfig& config);
    bool DeleteProvider(const std::string& id);
    std::optional<DiffusionProviderConfig> GetProvider(const std::string& id);
    std::vector<DiffusionProviderConfig> ListProviders();

private:
    void EnsureSchema();

    IDataStore* m_store;
    std::mutex m_mutex;
};

} // namespace animus::kernel