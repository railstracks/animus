#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "animus_kernel/IDataStore.h"

namespace animus::kernel {

// ============================================================================
// API Package Framework — persistence layer (#26 data layer, build order b)
//
// Spec: docs/api/SCHEMAS.md. The DDL there is descriptive; this store follows
// the codebase dialect policy: UUID PKs become generated TEXT ids, JSONB
// columns become TEXT holding compact JSON, TIMESTAMPTZ becomes unix-ms.
// Field names and semantics are the spec; JSON validation beyond shape
// linting lives at the sandbox/transport layer (c/d), not here.
//
// Cascade deletes are explicit (children first, in one transaction) — the
// store does not rely on FK pragmas, matching ChannelContextStore precedent.
// ============================================================================

struct ApiPackage {
    std::string id;                  // generated uuid-ish hex
    std::string name;                // lowercase slug [a-z0-9-], unique
    std::string display_name;        // optional
    std::string description;         // required non-empty
    std::string keywords;            // JSON array of strings, default "[]"
    std::string version;             // semver string, local version
    std::string registry_source;     // registry listing id; "" = locally created
    std::string registry_version;    // semver last downloaded; "" if none
    bool locally_modified{false};
    bool enabled{false};             // install != enable; enabling is explicit
    int64_t dispatch_cooldown_ms{10000};
    int64_t files_quota_mb{256};     // ctx.fs quota (D12)
    std::string state_schema;        // JSON object: key -> {type, default?, secret?}
    std::string state;               // JSON object: current values
    int64_t created_at_unix_ms{0};
    int64_t updated_at_unix_ms{0};
};

struct ApiPackageCommand {
    std::string id;
    std::string package_id;
    std::string name;                // lowercase, spaces allowed ("portfolio list")
    std::string kind;                // "action" | "hook"
    std::string event;               // hooks only: on_connect|on_disconnect|on_message|on_error
    std::string description;         // agent-facing affordance
    std::string parameters;          // JSON object (actions only), default "{}"
    std::string request;             // JSON object or "" (script-only actions)
    std::string script;              // Lua; defines run(ctx)
};

struct ApiPackageConnection {
    std::string id;
    std::string package_id;
    std::string name;                // lowercase slug
    std::string type;                // "websocket" | "longpoll"
    bool enabled{true};
    std::string url_template;        // interpolated from state
    std::string headers_template;    // JSON object, default "{}"
    std::string poll;                // JSON object or "" (longpoll only)
    std::string hooks;               // JSON object: event -> command name
};

class ApiPackageStore {
public:
    explicit ApiPackageStore(IDataStore* store);

    void EnsureSchema();

    // --- Packages -----------------------------------------------------------
    // Throws std::runtime_error on duplicate name.
    ApiPackage CreatePackage(const ApiPackage& pkg);

    std::optional<ApiPackage> GetPackage(const std::string& id) const;
    std::optional<ApiPackage> GetPackageByName(const std::string& name) const;
    std::vector<ApiPackage> ListPackages() const;

    // Full authored-field update (everything except id/name/enabled/state/
    // timestamps). Returns false if the package does not exist.
    bool UpdatePackageMeta(const ApiPackage& pkg);

    bool SetPackageEnabled(const std::string& id, bool enabled);
    // Persists a full state object (validation is the sandbox layer's job;
    // the store only checks it parses as a JSON object). Returns false if
    // the package is missing or the payload is not a JSON object.
    bool SetPackageState(const std::string& id, const std::string& stateJson);

    // Deletes the package and all commands/connections/agent rows. Returns
    // false if the package does not exist.
    bool DeletePackage(const std::string& id);

    // --- Commands -----------------------------------------------------------
    ApiPackageCommand AddCommand(const ApiPackageCommand& cmd);   // throws on dup (package,name)
    std::optional<ApiPackageCommand> GetCommand(const std::string& packageId,
                                                const std::string& name) const;
    std::vector<ApiPackageCommand> ListCommands(const std::string& packageId) const;
    bool DeleteCommand(const std::string& id);
    // Atomically replaces the command set of a package (manifest install /
    // upgrade path). Returns the inserted count.
    int ReplaceCommands(const std::string& packageId, const std::vector<ApiPackageCommand>& cmds);

    // --- Connections ----------------------------------------------------------
    ApiPackageConnection AddConnection(const ApiPackageConnection& conn);  // throws on dup (package,name)
    std::optional<ApiPackageConnection> GetConnection(const std::string& packageId,
                                                      const std::string& name) const;
    std::vector<ApiPackageConnection> ListConnections(const std::string& packageId) const;
    bool DeleteConnection(const std::string& id);
    int ReplaceConnections(const std::string& packageId,
                           const std::vector<ApiPackageConnection>& conns);

    // --- Per-agent enablement overlay (D13) -----------------------------------
    // Absent row -> inherit package default. Effective enablement for agent A
    // = package.enabled AND (no row OR row.enabled).
    void SetAgentEnablement(const std::string& packageId, const std::string& agentId, bool enabled);
    std::optional<bool> GetAgentEnablement(const std::string& packageId,
                                           const std::string& agentId) const;  // nullopt = no row
    bool ClearAgentEnablement(const std::string& packageId, const std::string& agentId);
    bool EffectiveEnabled(const std::string& packageId, const std::string& agentId) const;

    // --- Manifest install ------------------------------------------------------
    // Parses + lints a manifest v1 (docs/api/SCHEMAS.md "Manifest v1"),
    // creates the package with all commands and connections in one
    // transaction. Installs disabled (enabling is a separate explicit act).
    // Lua static checks are NOT part of this lint (that is the sandbox layer).
    // Throws std::runtime_error with a descriptive message on any violation.
    ApiPackage InstallFromManifest(const std::string& manifestJson);

    // Validates a package name (slug + reserved words). Throws with a
    // descriptive message. Used by CreatePackage and the api tool.
    static void ValidateName(const std::string& name);

private:
    static int64_t NowUnixMs();
    std::string GenerateId() const;
    bool TouchPackage(const std::string& id);  // bumps updated_at; false = missing

    IDataStore* m_store;
};

} // namespace animus::kernel
