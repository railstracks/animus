#include "animus_kernel/api/RegistryClient.h"

#include <json/json.h>

#include <sstream>

#include "animus_kernel/api/ManifestV1.h"

namespace animus::kernel::registry {

namespace {

std::string TrimTrailingSlash(const std::string& s) {
    std::string out = s;
    while (!out.empty() && out.back() == '/') out.pop_back();
    return out;
}

}  // namespace

HttpClient::Response FetchPackageManifest(HttpClient& http,
                                          const std::string& registryBase,
                                          const std::string& name,
                                          const std::string& version) {
    HttpClient::Request req;
    req.method = "GET";
    std::string url = TrimTrailingSlash(registryBase) + "/api/v1/packages/" + name;
    if (!version.empty()) url += "/versions/" + version;
    req.url = url;
    req.headers["Accept"] = "application/json";
    req.timeout_seconds = 10;
    return http.Execute(req);
}

RegistryInstallResult InstallFromRegistry(ApiPackageStore& store,
                                           HttpClient& http,
                                           const std::string& registryBase,
                                           const std::string& name,
                                           const std::string& version) {
    RegistryInstallResult result;

    // 1. Fetch
    HttpClient::Response resp = FetchPackageManifest(http, registryBase, name, version);
    if (resp.status_code == 0) {
        result.err = "registry unreachable: " + resp.error;
        return result;
    }
    if (resp.status_code == 404) {
        result.err = "package not found on registry";
        return result;
    }
    if (resp.status_code != 200) {
        result.err = "registry returned HTTP " + std::to_string(resp.status_code);
        return result;
    }

    // 2. Parse envelope {name, semantic_version, content_hash, manifest}
    Json::CharReaderBuilder rb;
    rb["collectComments"] = false;
    Json::Value envelope;
    std::string parseErr;
    std::istringstream ss(resp.body);
    if (!Json::parseFromStream(rb, ss, &envelope, &parseErr)) {
        result.err = "registry response is not valid JSON: " + parseErr;
        return result;
    }
    if (!envelope.isObject() || !envelope.isMember("manifest") ||
        !envelope.isMember("content_hash")) {
        result.err = "registry response missing manifest/content_hash";
        return result;
    }
    const std::string advertisedHash = envelope["content_hash"].asString();
    const std::string semanticVersion = envelope.get("semantic_version", "").asString();
    const std::string registryName = envelope.get("name", "").asString();
    if (registryName != name) {
        result.err = "registry name mismatch (asked '" + name + "', got '" + registryName + "')";
        return result;
    }

    // 3. Re-canonicalize + re-hash locally; must match the advertised hash.
    Json::StreamWriterBuilder wb;
    const std::string manifestJson = Json::writeString(wb, envelope["manifest"]);
    manifest_v1::Processed processed;
    std::string manifestErr;
    if (!manifest_v1::Process(manifestJson, processed, manifestErr)) {
        result.err = "manifest failed local validation: " + manifestErr;
        return result;
    }
    if (processed.content_hash != advertisedHash) {
        result.err = "CONTENT HASH MISMATCH — registry advertised " + advertisedHash +
                     " but manifest hashes to " + processed.content_hash +
                     " (refusing to install)";
        return result;
    }

    // 4. Install (disabled by design; user enables after configuring state).
    try {
        result.pkg = store.InstallFromManifest(manifestJson, TrimTrailingSlash(registryBase),
                                               semanticVersion.empty() ? processed.manifest.get("version", "").asString()
                                                                       : semanticVersion);
    } catch (const std::exception& e) {
        result.err = std::string("install failed: ") + e.what();
        return result;
    }

    result.ok = true;
    result.content_hash = processed.content_hash;
    result.semantic_version = semanticVersion;
    return result;
}

}  // namespace animus::kernel::registry
