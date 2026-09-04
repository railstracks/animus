// ============================================================================
// API Packages — admin-facing CRUD + debug execute
//
// GET    /api/v1/api/packages              — list (no manifest bodies)
// GET    /api/v1/api/packages/{id}         — detail (commands, connections, state masked)
// POST   /api/v1/api/packages              — install from manifest v1 JSON (installs disabled)
// PUT    /api/v1/api/packages/{id}/enabled — {enabled: bool}
// PUT    /api/v1/api/packages/{id}/state   — {state: {...}} full state write (secrets write-only)
// DELETE /api/v1/api/packages/{id}         — uninstall (cascades)
// POST   /api/v1/api/packages/{id}/execute — {command, args, agent_id} — debug surface
// ============================================================================

        // Helper lambda: package summary row
        auto pkgSummary = [](const ApiPackage& p) {
            Json::Value v(Json::objectValue);
            v["id"] = p.id;
            v["name"] = p.name;
            v["display_name"] = p.display_name;
            v["description"] = p.description;
            v["version"] = p.version;
            v["registry_source"] = p.registry_source;
            v["registry_version"] = p.registry_version;
            v["locally_modified"] = p.locally_modified;
            v["enabled"] = p.enabled;
            v["keywords"] = p.keywords;
            v["created_at"] = static_cast<Json::Int64>(p.created_at_unix_ms);
            v["updated_at"] = static_cast<Json::Int64>(p.updated_at_unix_ms);
            return v;
        };

        // Helper lambda: masked state for display (secret values never leave the box)
        auto maskedState = [](const ApiPackage& p) {
            Json::Value schema, state;
            Json::CharReaderBuilder rb;
            std::string errs;
            std::stringstream ss1(p.state_schema.empty() ? "{}" : p.state_schema);
            Json::parseFromStream(rb, ss1, &schema, &errs);
            std::stringstream ss2(p.state.empty() ? "{}" : p.state);
            Json::parseFromStream(rb, ss2, &state, &errs);
            if (state.isObject()) {
                for (const std::string& k : state.getMemberNames()) {
                    const Json::Value* def = schema.isObject() ? schema.find(k.data(), k.data() + k.size()) : nullptr;
                    bool secret = def && def->isObject() && (*def).get("secret", false).asBool();
                    bool hasValue = state[k] != Json::Value::null && (state[k].isString() ? !state[k].asString().empty() : true);
                    if (secret && hasValue) state[k] = "***";
                }
            }
            return state;
        };

        // GET /api/v1/api/packages
        drogon::app().registerHandler(
            "/api/v1/api/packages",
            [pkgSummary](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                (void)req;
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_apiPackageStore) {
                    SendJsonError(callback, "api package store not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }
                auto pkgs = server->m_apiPackageStore->ListPackages();
                Json::Value items(Json::arrayValue);
                for (const auto& p : pkgs) items.append(pkgSummary(p));
                Json::Value body(Json::objectValue);
                body["packages"] = items;
                body["count"] = static_cast<Json::Int>(pkgs.size());
                SendJsonResponse(callback, body);
            },
            {drogon::Get});

        // GET /api/v1/api/packages/{id}
        drogon::app().registerHandler(
            "/api/v1/api/packages/{id}",
            [pkgSummary, maskedState](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_apiPackageStore) {
                    SendJsonError(callback, "api package store not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }
                auto pkg = server->m_apiPackageStore->GetPackage(id);
                if (!pkg) { SendJsonError(callback, "package not found", drogon::k404NotFound); return; }

                Json::Value body = pkgSummary(*pkg);
                body["state_schema"] = pkg->state_schema.empty() ? "{}" : pkg->state_schema;
                body["state"] = maskedState(*pkg);

                Json::Value cmds(Json::arrayValue);
                for (const auto& c : server->m_apiPackageStore->ListCommands(id)) {
                    Json::Value cv(Json::objectValue);
                    cv["id"] = c.id;
                    cv["name"] = c.name;
                    cv["kind"] = c.kind;
                    cv["event"] = c.event;
                    cv["description"] = c.description;
                    cv["parameters"] = c.parameters;
                    cv["request"] = c.request;
                    cv["script"] = c.script;
                    cmds.append(cv);
                }
                body["commands"] = cmds;

                Json::Value conns(Json::arrayValue);
                for (const auto& c : server->m_apiPackageStore->ListConnections(id)) {
                    Json::Value cc(Json::objectValue);
                    cc["id"] = c.id;
                    cc["name"] = c.name;
                    cc["type"] = c.type;
                    cc["enabled"] = c.enabled;
                    cc["url_template"] = c.url_template;
                    cc["headers_template"] = c.headers_template;
                    cc["poll"] = c.poll;
                    cc["hooks"] = c.hooks;
                    conns.append(cc);
                }
                body["connections"] = conns;
                SendJsonResponse(callback, body);
            },
            {drogon::Get});

        // POST /api/v1/api/packages — install from manifest
        drogon::app().registerHandler(
            "/api/v1/api/packages",
            [pkgSummary](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_apiPackageStore) {
                    SendJsonError(callback, "api package store not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }
                Json::Value body;
                std::string parseError;
                if (!ParseJsonPayload(std::string(req->body()), &body, &parseError)) {
                    SendJsonError(callback, "invalid JSON body: " + parseError,
                        drogon::k400BadRequest);
                    return;
                }
                Json::StreamWriterBuilder wb;
                std::string manifest = Json::writeString(wb, body);
                try {
                    ApiPackage pkg = server->m_apiPackageStore->InstallFromManifest(manifest);
                    Json::Value out = pkgSummary(pkg);
                    out["installed_disabled"] = true;
                    SendJsonResponse(callback, out);
                } catch (const std::exception& e) {
                    SendJsonError(callback, std::string("install failed: ") + e.what(),
                        drogon::k400BadRequest);
                }
            },
            {drogon::Post});

        // PUT /api/v1/api/packages/{id}/enabled
        drogon::app().registerHandler(
            "/api/v1/api/packages/{id}/enabled",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_apiPackageStore) {
                    SendJsonError(callback, "api package store not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }
                Json::Value body;
                std::string parseError;
                if (!ParseJsonPayload(std::string(req->body()), &body, &parseError) ||
                    !body.isObject() || !body.isMember("enabled")) {
                    SendJsonError(callback, "body must be {\"enabled\": bool}",
                        drogon::k400BadRequest);
                    return;
                }
                if (!server->m_apiPackageStore->SetPackageEnabled(id, body["enabled"].asBool())) {
                    SendJsonError(callback, "package not found", drogon::k404NotFound);
                    return;
                }
                Json::Value out(Json::objectValue);
                out["ok"] = true;
                SendJsonResponse(callback, out);
            },
            {drogon::Put});

        // PUT /api/v1/api/packages/{id}/state
        drogon::app().registerHandler(
            "/api/v1/api/packages/{id}/state",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_apiPackageStore) {
                    SendJsonError(callback, "api package store not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }
                Json::Value body;
                std::string parseError;
                if (!ParseJsonPayload(std::string(req->body()), &body, &parseError) ||
                    !body.isObject() || !body["state"].isObject()) {
                    SendJsonError(callback, "body must be {\"state\": {...}}",
                        drogon::k400BadRequest);
                    return;
                }
                // write-only secrets: merge semantics — existing state overlaid by payload
                auto pkg = server->m_apiPackageStore->GetPackage(id);
                if (!pkg) { SendJsonError(callback, "package not found", drogon::k404NotFound); return; }

                Json::Value current;
                Json::CharReaderBuilder rb;
                std::string errs;
                std::stringstream ss(pkg->state.empty() ? "{}" : pkg->state);
                Json::parseFromStream(rb, ss, &current, &errs);

                Json::Value next = current;
                for (const std::string& k : body["state"].getMemberNames()) {
                    const Json::Value& v = body["state"][k];
                    if (v.isString() && v.asString() == "***") continue;  // masked echo = keep existing
                    next[k] = v;
                }
                Json::StreamWriterBuilder wb;
                if (!server->m_apiPackageStore->SetPackageState(id, Json::writeString(wb, next))) {
                    SendJsonError(callback, "state write rejected", drogon::k400BadRequest);
                    return;
                }
                Json::Value out(Json::objectValue);
                out["ok"] = true;
                SendJsonResponse(callback, out);
            },
            {drogon::Put});

        // DELETE /api/v1/api/packages/{id}
        drogon::app().registerHandler(
            "/api/v1/api/packages/{id}",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
                (void)req;
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_apiPackageStore) {
                    SendJsonError(callback, "api package store not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }
                if (!server->m_apiPackageStore->DeletePackage(id)) {
                    SendJsonError(callback, "package not found", drogon::k404NotFound);
                    return;
                }
                Json::Value out(Json::objectValue);
                out["ok"] = true;
                SendJsonResponse(callback, out);
            },
            {drogon::Delete});

        // POST /api/v1/api/packages/{id}/execute — debug surface
        drogon::app().registerHandler(
            "/api/v1/api/packages/{id}/execute",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_apiPackageStore || !server->m_apiRuntime) {
                    SendJsonError(callback, "api subsystem not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }
                auto pkg = server->m_apiPackageStore->GetPackage(id);
                if (!pkg) { SendJsonError(callback, "package not found", drogon::k404NotFound); return; }

                Json::Value body;
                std::string parseError;
                if (!ParseJsonPayload(std::string(req->body()), &body, &parseError) ||
                    !body.isObject() || !body.isMember("command")) {
                    SendJsonError(callback, "body must be {\"command\": str, \"args\": {}, \"agent_id\": str}",
                        drogon::k400BadRequest);
                    return;
                }
                std::string agentId = body.get("agent_id", "default").asString();
                Json::Value args = body.get("args", Json::Value(Json::objectValue));
                Json::Value result = server->m_apiRuntime->ExecuteAction(
                    pkg->name, body["command"].asString(), agentId, args);
                SendJsonResponse(callback, result);
            },
            {drogon::Post});
