// ============================================================================
// Lua Script API — admin-facing CRUD
//
// GET    /api/v1/lua/scripts?agent_id={agentId}  — list scripts
// GET    /api/v1/lua/scripts/{id}                 — get script by id
// POST   /api/v1/lua/scripts                      — create script
// PUT    /api/v1/lua/scripts/{id}                 — update script
// DELETE /api/v1/lua/scripts/{id}                 — delete script
// ============================================================================

        // GET /api/v1/lua/scripts?agent_id={agentId}
        drogon::app().registerHandler(
            "/api/v1/lua/scripts",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_scriptStore) {
                    SendJsonError(callback, "script store not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }

                std::string agentId = req->getParameter("agent_id");
                if (agentId.empty()) agentId = "default";

                auto scripts = server->m_scriptStore->List(agentId);

                Json::Value items(Json::arrayValue);
                for (const auto& s : scripts) {
                    Json::Value v(Json::objectValue);
                    v["id"] = s.id;
                    v["agent_id"] = s.agent_id;
                    v["name"] = s.name;
                    v["description"] = s.description;
                    v["enabled"] = s.enabled;
                    v["created_at"] = s.created_at;
                    v["updated_at"] = s.updated_at;
                    // Don't include source in list — too large
                    items.append(v);
                }

                Json::Value body(Json::objectValue);
                body["scripts"] = items;
                body["count"] = static_cast<Json::Int>(scripts.size());
                SendJsonResponse(callback, body);
            },
            {drogon::Get});

        // GET /api/v1/lua/scripts/{id}
        drogon::app().registerHandler(
            "/api/v1/lua/scripts/{id}",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_scriptStore) {
                    SendJsonError(callback, "script store not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }

                auto script = server->m_scriptStore->Get(id);
                if (!script) {
                    SendJsonError(callback, "script not found: " + id,
                        drogon::k404NotFound);
                    return;
                }

                Json::Value v(Json::objectValue);
                v["id"] = script->id;
                v["agent_id"] = script->agent_id;
                v["name"] = script->name;
                v["source"] = script->source;
                v["description"] = script->description;
                v["enabled"] = script->enabled;
                v["created_at"] = script->created_at;
                v["updated_at"] = script->updated_at;

                SendJsonResponse(callback, v);
            },
            {drogon::Get});

        // POST /api/v1/lua/scripts — create script
        drogon::app().registerHandler(
            "/api/v1/lua/scripts",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_scriptStore) {
                    SendJsonError(callback, "script store not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }

                Json::Value body;
                std::string parseError;
                if (!ParseJsonPayload(std::string(req->body()), &body, &parseError)) {
                    SendJsonError(callback, "invalid JSON: " + parseError,
                        drogon::k400BadRequest);
                    return;
                }

                ScriptDescriptor desc;
                desc.agent_id = body.get("agent_id", "default").asString();
                desc.name = body.get("name", "").asString();
                desc.source = body.get("source", "").asString();
                desc.description = body.get("description", "").asString();
                desc.enabled = body.get("enabled", true).asBool();

                if (desc.name.empty()) {
                    SendJsonError(callback, "name is required",
                        drogon::k400BadRequest);
                    return;
                }
                if (desc.source.empty()) {
                    SendJsonError(callback, "source is required",
                        drogon::k400BadRequest);
                    return;
                }

                // Check for duplicate name
                auto existing = server->m_scriptStore->GetByName(desc.agent_id, desc.name);
                if (existing) {
                    SendJsonError(callback, "script name already exists for this agent: " + desc.name,
                        drogon::k409Conflict);
                    return;
                }

                std::string error;
                std::string id = server->m_scriptStore->CreateAndReturnId(desc, &error);
                if (id.empty()) {
                    SendJsonError(callback, "failed to create script: " + error,
                        drogon::k500InternalServerError);
                    return;
                }

                Json::Value response(Json::objectValue);
                response["id"] = id;
                response["name"] = desc.name;
                response["agent_id"] = desc.agent_id;
                response["created_at"] = desc.created_at;
                SendJsonResponse(callback, response, drogon::k201Created);
            },
            {drogon::Post});

        // PUT /api/v1/lua/scripts/{id} — update script
        drogon::app().registerHandler(
            "/api/v1/lua/scripts/{id}",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_scriptStore) {
                    SendJsonError(callback, "script store not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }

                auto existing = server->m_scriptStore->Get(id);
                if (!existing) {
                    SendJsonError(callback, "script not found: " + id,
                        drogon::k404NotFound);
                    return;
                }

                Json::Value body;
                std::string parseError;
                if (!ParseJsonPayload(std::string(req->body()), &body, &parseError)) {
                    SendJsonError(callback, "invalid JSON: " + parseError,
                        drogon::k400BadRequest);
                    return;
                }

                // Update fields present in the request
                if (body.isMember("name")) existing->name = body["name"].asString();
                if (body.isMember("source")) existing->source = body["source"].asString();
                if (body.isMember("description")) existing->description = body["description"].asString();
                if (body.isMember("enabled")) existing->enabled = body["enabled"].asBool();

                std::string error;
                if (!server->m_scriptStore->Update(*existing, &error)) {
                    SendJsonError(callback, "failed to update script: " + error,
                        drogon::k500InternalServerError);
                    return;
                }

                Json::Value response(Json::objectValue);
                response["id"] = existing->id;
                response["name"] = existing->name;
                response["updated_at"] = existing->updated_at;
                SendJsonResponse(callback, response);
            },
            {drogon::Put});

        // DELETE /api/v1/lua/scripts/{id} — delete script
        drogon::app().registerHandler(
            "/api/v1/lua/scripts/{id}",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_scriptStore) {
                    SendJsonError(callback, "script store not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }

                auto existing = server->m_scriptStore->Get(id);
                if (!existing) {
                    SendJsonError(callback, "script not found: " + id,
                        drogon::k404NotFound);
                    return;
                }

                std::string error;
                if (!server->m_scriptStore->Delete(id, &error)) {
                    SendJsonError(callback, "failed to delete script: " + error,
                        drogon::k500InternalServerError);
                    return;
                }

                Json::Value response(Json::objectValue);
                response["deleted"] = true;
                response["id"] = id;
                SendJsonResponse(callback, response);
            },
            {drogon::Delete});

        // GET /api/v1/lua/fs?agent_id={agentId} — list filesystem scripts
        drogon::app().registerHandler(
            "/api/v1/lua/fs",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                FilesystemScriptLoader loader(server->m_luaScriptDir);

                std::string agentId = req->getParameter("agent_id");

                std::vector<DiscoveredScript> scripts;
                if (agentId.empty()) {
                    // List all — need agent IDs, but we don't have AgentStore here.
                    // Return shared only when no agent specified.
                    scripts = loader.DiscoverShared();
                } else {
                    scripts = loader.DiscoverForAgent(agentId);
                }

                Json::Value items(Json::arrayValue);
                for (const auto& s : scripts) {
                    Json::Value v(Json::objectValue);
                    v["name"] = s.name;
                    v["description"] = s.description;
                    v["agent_id"] = s.agent_id;
                    v["enabled"] = s.enabled;
                    v["path"] = s.path.string();
                    v["source"] = "filesystem";
                    items.append(v);
                }

                Json::Value body(Json::objectValue);
                body["scripts"] = items;
                body["count"] = static_cast<Json::Int>(scripts.size());
                body["root_dir"] = server->m_luaScriptDir;
                SendJsonResponse(callback, body);
            },
            {drogon::Get});

        // POST /api/v1/lua/export-to-filesystem — export DB scripts to filesystem
        drogon::app().registerHandler(
            "/api/v1/lua/export-to-filesystem",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_scriptStore) {
                    SendJsonError(callback, "script store not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }

                // Parse optional agent_id from body or query
                std::string agentId = req->getParameter("agent_id");

                std::vector<ScriptDescriptor> dbScripts;
                if (agentId.empty()) {
                    // Export all — but ScriptStore::List requires an agent_id.
                    // Export for "default" if no agent specified.
                    dbScripts = server->m_scriptStore->List("default");
                } else {
                    dbScripts = server->m_scriptStore->List(agentId);
                }

                FilesystemScriptLoader loader(server->m_luaScriptDir);

                std::vector<FilesystemScriptLoader::ExportEntry> entries;
                for (const auto& s : dbScripts) {
                    entries.push_back({s.agent_id, s.name, s.source});
                }

                std::string error;
                int exported = loader.Export(entries, &error);

                Json::Value response(Json::objectValue);
                response["exported"] = exported;
                response["total"] = static_cast<Json::Int>(dbScripts.size());
                response["root_dir"] = server->m_luaScriptDir;
                if (!error.empty()) {
                    response["warnings"] = error;
                }
                SendJsonResponse(callback, response);
            },
            {drogon::Post});