#pragma once
// ============================================================================
// RegistryClient — install API packages from an Animus Registry
// (https://animus-registry.steadyfort.com or any compatible server).
//
// Wire format (registry API v1):
//   GET {base}/api/v1/packages/{name}                  -> latest
//   GET {base}/api/v1/packages/{name}/versions/{v}     -> pinned (int or semver)
//   Response: { name, version, semantic_version, content_hash, manifest: {...} }
//
// Security: the fetched manifest is re-canonicalized and re-hashed locally
// (manifest_v1::Process) and MUST match the registry's content_hash before
// install. Hash mismatch = reject loudly. A registry can serve a manifest,
// but not a different one than the hash it advertises.
// ============================================================================

#include <string>

#include "animus_kernel/ApiPackageStore.h"
#include "animus_kernel/tools/HttpClient.h"

namespace animus::kernel::registry {

struct RegistryInstallResult {
    bool ok{false};
    ApiPackage pkg;
    std::string err;
    std::string content_hash;   // verified hash (on success)
    std::string semantic_version;
};

// Fetch + verify + install. Blocking (bounded by the HTTP timeout, ~10s).
RegistryInstallResult InstallFromRegistry(ApiPackageStore& store,
                                           HttpClient& http,
                                           const std::string& registryBase,
                                           const std::string& name,
                                           const std::string& version = "" /* latest */);

// Fetch only (list/detail probes, UI "what's available"): returns raw body.
HttpClient::Response FetchPackageManifest(HttpClient& http,
                                          const std::string& registryBase,
                                          const std::string& name,
                                          const std::string& version = "");

}  // namespace animus::kernel::registry
