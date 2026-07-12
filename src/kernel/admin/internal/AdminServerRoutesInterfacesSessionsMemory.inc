        // ============================================================================
        // Interfaces (CRUD + enable/disable)
        // ====================================================================================

        drogon::app().registerHandler(
            "/api/v1/interfaces",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                Json::Value out(Json::objectValue);
                Json::Value list(Json::arrayValue);
                for (const auto& iface : server->m_interfaceManager.ListInterfaces()) {
                    list.append(BuildInterfaceJson(iface, false));
                }
                out["interfaces"] = list;
                out["total"] = static_cast<Json::UInt64>(list.size());
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        drogon::app().registerHandler(
            "/api/v1/interfaces",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                const auto body = req->getJsonObject();
                if (!body || !body->isObject()) {
                    Json::Value out;
                    AddBadRequestError("request body must be a JSON object", &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }
                if (!body->isMember("name") || !(*body)["name"].isString()
                    || (*body)["name"].asString().empty()) {
                    Json::Value out;
                    AddBadRequestError("name is required", &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }
                if (!body->isMember("type") || !(*body)["type"].isString()
                    || (*body)["type"].asString().empty()) {
                    Json::Value out;
                    AddBadRequestError("type is required", &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }
                const std::string name = (*body)["name"].asString();
                const std::string type = (*body)["type"].asString();
                if (!server->m_interfaceManager.IsValidToken(name)) {
                    Json::Value out;
                    AddBadRequestError(
                        "name contains invalid characters (allowed: alphanumeric, '-', '_', '.', ':')",
                        &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }
                if (!server->m_interfaceManager.IsValidToken(type)) {
                    Json::Value out;
                    AddBadRequestError(
                        "type contains invalid characters (allowed: alphanumeric, '-', '_', '.', ':')",
                        &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                Json::Value config(Json::objectValue);
                if (body->isMember("config")) {
                    if (!(*body)["config"].isObject()) {
                        Json::Value out;
                        AddBadRequestError("config must be an object", &out);
                        auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                        resp->setStatusCode(drogon::k400BadRequest);
                        callback(resp);
                        return;
                    }
                    config = (*body)["config"];
                }
                std::string validationError;
                if (!server->m_interfaceManager.ValidateConfigForType(type, config, &validationError)) {
                    Json::Value out;
                    out["error"] = validationError;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                if (body->isMember("enabled") && !(*body)["enabled"].isBool()) {
                    Json::Value out;
                    AddBadRequestError("enabled must be a boolean", &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }
                const bool enabled = !body->isMember("enabled") || (*body)["enabled"].asBool();
                std::string moduleId = body->isMember("module_id") && (*body)["module_id"].isString()
                    ? (*body)["module_id"].asString()
                    : ("connector." + type);
                if (moduleId.empty() || !server->m_interfaceManager.IsValidToken(moduleId)) {
                    Json::Value out;
                    AddBadRequestError(
                        "module_id contains invalid characters (allowed: alphanumeric, '-', '_', '.', ':')",
                        &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                InterfaceState created{};
                created.name = name;
                created.type = type;
                created.moduleId = moduleId;
                created.enabled = enabled;
                created.lastEventUnixMs = NowUnixMs();
                created.config = config;
                std::string createError;
                if (!server->m_interfaceManager.CreateInterface(created, &createError)) {
                    Json::Value out;
                    out["error"] = createError.empty() ? "failed to create interface" : createError;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    if (createError == "interface already exists") {
                        resp->setStatusCode(drogon::k409Conflict);
                    } else {
                        resp->setStatusCode(drogon::k500InternalServerError);
                    }
                    callback(resp);
                    return;
                }

                std::string persistError;
                if (!server->SaveInterfacesToDisk(&persistError)) {
                    Json::Value out;
                    out["error"] = persistError;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }
                server->SyncIrcInterfaces();

                const auto stored = server->m_interfaceManager.GetInterface(name);
                callback(drogon::HttpResponse::newHttpJsonResponse(
                    BuildInterfaceJson(stored.value_or(created), true)));
            },
            {drogon::Post});

        drogon::app().registerHandler(
            "/api/v1/interfaces/{1}",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& interfaceName) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                Json::Value out(Json::objectValue);
                const auto iface = server->m_interfaceManager.GetInterface(interfaceName);
                if (!iface) {
                    out["error"] = "interface not found";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }
                out = BuildInterfaceJson(*iface, true);
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        drogon::app().registerHandler(
            "/api/v1/interfaces/{1}",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& interfaceName) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                const auto body = req->getJsonObject();
                if (!body || !body->isObject()) {
                    Json::Value out;
                    AddBadRequestError("request body must be a JSON object", &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }
                if (!body->isMember("config") || !(*body)["config"].isObject()) {
                    Json::Value out;
                    AddBadRequestError("config must be provided as an object", &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                Json::Value response(Json::objectValue);
                Json::Value nextConfig(Json::objectValue);
                std::string normalizeError;
                if (!server->m_interfaceManager.NormalizeUpdatedConfig(
                        interfaceName, (*body)["config"], &nextConfig, &normalizeError)) {
                    response["error"] = normalizeError;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
                    resp->setStatusCode(
                        normalizeError == "interface not found"
                            ? drogon::k404NotFound
                            : drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                InterfaceState updated;
                std::string updateError;
                if (!server->m_interfaceManager.UpdateInterfaceConfig(
                        interfaceName,
                        nextConfig,
                        NowUnixMs(),
                        &updated,
                        &updateError)) {
                    response["error"] = updateError.empty() ? "interface not found" : updateError;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }
                response = BuildInterfaceJson(updated, true);

                std::string persistError;
                if (!server->SaveInterfacesToDisk(&persistError)) {
                    Json::Value out;
                    out["error"] = persistError;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }
                server->SyncIrcInterfaces();
                callback(drogon::HttpResponse::newHttpJsonResponse(response));
            },
            {drogon::Patch});

        drogon::app().registerHandler(
            "/api/v1/interfaces/{1}/enable",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& interfaceName) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                Json::Value response(Json::objectValue);
                InterfaceState updated;
                std::string setError;
                if (!server->m_interfaceManager.SetInterfaceEnabled(
                        interfaceName,
                        true,
                        NowUnixMs(),
                        &updated,
                        &setError)) {
                    response["error"] = setError.empty() ? "interface not found" : setError;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }
                response["name"] = updated.name;
                response["enabled"] = updated.enabled;
                response["connected"] = updated.connected;

                std::string persistError;
                if (!server->SaveInterfacesToDisk(&persistError)) {
                    Json::Value out;
                    out["error"] = persistError;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }
                server->SyncIrcInterfaces();
                callback(drogon::HttpResponse::newHttpJsonResponse(response));
            },
            {drogon::Post});

        drogon::app().registerHandler(
            "/api/v1/interfaces/{1}/disable",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& interfaceName) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                Json::Value response(Json::objectValue);
                InterfaceState updated;
                std::string setError;
                if (!server->m_interfaceManager.SetInterfaceEnabled(
                        interfaceName,
                        false,
                        NowUnixMs(),
                        &updated,
                        &setError)) {
                    response["error"] = setError.empty() ? "interface not found" : setError;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }
                response["name"] = updated.name;
                response["enabled"] = updated.enabled;
                response["connected"] = updated.connected;

                std::string persistError;
                if (!server->SaveInterfacesToDisk(&persistError)) {
                    Json::Value out;
                    out["error"] = persistError;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }
                server->SyncIrcInterfaces();
                callback(drogon::HttpResponse::newHttpJsonResponse(response));
            },
            {drogon::Post});

        drogon::app().registerHandler(
            "/api/v1/interfaces/{1}",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& interfaceName) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                std::string deleteError;
                if (!server->m_interfaceManager.DeleteInterface(interfaceName, &deleteError)) {
                    Json::Value out;
                    out["error"] = deleteError.empty() ? "interface not found" : deleteError;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }
                server->SyncIrcInterfaces();

                std::string persistError;
                if (!server->SaveInterfacesToDisk(&persistError)) {
                    Json::Value out;
                    out["error"] = persistError;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                Json::Value out(Json::objectValue);
                out["deleted"] = true;
                out["name"] = interfaceName;
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Delete});

        // ============================================================================
        // Sessions (list, get, history, delete, context)
        // ====================================================================================

        drogon::app().registerHandler(
            "/api/v1/sessions",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_sessions) {
                    Json::Value out;
                    out["error"] = "session manager not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                std::vector<std::shared_ptr<Session>> sessions = server->m_sessions->ListSessions();
                std::sort(
                    sessions.begin(),
                    sessions.end(),
                    [](const std::shared_ptr<Session>& a, const std::shared_ptr<Session>& b) {
                        return a->LastActiveUnixMs() > b->LastActiveUnixMs();
                    });

                Json::Value out(Json::objectValue);
                Json::Value list(Json::arrayValue);
                for (const auto& session : sessions) {
                    list.append(BuildSessionSummaryJson(*session));
                }
                out["sessions"] = list;
                out["total"] = static_cast<Json::UInt64>(sessions.size());
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        drogon::app().registerHandler(
            "/api/v1/sessions/{1}",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& sessionIdText) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_sessions) {
                    Json::Value out;
                    out["error"] = "session manager not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                SessionId sessionId = 0;
                if (!ParseSessionId(sessionIdText, &sessionId)) {
                    Json::Value out;
                    AddBadRequestError("invalid session id", &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                auto session = server->m_sessions->GetById(sessionId);
                if (!session) {
                    Json::Value out;
                    out["error"] = "session not found";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }

                Json::Value out = BuildSessionSummaryJson(*session);
                out["summary"] = session->Summary();
                const SessionTurn* compaction = session->GetCompactionSummary();
                if (compaction) {
                    out["compaction_summary"] = BuildSessionTurnJson(*compaction);
                } else {
                    out["compaction_summary"] = Json::nullValue;
                }

                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        // ============================================================================
        // Memory — legacy in-memory API
        // ====================================================================================

        drogon::app().registerHandler(
            "/api/v1/sessions/{1}/history",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& sessionIdText) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_sessions) {
                    Json::Value out;
                    out["error"] = "session manager not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                SessionId sessionId = 0;
                if (!ParseSessionId(sessionIdText, &sessionId)) {
                    Json::Value out;
                    AddBadRequestError("invalid session id", &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                auto session = server->m_sessions->GetById(sessionId);
                if (!session) {
                    Json::Value out;
                    out["error"] = "session not found";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }

                const std::uint32_t page = ParsePaginationParam(req, "page", 1, 1, 1000000);
                const std::uint32_t limit = ParsePaginationParam(req, "limit", 50, 1, 200);
                const std::vector<SessionTurn>& turns = session->Turns();
                const std::size_t total = turns.size();
                const std::size_t offset = static_cast<std::size_t>(page - 1) * limit;

                Json::Value items(Json::arrayValue);
                if (offset < total) {
                    const std::size_t maxItems = std::min<std::size_t>(limit, total - offset);
                    for (std::size_t i = 0; i < maxItems; ++i) {
                        const std::size_t reverseIndex = total - 1 - (offset + i);
                        items.append(BuildSessionTurnJson(turns[reverseIndex]));
                    }
                }

                Json::Value out(Json::objectValue);
                out["session_id"] = static_cast<Json::UInt64>(sessionId);
                out["page"] = page;
                out["limit"] = limit;
                out["order"] = "desc";
                out["total"] = static_cast<Json::UInt64>(total);
                const std::size_t totalPages = (total == 0)
                    ? 0
                    : ((total + static_cast<std::size_t>(limit) - 1) / static_cast<std::size_t>(limit));
                out["total_pages"] = static_cast<Json::UInt64>(totalPages);
                out["items"] = items;
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        drogon::app().registerHandler(
            "/api/v1/sessions/{1}",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& sessionIdText) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_sessions) {
                    Json::Value out;
                    out["error"] = "session manager not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                SessionId sessionId = 0;
                if (!ParseSessionId(sessionIdText, &sessionId)) {
                    Json::Value out;
                    AddBadRequestError("invalid session id", &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                if (!server->m_sessions->DeleteById(sessionId)) {
                    Json::Value out;
                    out["error"] = "session not found";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }

                Json::Value out(Json::objectValue);
                out["status"] = "deleted";
                out["session_id"] = static_cast<Json::UInt64>(sessionId);
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Delete});

        drogon::app().registerHandler(
            "/api/v1/sessions/{1}/context",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& sessionIdText) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_sessions) {
                    Json::Value out;
                    out["error"] = "session manager not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                SessionId sessionId = 0;
                if (!ParseSessionId(sessionIdText, &sessionId)) {
                    Json::Value out;
                    AddBadRequestError("invalid session id", &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                auto session = server->m_sessions->GetById(sessionId);
                if (!session) {
                    Json::Value out;
                    out["error"] = "session not found";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }

                KernelConfig::AgentRuntimeConfig agentSnapshot;
                {
                    std::lock_guard<std::mutex> lock(server->m_agentMutex);
                    agentSnapshot = server->m_agentConfig;
                }

                Json::Value out(Json::objectValue);
                out["session_id"] = static_cast<Json::UInt64>(sessionId);
                out["memory_layers_loaded"] = Json::Value(Json::arrayValue);
                out["tools_available"] = Json::Value(Json::arrayValue);
                out["token_budget_state"]["configured_token_budget_per_prompt"] =
                    agentSnapshot.budget.tokenBudgetPerPrompt;
                out["token_budget_state"]["consumed_tokens"] = 0;
                out["token_budget_state"]["remaining_tokens"] =
                    agentSnapshot.budget.tokenBudgetPerPrompt;
                out["note"] =
                    "ChainRunner/memory/tool runtime not yet implemented; current context is empty.";

                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        // ============================================================================
        // Token Estimate API
        // ====================================================================================

        // GET /api/v1/sessions/{sessionId}/token-estimate?draft=<text>
        //
        // Dry-run prompt composition: assembles the full system prompt +
        // session turns + compaction summary + optional draft message,
        // returns estimated token counts without hitting the LLM.
        drogon::app().registerHandler(
            "/api/v1/sessions/{1}/token-estimate",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& sessionIdText) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_sessions) {
                    SendJsonError(callback, "session manager not available", drogon::k503ServiceUnavailable);
                    return;
                }

                SessionId sessionId = 0;
                if (!ParseSessionId(sessionIdText, &sessionId)) {
                    Json::Value out;
                    AddBadRequestError("invalid session id", &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                auto session = server->m_sessions->GetById(sessionId);
                if (!session) {
                    SendJsonError(callback, "session not found", drogon::k404NotFound);
                    return;
                }

                // Optional draft message from query param
                std::string draftMessage = req->getParameter("draft");

                // Resolve agent
                Agent agent;
                if (server->m_agentStore && !session->AgentId().empty()) {
                    auto loaded = server->m_agentStore->GetById(session->AgentId());
                    if (loaded) agent = *loaded;
                }

                // Resolve provider + model (same logic as ChatSessionService)
                std::string providerId = session->ProviderId();
                if (providerId.empty() && server->m_agentStore && !session->AgentId().empty()) {
                    auto ag = server->m_agentStore->GetById(session->AgentId());
                    if (ag && !ag->default_provider.empty()) providerId = ag->default_provider;
                }
                if (providerId.empty()) {
                    providerId = server->GetDefaultProviderId();
                }

                std::string model;
                std::size_t contextWindow = 128000;
                bool hasProviderState = false;
                ProviderState providerState;

                if (const auto provider = server->GetProvider(providerId)) {
                    providerState = *provider;
                    hasProviderState = true;
                    if (model.empty()) model = providerState.defaultModel;
                }
                if (server->m_agentStore && !session->AgentId().empty()) {
                    auto ag = server->m_agentStore->GetById(session->AgentId());
                    if (model.empty() && ag && !ag->default_model.empty()) {
                        model = ag->default_model;
                    }
                }
                if (hasProviderState) {
                    contextWindow = AdminServer::ResolveProviderContextWindow(
                        providerState, model,
                        static_cast<std::uint32_t>(contextWindow));
                }
                // Clamp to agent's context_window if set (acts as upper bound)
                if (server->m_agentStore && !session->AgentId().empty()) {
                    auto ag2 = server->m_agentStore->GetById(session->AgentId());
                    if (ag2 && ag2->context_window > 0) {
                        contextWindow = std::min(contextWindow, static_cast<std::size_t>(ag2->context_window));
                    }
                }
                if (model.empty()) model = "gpt-4o";

                // Assemble system prompt from context registry (cached for admin preview)
                std::string systemPrompt;
                if (server->m_contextRegistry) {
                    SessionAccess sessionAccess(session, SessionAccessMode::ReadOnly);
                    auto blocks = server->m_contextRegistry->AssembleCached(agent, sessionAccess);
                    for (const auto& block : blocks) {
                        systemPrompt += block.content + "\n";
                    }
                }

                // Load compaction summary from DB if not already in session
                const SessionTurn* compactionSummary = session->GetCompactionSummary();
                if (!compactionSummary && server->m_compactionService) {
                    auto latest = server->m_compactionService->GetLatestCompaction(session->Id());
                    if (latest) {
                        // Build a temporary summary turn for the estimator
                        static thread_local SessionTurn tempSummary;
                        tempSummary.role = "system";
                        tempSummary.content = latest->summary;
                        tempSummary.is_summary = true;
                        compactionSummary = &tempSummary;
                    }
                }

                // Build dry-run prompt assembly
                PromptAssembler assembler;
                const float compactionThreshold = 0.8f;
                auto assembly = assembler.Build(
                    session->Turns(),
                    compactionSummary,
                    draftMessage,
                    systemPrompt,
                    model,
                    contextWindow,
                    compactionThreshold);

                Json::Value body(Json::objectValue);
                body["session_id"] = static_cast<Json::UInt64>(sessionId);
                body["estimated_tokens"] = static_cast<Json::UInt64>(assembly.total_tokens);
                body["context_window"] = static_cast<Json::UInt64>(contextWindow);
                body["compaction_threshold"] = compactionThreshold;
                body["compaction_trigger_tokens"] = static_cast<Json::UInt64>(
                    static_cast<std::size_t>(static_cast<float>(contextWindow) * compactionThreshold));
                body["needs_compaction"] = assembly.needs_compaction;

                Json::Value breakdown(Json::objectValue);
                breakdown["system_prompt"] = static_cast<Json::UInt64>(assembly.system_prompt_tokens);
                breakdown["compaction_summary"] = static_cast<Json::UInt64>(assembly.compaction_summary_tokens);
                breakdown["session_turns"] = static_cast<Json::UInt64>(assembly.session_turns_tokens);
                breakdown["draft_message"] = static_cast<Json::UInt64>(assembly.draft_message_tokens);
                body["breakdown"] = breakdown;

                body["model"] = model;
                body["provider_id"] = providerId;

                callback(drogon::HttpResponse::newHttpJsonResponse(body));
            },
            {drogon::Get});

        // ============================================================================
        // Session Compaction API
        // ====================================================================================

        // GET /api/v1/sessions/{sessionId}/compactions — list all compactions
        drogon::app().registerHandler(
            "/api/v1/sessions/{1}/compactions",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& sessionIdText) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_compactionService) {
                    Json::Value out;
                    out["error"] = "compaction service not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }
                SessionId sessionId = 0;
                if (!ParseSessionId(sessionIdText, &sessionId)) {
                    Json::Value out;
                    AddBadRequestError("invalid session id", &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }
                auto compactions = server->m_compactionService->GetCompactions(sessionId);
                Json::Value arr(Json::arrayValue);
                for (const auto& c : compactions) {
                    Json::Value entry;
                    entry["id"] = static_cast<Json::UInt64>(c.id);
                    entry["session_id"] = static_cast<Json::UInt64>(c.session_id);
                    entry["message_id"] = static_cast<Json::UInt64>(c.message_id);
                    entry["summary"] = c.summary;
                    entry["token_count"] = static_cast<Json::UInt64>(c.token_count);
                    entry["model"] = c.model;
                    entry["created_at_unix_ms"] = static_cast<Json::UInt64>(c.created_at_unix_ms);
                    arr.append(entry);
                }
                Json::Value out;
                out["compactions"] = arr;
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        // GET /api/v1/sessions/{sessionId}/compactions/latest — most recent compaction
        drogon::app().registerHandler(
            "/api/v1/sessions/{1}/compactions/latest",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& sessionIdText) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_compactionService) {
                    Json::Value out;
                    out["error"] = "compaction service not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }
                SessionId sessionId = 0;
                if (!ParseSessionId(sessionIdText, &sessionId)) {
                    Json::Value out;
                    AddBadRequestError("invalid session id", &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }
                auto latest = server->m_compactionService->GetLatestCompaction(sessionId);
                if (latest) {
                    Json::Value out;
                    out["id"] = static_cast<Json::UInt64>(latest->id);
                    out["session_id"] = static_cast<Json::UInt64>(latest->session_id);
                    out["message_id"] = static_cast<Json::UInt64>(latest->message_id);
                    out["summary"] = latest->summary;
                    out["token_count"] = static_cast<Json::UInt64>(latest->token_count);
                    out["model"] = latest->model;
                    out["created_at_unix_ms"] = static_cast<Json::UInt64>(latest->created_at_unix_ms);
                    callback(drogon::HttpResponse::newHttpJsonResponse(out));
                } else {
                    Json::Value out;
                    out["compaction_summary"] = Json::nullValue;
                    callback(drogon::HttpResponse::newHttpJsonResponse(out));
                }
            },
            {drogon::Get});

        // POST /api/v1/sessions/{sessionId}/compact — manually trigger compaction
        drogon::app().registerHandler(
            "/api/v1/sessions/{1}/compact",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& sessionIdText) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_compactionService || !server->m_sessions) {
                    Json::Value out;
                    out["error"] = "compaction service not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }
                SessionId sessionId = 0;
                if (!ParseSessionId(sessionIdText, &sessionId)) {
                    Json::Value out;
                    AddBadRequestError("invalid session id", &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                KernelConfig::AgentRuntimeConfig agentConfig;
                {
                    std::lock_guard<std::mutex> lock(server->m_agentMutex);
                    agentConfig = server->m_agentConfig;
                }
                std::size_t contextWindow = 128000;  // Default context window

                auto result = server->m_compactionService->CompactNow(
                    sessionId,
                    "",  // system prompt — will use default compaction prompt
                    "",  // provider — will use default
                    "",  // model — will use default
                    contextWindow);

                Json::Value out;
                if (result.success) {
                    out["compacted"] = true;
                    out["turns_compacted"] = static_cast<Json::UInt64>(result.turns_compacted);
                    out["summary_tokens"] = static_cast<Json::UInt64>(result.entry.token_count);
                    out["message_id"] = static_cast<Json::UInt64>(result.entry.message_id);
                } else {
                    out["compacted"] = false;
                    out["error"] = result.error;
                }
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Post});

        drogon::app().registerHandler(
            "/api/v1/memory/layers",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                std::string agentId = req->getParameter("agent_id");
                const auto result = server->m_memoryManager.SqlListLayers(server->m_memoryStore, agentId);
                auto resp = drogon::HttpResponse::newHttpJsonResponse(result.body);
                resp->setStatusCode(static_cast<drogon::HttpStatusCode>(result.httpStatusCode));
                callback(resp);
            },
            {drogon::Get});

        // GET /api/v1/memory/templates/{kind}?locale=<locale>
        // kind: review | intake | ontology | gallivanting
        drogon::app().registerHandler(
            "/api/v1/memory/templates/{1}",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& kind) {
                std::string directory;
                if (kind == "review") {
                    directory = "consolidation-review";
                } else if (kind == "intake") {
                    directory = "consolidation-intake";
                } else if (kind == "ontology") {
                    directory = "consolidation-ontology";
                } else if (kind == "gallivanting") {
                    directory = "gallivanting";
                } else {
                    Json::Value out(Json::objectValue);
                    out["error"] = "template kind must be 'review', 'intake', 'ontology', or 'gallivanting'";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                std::string locale = req->getParameter("locale");
                if (locale.empty()) locale = "en";

                static const std::regex kLocaleSafe("^[A-Za-z0-9_-]+$");
                if (!std::regex_match(locale, kLocaleSafe)) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "invalid locale";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                std::vector<std::string> localeCandidates;
                localeCandidates.push_back(locale);
                const std::size_t delimPos = locale.find_first_of("-_");
                if (delimPos != std::string::npos && delimPos > 0) {
                    const std::string base = locale.substr(0, delimPos);
                    if (base != locale) localeCandidates.push_back(base);
                }
                if (locale != "en") localeCandidates.push_back("en");

                std::string templateContent;
                std::string localeUsed;
                bool found = false;

                // Try embedded templates first, then filesystem.
                for (const auto& candidate : localeCandidates) {
                    const std::string relPath = directory + "/" + candidate + ".md";
                    EmbeddedTemplate embedded;
                    if (HasEmbeddedTemplates() && GetEmbeddedTemplate(relPath, &embedded)) {
                        templateContent = std::string(
                            reinterpret_cast<const char*>(embedded.data),
                            embedded.size);
                        localeUsed = candidate;
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    std::filesystem::path foundPath;
                    const auto cwd = std::filesystem::current_path();
                    const std::vector<std::filesystem::path> templateRoots{
                        cwd / "templates",
                        cwd / ".." / "templates",
                    };
                    for (const auto& candidate : localeCandidates) {
                        for (const auto& root : templateRoots) {
                            const auto path = root / directory / (candidate + ".md");
                            if (std::filesystem::exists(path) && std::filesystem::is_regular_file(path)) {
                                foundPath = path;
                                localeUsed = candidate;
                                break;
                            }
                        }
                        if (!foundPath.empty()) {
                            break;
                        }
                    }

                    if (foundPath.empty()) {
                        Json::Value out(Json::objectValue);
                        out["error"] = "template not found";
                        auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                        resp->setStatusCode(drogon::k404NotFound);
                        callback(resp);
                        return;
                    }

                    std::ifstream in(foundPath);
                    if (!in.is_open()) {
                        Json::Value out(Json::objectValue);
                        out["error"] = "failed to read template";
                        auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                        resp->setStatusCode(drogon::k500InternalServerError);
                        callback(resp);
                        return;
                    }
                    std::ostringstream ss;
                    ss << in.rdbuf();
                    templateContent = ss.str();
                    found = true;
                }

                Json::Value out(Json::objectValue);
                out["content"] = templateContent;
                out["locale"] = localeUsed;
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        drogon::app().registerHandler(
            "/api/v1/memory/layers/{1}/observations",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& layerName) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                const std::uint32_t page = ParsePaginationParam(req, "page", 1, 1, 1000000);
                const std::uint32_t limit = ParsePaginationParam(req, "limit", 50, 1, 200);
                const std::string sort = req->getParameter("sort");
                const std::string order = req->getParameter("order");
                const bool ascending = (order == "asc");

                // New contract: numeric layer ID uses SQLite-backed memory store.
                // Legacy contract: non-numeric layer key falls back to in-memory manager.
                MemoryManager::OperationResult result;
                bool isNumeric = !layerName.empty();
                for (char ch : layerName) {
                    if (ch < '0' || ch > '9') {
                        isNumeric = false;
                        break;
                    }
                }
                if (isNumeric) {
                    result = server->m_memoryManager.SqlListLayerObservations(
                        server->m_memoryStore, layerName, page, limit);
                } else {
                    result = server->m_memoryManager.BuildLayerObservationsResponse(
                        layerName, sort, ascending, page, limit);
                }
                auto resp = drogon::HttpResponse::newHttpJsonResponse(result.body);
                resp->setStatusCode(static_cast<drogon::HttpStatusCode>(result.httpStatusCode));
                callback(resp);
            },
            {drogon::Get});

        // GET /api/v1/ontology/categories
        drogon::app().registerHandler(
            "/api/v1/ontology/categories",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_ontologyStore) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "ontology store not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                const std::string agentId = req->getParameter("agent_id");

                const std::array<ontology::RootCategory, 7> categories{
                    ontology::RootCategory::Persons,
                    ontology::RootCategory::Concepts,
                    ontology::RootCategory::Procedures,
                    ontology::RootCategory::Events,
                    ontology::RootCategory::Locations,
                    ontology::RootCategory::Organizations,
                    ontology::RootCategory::Projects
                };

                Json::Value out(Json::arrayValue);
                for (const auto category : categories) {
                    const auto entities = server->m_ontologyStore->ListEntities(category, agentId);
                    Json::Value item(Json::objectValue);
                    item["category"] = ontology::OntologyStore::RootCategoryToString(category);
                    item["count"] = static_cast<Json::UInt64>(entities.size());
                    out.append(item);
                }
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        // GET /api/v1/ontology/entities
        drogon::app().registerHandler(
            "/api/v1/ontology/entities",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_ontologyStore) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "ontology store not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                const std::string categoryParam = req->getParameter("category");
                const std::string parentIdParam = req->getParameter("parent_id");
                const std::string agentIdParam = req->getParameter("agent_id");

                std::vector<ontology::OntologyEntity> entities;
                if (!parentIdParam.empty()) {
                    try {
                        entities = server->m_ontologyStore->ListChildren(
                            static_cast<int64_t>(std::stoll(parentIdParam)), agentIdParam);
                    } catch (...) {
                        Json::Value out(Json::objectValue);
                        out["error"] = "invalid parent_id";
                        auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                        resp->setStatusCode(drogon::k400BadRequest);
                        callback(resp);
                        return;
                    }
                } else if (!categoryParam.empty()) {
                    auto category = ontology::OntologyStore::RootCategoryFromString(categoryParam);
                    if (!category.has_value()) {
                        Json::Value out(Json::objectValue);
                        out["error"] = "invalid category";
                        auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                        resp->setStatusCode(drogon::k400BadRequest);
                        callback(resp);
                        return;
                    }
                    entities = server->m_ontologyStore->ListEntities(*category, agentIdParam);
                } else {
                    entities = server->m_ontologyStore->ListEntities(std::nullopt, agentIdParam);
                }

                Json::Value out(Json::arrayValue);
                for (const auto& entity : entities) {
                    Json::Value item(Json::objectValue);
                    item["id"] = static_cast<Json::UInt64>(entity.id);
                    if (entity.parent_id.has_value()) {
                        item["parent_id"] = static_cast<Json::UInt64>(*entity.parent_id);
                    } else {
                        item["parent_id"] = Json::nullValue;
                    }
                    item["root_category"] = ontology::OntologyStore::RootCategoryToString(
                        entity.root_category);
                    item["name"] = entity.name;
                    item["full_path"] = entity.full_path;
                    item["sort_order"] = entity.sort_order;
                    item["agent_id"] = entity.agent_id;
                    item["created_at_unix_ms"] =
                        static_cast<Json::UInt64>(entity.created_at_unix_ms);
                    item["updated_at_unix_ms"] =
                        static_cast<Json::UInt64>(entity.updated_at_unix_ms);
                    out.append(item);
                }
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        // GET /api/v1/ontology/entities/{id}
        drogon::app().registerHandler(
            "/api/v1/ontology/entities/{1}",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& idStr) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_ontologyStore) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "ontology store not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                int64_t id = 0;
                try {
                    id = static_cast<int64_t>(std::stoll(idStr));
                } catch (...) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "invalid id";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                auto entity = server->m_ontologyStore->GetEntity(id);
                if (!entity.has_value()) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "not found";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }

                Json::Value out(Json::objectValue);
                out["id"] = static_cast<Json::UInt64>(entity->id);
                if (entity->parent_id.has_value()) {
                    out["parent_id"] = static_cast<Json::UInt64>(*entity->parent_id);
                } else {
                    out["parent_id"] = Json::nullValue;
                }
                out["root_category"] = ontology::OntologyStore::RootCategoryToString(
                    entity->root_category);
                out["name"] = entity->name;
                out["full_path"] = entity->full_path;
                out["sort_order"] = entity->sort_order;
                out["agent_id"] = entity->agent_id;
                out["created_at_unix_ms"] = static_cast<Json::UInt64>(entity->created_at_unix_ms);
                out["updated_at_unix_ms"] = static_cast<Json::UInt64>(entity->updated_at_unix_ms);

                Json::Value properties(Json::arrayValue);
                for (const auto& prop : server->m_ontologyStore->ListProperties(entity->id)) {
                    Json::Value p(Json::objectValue);
                    p["id"] = static_cast<Json::UInt64>(prop.id);
                    p["entity_id"] = static_cast<Json::UInt64>(prop.entity_id);
                    p["key"] = prop.key;
                    p["value"] = prop.value;
                    p["value_type"] = ontology::OntologyStore::PropertyTypeToString(
                        prop.value_type);
                    p["memory_state"] = ontology::OntologyStore::MemoryStateToString(
                        prop.memory_state);
                    if (prop.linked_observation_id.has_value()) {
                        p["linked_observation_id"] =
                            static_cast<Json::UInt64>(*prop.linked_observation_id);
                    } else {
                        p["linked_observation_id"] = Json::nullValue;
                    }
                    p["created_at_unix_ms"] = static_cast<Json::UInt64>(prop.created_at_unix_ms);
                    p["updated_at_unix_ms"] = static_cast<Json::UInt64>(prop.updated_at_unix_ms);
                    properties.append(p);
                }
                out["properties"] = properties;
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        // GET /api/v1/ontology/entities/{id}/properties
        drogon::app().registerHandler(
            "/api/v1/ontology/entities/{1}/properties",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& idStr) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_ontologyStore) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "ontology store not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                int64_t id = 0;
                try {
                    id = static_cast<int64_t>(std::stoll(idStr));
                } catch (...) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "invalid id";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                Json::Value out(Json::arrayValue);
                for (const auto& prop : server->m_ontologyStore->ListProperties(id)) {
                    Json::Value p(Json::objectValue);
                    p["id"] = static_cast<Json::UInt64>(prop.id);
                    p["entity_id"] = static_cast<Json::UInt64>(prop.entity_id);
                    p["key"] = prop.key;
                    p["value"] = prop.value;
                    p["value_type"] = ontology::OntologyStore::PropertyTypeToString(
                        prop.value_type);
                    p["memory_state"] = ontology::OntologyStore::MemoryStateToString(
                        prop.memory_state);
                    if (prop.linked_observation_id.has_value()) {
                        p["linked_observation_id"] =
                            static_cast<Json::UInt64>(*prop.linked_observation_id);
                    } else {
                        p["linked_observation_id"] = Json::nullValue;
                    }
                    p["created_at_unix_ms"] = static_cast<Json::UInt64>(prop.created_at_unix_ms);
                    p["updated_at_unix_ms"] = static_cast<Json::UInt64>(prop.updated_at_unix_ms);
                    out.append(p);
                }
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        // GET /api/v1/ontology/entities/{id}/tree
        drogon::app().registerHandler(
            "/api/v1/ontology/entities/{1}/tree",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& idStr) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_ontologyStore) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "ontology store not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                int64_t id = 0;
                try {
                    id = static_cast<int64_t>(std::stoll(idStr));
                } catch (...) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "invalid id";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                int depth = 3;
                const std::string depthParam = req->getParameter("depth");
                if (!depthParam.empty()) {
                    try {
                        depth = std::max(1, std::min(32, std::stoi(depthParam)));
                    } catch (...) {
                        depth = 3;
                    }
                }

                auto entity = server->m_ontologyStore->GetEntity(id);
                if (!entity.has_value()) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "not found";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }

                std::function<Json::Value(const ontology::OntologyEntity&, int)> buildTree =
                    [&](const ontology::OntologyEntity& node, int remainingDepth) {
                        Json::Value out(Json::objectValue);
                        out["id"] = static_cast<Json::UInt64>(node.id);
                        if (node.parent_id.has_value()) {
                            out["parent_id"] = static_cast<Json::UInt64>(*node.parent_id);
                        } else {
                            out["parent_id"] = Json::nullValue;
                        }
                        out["root_category"] =
                            ontology::OntologyStore::RootCategoryToString(node.root_category);
                        out["name"] = node.name;
                        out["full_path"] = node.full_path;
                        out["sort_order"] = node.sort_order;
                        out["created_at_unix_ms"] =
                            static_cast<Json::UInt64>(node.created_at_unix_ms);
                        out["updated_at_unix_ms"] =
                            static_cast<Json::UInt64>(node.updated_at_unix_ms);
                        Json::Value children(Json::arrayValue);
                        if (remainingDepth > 0) {
                            for (const auto& child : server->m_ontologyStore->ListChildren(node.id)) {
                                children.append(buildTree(child, remainingDepth - 1));
                            }
                        }
                        out["children"] = children;
                        return out;
                    };

                callback(drogon::HttpResponse::newHttpJsonResponse(buildTree(*entity, depth)));
            },
            {drogon::Get});

        // GET /api/v1/ontology/mutations
        drogon::app().registerHandler(
            "/api/v1/ontology/mutations",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_ontologyStore) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "ontology store not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                int64_t sinceMs = 0;
                int64_t limit = 100;
                const std::string sinceParam = req->getParameter("since_unix_ms");
                const std::string limitParam = req->getParameter("limit");
                if (!sinceParam.empty()) {
                    try {
                        sinceMs = static_cast<int64_t>(std::stoll(sinceParam));
                    } catch (...) {
                        sinceMs = 0;
                    }
                }
                if (!limitParam.empty()) {
                    try {
                        limit = std::max<int64_t>(1, std::min<int64_t>(1000, std::stoll(limitParam)));
                    } catch (...) {
                        limit = 100;
                    }
                }

                Json::Value out(Json::arrayValue);
                for (const auto& m : server->m_ontologyStore->QueryMutations(sinceMs, limit)) {
                    Json::Value item(Json::objectValue);
                    item["id"] = static_cast<Json::UInt64>(m.id);
                    item["mutation_type"] = m.mutation_type;
                    item["target_type"] = m.target_type;
                    item["target_id"] = static_cast<Json::UInt64>(m.target_id);
                    item["previous_state"] = m.previous_state;
                    item["new_state"] = m.new_state;
                    item["motivation"] = m.motivation;
                    item["unix_ms"] = static_cast<Json::UInt64>(m.unix_ms);
                    out.append(item);
                }
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        // GET /api/v1/memory-files
        drogon::app().registerHandler(
            "/api/v1/memory-files",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_memoryFileStore) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "memory file store not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                const std::string typeParam = req->getParameter("type");
                const std::string limitParam = req->getParameter("limit");
                const std::string offsetParam = req->getParameter("offset");
                const std::string agentIdParam = req->getParameter("agent_id");

                std::optional<memory::MemoryFileType> type = std::nullopt;
                if (!typeParam.empty()) {
                    auto parsed = memory::MemoryFileStore::FileTypeFromString(typeParam);
                    if (!parsed.has_value()) {
                        Json::Value out(Json::objectValue);
                        out["error"] = "invalid type";
                        auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                        resp->setStatusCode(drogon::k400BadRequest);
                        callback(resp);
                        return;
                    }
                    type = *parsed;
                }

                int64_t limit = 100;
                int64_t offset = 0;
                if (!limitParam.empty()) {
                    try {
                        limit = std::max<int64_t>(1, std::min<int64_t>(1000, std::stoll(limitParam)));
                    } catch (...) {}
                }
                if (!offsetParam.empty()) {
                    try {
                        offset = std::max<int64_t>(0, std::stoll(offsetParam));
                    } catch (...) {}
                }

                int64_t filterAgentId = -1; // -1 = no filter
                if (!agentIdParam.empty()) {
                    try { filterAgentId = std::stoll(agentIdParam); } catch (...) {}
                }

                Json::Value out(Json::arrayValue);
                for (const auto& file : server->m_memoryFileStore->ListFiles(type, limit, offset)) {
                    if (filterAgentId >= 0 && file.agent_id != filterAgentId && file.agent_id != 0) continue;
                    Json::Value item(Json::objectValue);
                    item["id"] = static_cast<Json::UInt64>(file.id);
                    item["source_path"] = file.source_path;
                    item["file_type"] = memory::MemoryFileStore::FileTypeToString(file.file_type);
                    item["content_mutable"] = file.content_mutable;
                    item["agent_id"] = static_cast<Json::Int64>(file.agent_id);
                    item["superseded"] = file.superseded;
                    item["status"] = static_cast<int>(file.status);
                    item["created_at_unix_ms"] = static_cast<Json::UInt64>(file.created_at_unix_ms);
                    item["imported_at_unix_ms"] = static_cast<Json::UInt64>(file.imported_at_unix_ms);
                    out.append(item);
                }
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        // GET /api/v1/memory-files/stats
        drogon::app().registerHandler(
            "/api/v1/memory-files/stats",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_memoryFileStore) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "memory file store not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                const std::string agentIdParam = req->getParameter("agent_id");
                int64_t filterAgentId = -1;
                if (!agentIdParam.empty()) {
                    try { filterAgentId = std::stoll(agentIdParam); } catch (...) {}
                }

                const std::array<memory::MemoryFileType, 6> fileTypes{
                    memory::MemoryFileType::ExpandedMemory,
                    memory::MemoryFileType::SessionLog,
                    memory::MemoryFileType::DailyNote,
                    memory::MemoryFileType::BootstrapFile,
                    memory::MemoryFileType::Journal,
                    memory::MemoryFileType::ExternalDoc
                };

                Json::Value out(Json::objectValue);
                Json::Value byType(Json::objectValue);
                int64_t total = 0;
                for (const auto type : fileTypes) {
                    int64_t count;
                    if (filterAgentId >= 0) {
                        // When filtering by agent, count from ListFiles and filter
                        auto files = server->m_memoryFileStore->ListFiles(type, 10000, 0);
                        count = 0;
                        for (const auto& f : files) {
                            if (f.agent_id == filterAgentId || f.agent_id == 0) count++;
                        }
                    } else {
                        count = server->m_memoryFileStore->CountByType(type);
                    }
                    total += count;
                    byType[memory::MemoryFileStore::FileTypeToString(type)] =
                        static_cast<Json::Int64>(count);
                }
                out["total"] = static_cast<Json::Int64>(total);
                out["by_type"] = byType;
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        // GET /api/v1/memory-files/{id}
        drogon::app().registerHandler(
            "/api/v1/memory-files/{1}",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& idStr) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_memoryFileStore) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "memory file store not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                int64_t id = 0;
                try {
                    id = std::stoll(idStr);
                } catch (...) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "invalid id";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                auto file = server->m_memoryFileStore->GetFile(id);
                if (!file) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "not found";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }

                Json::Value out(Json::objectValue);
                out["id"] = static_cast<Json::UInt64>(file->id);
                out["source_path"] = file->source_path;
                out["file_type"] = memory::MemoryFileStore::FileTypeToString(file->file_type);
                out["content"] = file->content;
                out["content_mutable"] = file->content_mutable;
                out["agent_id"] = static_cast<Json::Int64>(file->agent_id);
                out["superseded"] = file->superseded;
                out["created_at_unix_ms"] = static_cast<Json::UInt64>(file->created_at_unix_ms);
                out["imported_at_unix_ms"] = static_cast<Json::UInt64>(file->imported_at_unix_ms);
                out["status"] = static_cast<int>(file->status);
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        // POST /api/v1/memory-files/{id}/process — mark file for processing
        drogon::app().registerHandler(
            "/api/v1/memory-files/{1}/process",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& fileIdText) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_memoryFileStore) {
                    SendJsonError(callback, "memory file store not available", drogon::k503ServiceUnavailable);
                    return;
                }
                int64_t fileId = 0;
                try { fileId = std::stoll(fileIdText); } catch (...) {}
                if (fileId <= 0) {
                    SendJsonError(callback, "invalid file id", drogon::k400BadRequest);
                    return;
                }

                auto file = server->m_memoryFileStore->GetFile(fileId);
                if (!file) {
                    SendJsonError(callback, "file not found", drogon::k404NotFound);
                    return;
                }

                // Set status to Unprocessed so next consolidation cycle picks it up
                file->status = memory::MemoryFileStatus::Unprocessed;
                if (!server->m_memoryFileStore->UpdateFile(*file)) {
                    SendJsonError(callback, "failed to update file", drogon::k500InternalServerError);
                    return;
                }

                Json::Value out(Json::objectValue);
                out["id"] = static_cast<Json::UInt64>(fileId);
                out["status"] = "queued_for_processing";
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Post});

        // POST /api/v1/memory-files/import
        drogon::app().registerHandler(
            "/api/v1/memory-files/import",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_memoryFileStore) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "memory file store not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                const auto body = req->getJsonObject();
                if (!body || !body->isObject()) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "request body must be a JSON object";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                if (!body->isMember("source_path") || !(*body)["source_path"].isString()
                    || !body->isMember("content") || !(*body)["content"].isString()) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "source_path and content are required";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                std::optional<memory::MemoryFileType> fileType = memory::MemoryFileType::ExternalDoc;
                if (body->isMember("file_type")) {
                    if ((*body)["file_type"].isString()) {
                        fileType = memory::MemoryFileStore::FileTypeFromString(
                            (*body)["file_type"].asString());
                    } else if ((*body)["file_type"].isInt() || (*body)["file_type"].isUInt()
                               || (*body)["file_type"].isInt64() || (*body)["file_type"].isUInt64()) {
                        const int64_t raw = (*body)["file_type"].asInt64();
                        if (raw >= 0 && raw <= 5) {
                            fileType = static_cast<memory::MemoryFileType>(raw);
                        } else {
                            fileType = std::nullopt;
                        }
                    }
                }
                if (!fileType.has_value()) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "invalid file_type";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                memory::MemoryFile file;
                file.source_path = (*body)["source_path"].asString();
                file.file_type = *fileType;
                file.content = (*body)["content"].asString();
                file.content_mutable = !body->isMember("content_mutable")
                    || (*body)["content_mutable"].asBool();
                file.agent_id = body->isMember("agent_id")
                    ? static_cast<int64_t>((*body)["agent_id"].asInt64())
                    : 0;
                file.superseded = body->isMember("superseded") && (*body)["superseded"].asBool();
                file.created_at_unix_ms = body->isMember("created_at_unix_ms")
                    ? static_cast<int64_t>((*body)["created_at_unix_ms"].asInt64())
                    : 0;
                file.imported_at_unix_ms = body->isMember("imported_at_unix_ms")
                    ? static_cast<int64_t>((*body)["imported_at_unix_ms"].asInt64())
                    : 0;

                auto created = server->m_memoryFileStore->CreateFile(file);
                if (created.id <= 0) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "failed to import memory file";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                Json::Value out(Json::objectValue);
                out["id"] = static_cast<Json::UInt64>(created.id);
                out["source_path"] = created.source_path;
                out["file_type"] = memory::MemoryFileStore::FileTypeToString(created.file_type);
                out["content_mutable"] = created.content_mutable;
                out["agent_id"] = static_cast<Json::Int64>(created.agent_id);
                out["superseded"] = created.superseded;
                out["created_at_unix_ms"] = static_cast<Json::UInt64>(created.created_at_unix_ms);
                out["imported_at_unix_ms"] = static_cast<Json::UInt64>(created.imported_at_unix_ms);
                auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                resp->setStatusCode(drogon::k201Created);
                callback(resp);
            },
            {drogon::Post});

        // POST /api/v1/memory-files/import-batch
        drogon::app().registerHandler(
            "/api/v1/memory-files/import-batch",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_memoryFileStore) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "memory file store not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                const auto body = req->getJsonObject();
                if (!body || !body->isObject() || !body->isMember("files")
                    || !(*body)["files"].isArray()) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "request body must include files[]";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                Json::Value imported(Json::arrayValue);
                for (const auto& item : (*body)["files"]) {
                    if (!item.isObject()) continue;
                    if (!item.isMember("source_path") || !item["source_path"].isString()) continue;
                    if (!item.isMember("content") || !item["content"].isString()) continue;

                    auto type = item.isMember("file_type") && item["file_type"].isString()
                        ? memory::MemoryFileStore::FileTypeFromString(item["file_type"].asString())
                        : std::optional<memory::MemoryFileType>(memory::MemoryFileType::ExternalDoc);
                    if (!type) continue;

                    memory::MemoryFile file;
                    file.source_path = item["source_path"].asString();
                    file.file_type = *type;
                    file.content = item["content"].asString();
                    file.content_mutable = !item.isMember("content_mutable")
                        || item["content_mutable"].asBool();
                    file.agent_id = item.isMember("agent_id") ? item["agent_id"].asInt64() : 0;
                    file.superseded = item.isMember("superseded") && item["superseded"].asBool();
                    file.created_at_unix_ms = item.isMember("created_at_unix_ms")
                        ? item["created_at_unix_ms"].asInt64()
                        : 0;
                    file.imported_at_unix_ms = item.isMember("imported_at_unix_ms")
                        ? item["imported_at_unix_ms"].asInt64()
                        : 0;

                    auto created = server->m_memoryFileStore->CreateFile(file);
                    if (created.id <= 0) continue;
                    Json::Value outItem(Json::objectValue);
                    outItem["id"] = static_cast<Json::UInt64>(created.id);
                    outItem["source_path"] = created.source_path;
                    outItem["file_type"] = memory::MemoryFileStore::FileTypeToString(created.file_type);
                    outItem["content_mutable"] = created.content_mutable;
                    imported.append(outItem);
                }

                Json::Value out(Json::objectValue);
                out["imported"] = imported;
                out["count"] = static_cast<Json::UInt64>(imported.size());
                auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                resp->setStatusCode(drogon::k201Created);
                callback(resp);
            },
            {drogon::Post});

        // PATCH /api/v1/memory-files/{id}/metadata
        // Updates mutable metadata and content when content_mutable allows it.
        drogon::app().registerHandler(
            "/api/v1/memory-files/{1}/metadata",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& idStr) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_memoryFileStore) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "memory file store not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                int64_t id = 0;
                try {
                    id = std::stoll(idStr);
                } catch (...) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "invalid id";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                const auto body = req->getJsonObject();
                if (!body || !body->isObject()) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "request body must be a JSON object";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                auto existing = server->m_memoryFileStore->GetFile(id);
                if (!existing) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "not found";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }
                const bool wasContentMutable = existing->content_mutable;
                const std::string previousContent = existing->content;

                if (body->isMember("source_path") && (*body)["source_path"].isString()) {
                    existing->source_path = (*body)["source_path"].asString();
                }
                if (body->isMember("file_type")) {
                    if ((*body)["file_type"].isString()) {
                        auto parsed = memory::MemoryFileStore::FileTypeFromString(
                            (*body)["file_type"].asString());
                        if (!parsed.has_value()) {
                            Json::Value out(Json::objectValue);
                            out["error"] = "invalid file_type";
                            auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                            resp->setStatusCode(drogon::k400BadRequest);
                            callback(resp);
                            return;
                        }
                        existing->file_type = *parsed;
                    }
                }
                if (body->isMember("agent_id")) {
                    existing->agent_id = static_cast<int64_t>((*body)["agent_id"].asInt64());
                }
                if (body->isMember("content_mutable")) {
                    existing->content_mutable = (*body)["content_mutable"].asBool();
                }
                if (body->isMember("superseded")) {
                    existing->superseded = (*body)["superseded"].asBool();
                }
                if (body->isMember("created_at_unix_ms")) {
                    existing->created_at_unix_ms =
                        static_cast<int64_t>((*body)["created_at_unix_ms"].asInt64());
                }
                if (body->isMember("content") && (*body)["content"].isString()) {
                    existing->content = (*body)["content"].asString();
                }

                if (existing->content != previousContent
                    && !wasContentMutable
                    && !existing->content_mutable) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "content is immutable for this file";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                if (!server->m_memoryFileStore->UpdateFile(*existing)) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "update failed";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                Json::Value out(Json::objectValue);
                out["status"] = "ok";
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Patch});

        // DELETE /api/v1/memory-files/{id}
        drogon::app().registerHandler(
            "/api/v1/memory-files/{1}",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& idStr) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_memoryFileStore) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "memory file store not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                int64_t id = 0;
                try {
                    id = std::stoll(idStr);
                } catch (...) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "invalid id";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                if (!server->m_memoryFileStore->DeleteFile(id)) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "not found";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }
                Json::Value out(Json::objectValue);
                out["status"] = "ok";
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Delete});

        // POST /api/v1/memory/search
        drogon::app().registerHandler(
            "/api/v1/memory/search",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_memorySearch) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "memory search not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                const auto body = req->getJsonObject();
                if (!body || !body->isObject()
                    || !body->isMember("query")
                    || !(*body)["query"].isString()) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "query is required";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                const std::string query = (*body)["query"].asString();
                const std::string agentId = body->isMember("agent_id")
                    ? (*body)["agent_id"].asString()
                    : "";
                const int64_t limit = body->isMember("limit")
                    ? std::max<int64_t>(1, std::min<int64_t>(200, (*body)["limit"].asInt64()))
                    : 20;

                memory::MemorySearchDomain domains;
                if (body->isMember("domains") && (*body)["domains"].isObject()) {
                    const auto& d = (*body)["domains"];
                    if (d.isMember("observations")) domains.observations = d["observations"].asBool();
                    if (d.isMember("ontology")) domains.ontology = d["ontology"].asBool();
                    if (d.isMember("raw_files")) domains.raw_files = d["raw_files"].asBool();
                    if (d.isMember("diary")) domains.diary = d["diary"].asBool();
                    if (d.isMember("sessions")) domains.sessions = d["sessions"].asBool();
                }

                const auto searchResults = server->m_memorySearch->Search(query, agentId, domains, limit);
                Json::Value out(Json::objectValue);
                Json::Value items(Json::arrayValue);
                for (const auto& result : searchResults) {
                    Json::Value item(Json::objectValue);
                    item["domain"] = result.domain;
                    item["id"] = static_cast<Json::Int64>(result.id);
                    item["text"] = result.text;
                    item["relevance"] = result.relevance;
                    if (result.layer_id.has_value()) {
                        item["layer_id"] = static_cast<Json::Int64>(*result.layer_id);
                    } else {
                        item["layer_id"] = Json::nullValue;
                    }
                    if (result.entity_path.has_value()) {
                        item["entity_path"] = *result.entity_path;
                    } else {
                        item["entity_path"] = Json::nullValue;
                    }
                    if (result.source_path.has_value()) {
                        item["source_path"] = *result.source_path;
                    } else {
                        item["source_path"] = Json::nullValue;
                    }
                    if (result.layer.has_value()) {
                        item["layer"] = *result.layer;
                    } else {
                        item["layer"] = Json::nullValue;
                    }
                    if (result.session_key.has_value()) {
                        item["session_key"] = *result.session_key;
                    } else {
                        item["session_key"] = Json::nullValue;
                    }
                    items.append(item);
                }
                out["results"] = items;
                out["count"] = static_cast<Json::UInt64>(items.size());
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Post});

        // POST /api/v1/memory/reindex — rebuild FTS indexes
        drogon::app().registerHandler(
            "/api/v1/memory/reindex",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_memorySearch) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "memory search not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                const auto body = req->getJsonObject();
                std::string domain = (body && body->isMember("domain") && (*body)["domain"].isString())
                    ? (*body)["domain"].asString() : "";

                Json::Value out(Json::objectValue);

                if (domain == "diary" || domain.empty()) {
                    bool ok = server->m_memorySearch->RebuildDiaryIndex();
                    auto stats = server->m_memorySearch->VerifyFtsSync("diary");
                    out["diary"]["rebuilt"] = ok;
                    out["diary"]["source_count"] = static_cast<Json::Int64>(stats.source_count);
                    out["diary"]["fts_count"] = static_cast<Json::Int64>(stats.fts_count);
                }

                if (domain == "observations" || domain.empty()) {
                    // Observations FTS uses IF NOT EXISTS triggers — verify and report.
                    auto stats = server->m_memorySearch->VerifyFtsSync("observations");
                    out["observations"]["source_count"] = static_cast<Json::Int64>(stats.source_count);
                    out["observations"]["fts_count"] = static_cast<Json::Int64>(stats.fts_count);
                }

                if (!out.isMember("diary") && !out.isMember("observations")) {
                    out["error"] = "unknown domain: '" + domain + "'. Use 'diary', 'observations', or empty for both.";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Post});

        drogon::app().registerHandler(
            "/api/v1/memory/observations/{1}",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& observationId) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                MemoryManager::OperationResult result;
                bool isNumeric = !observationId.empty();
                for (char ch : observationId) {
                    if (ch < '0' || ch > '9') {
                        isNumeric = false;
                        break;
                    }
                }
                if (isNumeric) {
                    result = server->m_memoryManager.SqlGetObservation(
                        server->m_memoryStore, observationId);
                } else {
                    result = server->m_memoryManager.BuildObservationResponse(observationId);
                }
                auto resp = drogon::HttpResponse::newHttpJsonResponse(result.body);
                resp->setStatusCode(static_cast<drogon::HttpStatusCode>(result.httpStatusCode));
                callback(resp);
            },
            {drogon::Get});

        drogon::app().registerHandler(
            "/api/v1/memory/observations/{1}",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& observationId) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                const auto body = req->getJsonObject();
                const auto result = server->m_memoryManager.PatchObservationFromBody(
                    observationId, body ? *body : Json::Value(Json::nullValue));
                if (result.httpStatusCode != 200) {
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(result.body);
                    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(result.httpStatusCode));
                    callback(resp);
                    return;
                }

                std::string persistErr;
                if (!server->SaveMemoryToDisk(&persistErr)) {
                    Json::Value out;
                    out["error"] = persistErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                callback(drogon::HttpResponse::newHttpJsonResponse(result.body));
            },
            {drogon::Patch});

        drogon::app().registerHandler(
            "/api/v1/memory/observations/{1}",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& observationId) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                MemoryManager::OperationResult result;
                bool isNumeric = !observationId.empty();
                for (char ch : observationId) {
                    if (ch < '0' || ch > '9') {
                        isNumeric = false;
                        break;
                    }
                }
                if (isNumeric) {
                    result = server->m_memoryManager.SqlDeleteObservation(
                        server->m_memoryStore, observationId);
                } else {
                    result = server->m_memoryManager.ArchiveObservationById(observationId);
                }
                if (result.httpStatusCode != 200) {
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(result.body);
                    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(result.httpStatusCode));
                    callback(resp);
                    return;
                }

                if (!isNumeric) {
                    std::string persistErr;
                    if (!server->SaveMemoryToDisk(&persistErr)) {
                        Json::Value out;
                        out["error"] = persistErr;
                        auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                        resp->setStatusCode(drogon::k500InternalServerError);
                        callback(resp);
                        return;
                    }
                }

                callback(drogon::HttpResponse::newHttpJsonResponse(result.body));
            },
            {drogon::Delete});

        drogon::app().registerHandler(
            "/api/v1/memory/consolidate",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_jobs) {
                    Json::Value out;
                    out["error"] = "job system not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                const auto startResult = server->m_memoryManager.BeginConsolidationRequest();
                if (startResult.httpStatusCode != 200) {
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(startResult.body);
                    resp->setStatusCode(
                        static_cast<drogon::HttpStatusCode>(startResult.httpStatusCode));
                    callback(resp);
                    return;
                }

                const std::uint64_t jobId = startResult.body.get("job_id", 0).asUInt64();
                server->m_jobs->EnqueueInLane(
                    ::animus::jobs::JobLane::Memory,
                    [server, jobId] {
                        server->m_memoryManager.RunConsolidationJob(jobId);
                    });

                callback(drogon::HttpResponse::newHttpJsonResponse(startResult.body));
            },
            {drogon::Post});

        drogon::app().registerHandler(
            "/api/v1/memory/consolidation/status",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                callback(drogon::HttpResponse::newHttpJsonResponse(
                    server->m_memoryManager.BuildConsolidationStatusResponse()));
            },
            {drogon::Get});

        // === Memory Layer API (SQLite-backed) ===

        // ============================================================================
        // Memory — SQLite-backed API
        // ====================================================================================

        // POST /api/v1/memory/layers
        drogon::app().registerHandler(
            "/api/v1/memory/layers",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(Json::Value("memory store not available"));
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }
                auto body = req->getJsonObject();
                const auto result = server->m_memoryManager.SqlCreateLayer(
                    server->m_memoryStore, body.get());
                auto resp = drogon::HttpResponse::newHttpJsonResponse(result.body);
                resp->setStatusCode(static_cast<drogon::HttpStatusCode>(result.httpStatusCode));
                callback(resp);
            },
            {drogon::Post});

        // PUT /api/v1/memory/layers/{id}
        drogon::app().registerHandler(
            "/api/v1/memory/layers/{id}",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& idStr) {
                AdminServer* server = g_activeAdminServer.load();
                auto body = req->getJsonObject();
                const auto result = server
                    ? server->m_memoryManager.SqlUpdateLayer(server->m_memoryStore, idStr, body.get())
                    : MemoryManager::OperationResult{200, Json::Value("unavailable")};
                auto resp = drogon::HttpResponse::newHttpJsonResponse(result.body);
                resp->setStatusCode(static_cast<drogon::HttpStatusCode>(result.httpStatusCode));
                callback(resp);
            },
            {drogon::Put});

        // DELETE /api/v1/memory/layers/{id}
        drogon::app().registerHandler(
            "/api/v1/memory/layers/{id}",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& idStr) {
                AdminServer* server = g_activeAdminServer.load();
                const auto result = server
                    ? server->m_memoryManager.SqlDeleteLayer(server->m_memoryStore, idStr)
                    : MemoryManager::OperationResult{200, Json::Value("unavailable")};
                auto resp = drogon::HttpResponse::newHttpJsonResponse(result.body);
                resp->setStatusCode(static_cast<drogon::HttpStatusCode>(result.httpStatusCode));
                callback(resp);
            },
            {drogon::Delete});

        // POST /api/v1/memory/layers/{id}/observations
        drogon::app().registerHandler(
            "/api/v1/memory/layers/{id}/observations",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& idStr) {
                AdminServer* server = g_activeAdminServer.load();
                auto body = req->getJsonObject();
                const auto result = server
                    ? server->m_memoryManager.SqlCreateLayerObservation(
                          server->m_memoryStore, idStr, body.get())
                    : MemoryManager::OperationResult{200, Json::Value("unavailable")};
                auto resp = drogon::HttpResponse::newHttpJsonResponse(result.body);
                resp->setStatusCode(static_cast<drogon::HttpStatusCode>(result.httpStatusCode));
                callback(resp);
            },
            {drogon::Post});

        // GET /api/v1/memory/layers/{id}/perspective
        drogon::app().registerHandler(
            "/api/v1/memory/layers/{id}/perspective",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& idStr) {
                AdminServer* server = g_activeAdminServer.load();
                const auto result = server
                    ? server->m_memoryManager.SqlGetLayerPerspective(server->m_memoryStore, idStr)
                    : MemoryManager::OperationResult{200, Json::Value("unavailable")};
                auto resp = drogon::HttpResponse::newHttpJsonResponse(result.body);
                resp->setStatusCode(static_cast<drogon::HttpStatusCode>(result.httpStatusCode));
                callback(resp);
            },
            {drogon::Get});

        // PUT /api/v1/memory/layers/{id}/perspective
        drogon::app().registerHandler(
            "/api/v1/memory/layers/{id}/perspective",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& idStr) {
                AdminServer* server = g_activeAdminServer.load();
                auto body = req->getJsonObject();
                const auto result = server
                    ? server->m_memoryManager.SqlPutLayerPerspective(
                          server->m_memoryStore, idStr, body.get())
                    : MemoryManager::OperationResult{200, Json::Value("unavailable")};
                auto resp = drogon::HttpResponse::newHttpJsonResponse(result.body);
                resp->setStatusCode(static_cast<drogon::HttpStatusCode>(result.httpStatusCode));
                callback(resp);
            },
            {drogon::Put});

        // ====================================================================
        // Manual consolidation triggers (Review Now / Perform Intake)
        // ====================================================================

        // POST /api/v1/memory/layers/{id}/consolidate
        // Triggers an immediate consolidation review session for a specific layer.
        drogon::app().registerHandler(
            "/api/v1/memory/layers/{id}/consolidate",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& idStr) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }

                int64_t layerId = 0;
                try { layerId = std::stoll(idStr); } catch (...) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "invalid layer id";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                auto layer = server->m_memoryStore->GetLayer(layerId);
                if (!layer) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "layer not found";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }

                // Create a one-shot schedule that fires immediately.
                // The scheduler callback will create a consolidation session.
                if (!server->m_scheduler) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "scheduler not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                ScheduleDescriptor sd;
                sd.agent_id = layer->agent_id;
                sd.tag = "consolidation";
                sd.type = ScheduleType::OneShot;
                sd.next_fire = Scheduler::IsoNow();
                sd.message = "consolidate:" + layer->name;
                std::string createErr;
                server->m_scheduler->Create(sd, &createErr);

                Json::Value out(Json::objectValue);
                out["triggered"] = createErr.empty();
                out["layer"] = layer->name;
                out["action"] = "consolidate";
                if (!createErr.empty()) out["error"] = createErr;
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Post});

        // POST /api/v1/memory/layers/{id}/intake
        // Triggers an immediate intake session for a specific layer.
        // Only works if the layer has intake enabled (intake_interval is set).
        drogon::app().registerHandler(
            "/api/v1/memory/layers/{id}/intake",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& idStr) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }

                int64_t layerId = 0;
                try { layerId = std::stoll(idStr); } catch (...) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "invalid layer id";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                auto layer = server->m_memoryStore->GetLayer(layerId);
                if (!layer) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "layer not found";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }

                if (!layer->intake_interval.has_value() || layer->intake_interval->empty()) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "intake not enabled for this layer (no intake_interval configured)";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                if (!server->m_scheduler) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "scheduler not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                ScheduleDescriptor sd;
                sd.agent_id = "default";
                sd.tag = "consolidation";
                sd.type = ScheduleType::OneShot;
                sd.next_fire = Scheduler::IsoNow();
                sd.message = "intake:" + std::to_string(layerId);
                std::string createErr;
                server->m_scheduler->Create(sd, &createErr);

                Json::Value out(Json::objectValue);
                out["triggered"] = createErr.empty();
                out["layer"] = layer->name;
                out["action"] = "intake";
                if (!createErr.empty()) out["error"] = createErr;
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Post});

        // ====================================================================
        // POST /api/v1/memory/agents/{id}/intake
        // Triggers an immediate intake session for a specific agent.
        // Agent-level intake (works regardless of layer intake config).
        // ====================================================================
        drogon::app().registerHandler(
            "/api/v1/memory/agents/{id}/intake",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& agentId) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }

                if (!server->m_scheduler) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "scheduler not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                ScheduleDescriptor sd;
                sd.agent_id = agentId;
                sd.tag = "consolidation";
                sd.type = ScheduleType::OneShot;
                sd.next_fire = Scheduler::IsoNow();
                sd.message = "intake:day";
                std::string createErr;
                server->m_scheduler->Create(sd, &createErr);

                Json::Value out(Json::objectValue);
                out["triggered"] = createErr.empty();
                out["agent_id"] = agentId;
                out["action"] = "intake";
                if (!createErr.empty()) out["error"] = createErr;
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Post});

        // ====================================================================
        // Session Notes API
        // ====================================================================

        // GET /api/v1/sessions/{sessionKey}/notes
        drogon::app().registerHandler(
            "/api/v1/sessions/{1}/notes",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& sessionKey) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_sessionNotesStore) {
                    SendJsonError(callback, "session notes not available", drogon::k503ServiceUnavailable);
                    return;
                }
                std::string agentId = req->getParameter("agent_id");
                if (agentId.empty()) agentId = "default";

                auto notes = server->m_sessionNotesStore->ListForSession(sessionKey, agentId);
                Json::Value items(Json::arrayValue);
                for (const auto& note : notes) {
                    Json::Value item(Json::objectValue);
                    item["id"] = static_cast<Json::Int64>(note.id);
                    item["bullet"] = note.bullet;
                    item["sort_order"] = note.sort_order;
                    item["created_at_unix_ms"] = static_cast<Json::Int64>(note.created_at_unix_ms);
                    item["updated_at_unix_ms"] = static_cast<Json::Int64>(note.updated_at_unix_ms);
                    items.append(item);
                }
                Json::Value body(Json::objectValue);
                body["notes"] = items;
                body["count"] = static_cast<Json::Int>(items.size());
                callback(drogon::HttpResponse::newHttpJsonResponse(body));
            },
            {drogon::Get});

        // POST /api/v1/sessions/{sessionKey}/notes
        drogon::app().registerHandler(
            "/api/v1/sessions/{1}/notes",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& sessionKey) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_sessionNotesStore) {
                    SendJsonError(callback, "session notes not available", drogon::k503ServiceUnavailable);
                    return;
                }
                const auto body = req->getJsonObject();
                if (!body || !body->isMember("bullet") || !(*body)["bullet"].isString()) {
                    SendJsonError(callback, "bullet is required", drogon::k400BadRequest);
                    return;
                }
                std::string agentId = (body->isMember("agent_id") && (*body)["agent_id"].isString())
                    ? (*body)["agent_id"].asString() : "default";
                std::string bullet = (*body)["bullet"].asString();
                std::string position = (body->isMember("position") && (*body)["position"].isString())
                    ? (*body)["position"].asString() : "bottom";

                auto count = server->m_sessionNotesStore->CountForSession(sessionKey, agentId);
                if (count >= SessionNotesStore::kMaxNotesPerSession) {
                    SendJsonError(callback, "session note limit reached", drogon::k400BadRequest);
                    return;
                }

                int sortOrder;
                if (position == "top") {
                    sortOrder = 0;
                } else {
                    sortOrder = server->m_sessionNotesStore->NextSortOrder(sessionKey, agentId);
                }
                auto note = server->m_sessionNotesStore->Create(sessionKey, agentId, bullet, sortOrder);

                if (position == "top") {
                    auto all = server->m_sessionNotesStore->ListForSession(sessionKey, agentId);
                    std::vector<int64_t> ids;
                    ids.push_back(note.id);
                    for (const auto& n : all) {
                        if (n.id != note.id) ids.push_back(n.id);
                    }
                    server->m_sessionNotesStore->Reorder(ids, agentId);
                }

                Json::Value resp(Json::objectValue);
                resp["id"] = static_cast<Json::Int64>(note.id);
                resp["bullet"] = note.bullet;
                resp["sort_order"] = note.sort_order;
                callback(drogon::HttpResponse::newHttpJsonResponse(resp));
            },
            {drogon::Post});

        // PATCH /api/v1/sessions/{sessionKey}/notes/{noteId}
        drogon::app().registerHandler(
            "/api/v1/sessions/{1}/notes/{2}",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& sessionKey,
               const std::string& noteIdStr) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_sessionNotesStore) {
                    SendJsonError(callback, "session notes not available", drogon::k503ServiceUnavailable);
                    return;
                }
                int64_t noteId = 0;
                try { noteId = std::stoll(noteIdStr); } catch (...) {}
                if (noteId <= 0) {
                    SendJsonError(callback, "invalid note_id", drogon::k400BadRequest);
                    return;
                }
                const auto body = req->getJsonObject();
                std::string agentId = (body && body->isMember("agent_id") && (*body)["agent_id"].isString())
                    ? (*body)["agent_id"].asString() : "default";
                std::string newBullet = (body && body->isMember("bullet") && (*body)["bullet"].isString())
                    ? (*body)["bullet"].asString() : "";
                if (newBullet.empty()) {
                    SendJsonError(callback, "bullet is required", drogon::k400BadRequest);
                    return;
                }
                if (!server->m_sessionNotesStore->Update(noteId, agentId, newBullet)) {
                    SendJsonError(callback, "note not found or update failed", drogon::k404NotFound);
                    return;
                }
                Json::Value resp(Json::objectValue);
                resp["success"] = true;
                callback(drogon::HttpResponse::newHttpJsonResponse(resp));
            },
            {drogon::Patch});

        // DELETE /api/v1/sessions/{sessionKey}/notes/{noteId}
        drogon::app().registerHandler(
            "/api/v1/sessions/{1}/notes/{2}",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& sessionKey,
               const std::string& noteIdStr) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_sessionNotesStore) {
                    SendJsonError(callback, "session notes not available", drogon::k503ServiceUnavailable);
                    return;
                }
                int64_t noteId = 0;
                try { noteId = std::stoll(noteIdStr); } catch (...) {}
                if (noteId <= 0) {
                    SendJsonError(callback, "invalid note_id", drogon::k400BadRequest);
                    return;
                }
                std::string agentId = req->getParameter("agent_id");
                if (agentId.empty()) agentId = "default";
                if (!server->m_sessionNotesStore->Delete(noteId, agentId)) {
                    SendJsonError(callback, "note not found or delete failed", drogon::k404NotFound);
                    return;
                }
                Json::Value resp(Json::objectValue);
                resp["success"] = true;
                callback(drogon::HttpResponse::newHttpJsonResponse(resp));
            },
            {drogon::Delete});

        // ====================================================================
        // Context Provider Registry API
        // ====================================================================

        // GET /api/v1/context/providers
        drogon::app().registerHandler(
            "/api/v1/context/providers",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }

                if (!server->m_contextRegistry) {
                    SendJsonError(callback, "context registry not available", drogon::k503ServiceUnavailable);
                    return;
                }

                auto infos = server->m_contextRegistry->ListProviders();
                Json::Value items(Json::arrayValue);
                for (const auto& info : infos) {
                    Json::Value item(Json::objectValue);
                    item["name"] = info.name;
                    item["priority"] = info.priority;
                    items.append(item);
                }

                Json::Value body(Json::objectValue);
                body["providers"] = items;
                body["count"] = static_cast<Json::Int>(items.size());
                callback(drogon::HttpResponse::newHttpJsonResponse(body));
            },
            {drogon::Get});

        // ====================================================================
        // Active Memory — assembled context for an agent/session
        // ====================================================================

        // GET /api/v1/context/active-memory?agent_id=default&session_key=...
        //
        // Returns the fully assembled agent preamble broken into:
        //   rendered  — composite markdown of all blocks
        //   blocks    — per-block name, content, priority
        //   flags     — which sub-blocks were active for this session type
        //
        // Without session_key: uses a synthetic empty session (provider testing).
        // With session_key:    resolves the real session for runtime debugging.
        drogon::app().registerHandler(
            "/api/v1/context/active-memory",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }

                if (!server->m_contextRegistry) {
                    SendJsonError(callback, "context registry not available", drogon::k503ServiceUnavailable);
                    return;
                }

                // Resolve agent
                std::string agentId = req->getParameter("agent_id");
                if (agentId.empty()) agentId = "default";

                Agent agent;
                if (server->m_agentStore) {
                    auto loaded = server->m_agentStore->GetById(agentId);
                    if (!loaded) {
                        SendJsonError(callback, "agent not found: " + agentId, drogon::k404NotFound);
                        return;
                    }
                    agent = *loaded;
                } else {
                    SendJsonError(callback, "agent store not available", drogon::k503ServiceUnavailable);
                    return;
                }

                // Resolve or synthesize session
                std::string sessionKeyStr = req->getParameter("session_key");
                SessionAccess sessionAccess;

                if (!sessionKeyStr.empty() && server->m_sessions) {
                    // Parse session key: connector|conversation_id|thread_id
                    // Also accept numeric session ID for convenience
                    bool found = false;

                    // Try as numeric ID first
                    SessionId sid = 0;
                    bool isNumeric = !sessionKeyStr.empty();
                    for (char c : sessionKeyStr) {
                        if (!std::isdigit(static_cast<unsigned char>(c))) {
                            isNumeric = false;
                            break;
                        }
                    }

                    if (isNumeric) {
                        try {
                            sid = std::stoull(sessionKeyStr);
                        } catch (...) {
                            sid = 0;
                        }
                        auto session = server->m_sessions->GetById(sid);
                        if (session) {
                            sessionAccess = SessionAccess(session, SessionAccessMode::ReadOnly);
                            found = true;
                        }
                    }

                    if (!found) {
                        // Try as SessionKey string (connector|conv_id|thread_id)
                        auto pipePos1 = sessionKeyStr.find('|');
                        if (pipePos1 != std::string::npos) {
                            auto pipePos2 = sessionKeyStr.find('|', pipePos1 + 1);
                            if (pipePos2 != std::string::npos) {
                                SessionKey key;
                                key.connector = sessionKeyStr.substr(0, pipePos1);
                                key.conversation_id = sessionKeyStr.substr(pipePos1 + 1, pipePos2 - pipePos1 - 1);
                                key.thread_id = sessionKeyStr.substr(pipePos2 + 1);
                                auto session = server->m_sessions->GetOrCreate(key);
                                if (session) {
                                    sessionAccess = SessionAccess(session, SessionAccessMode::ReadOnly);
                                    found = true;
                                }
                            }
                        }
                    }

                    if (!found && isNumeric) {
                        SendJsonError(callback, "session not found: " + sessionKeyStr, drogon::k404NotFound);
                        return;
                    }
                    // Non-numeric key that didn't parse: fall through to synthetic
                }

                if (!sessionAccess) {
                    // Synthetic session — empty turns, no tags, for provider testing
                    auto synthetic = std::make_shared<Session>(0, SessionKey{"_synthetic", "_active_memory_view", ""});
                    synthetic->SetAgentId(agentId);
                    sessionAccess = SessionAccess(synthetic, SessionAccessMode::ReadOnly);
                }

                // Assemble context blocks (cached for admin preview)
                auto blocks = server->m_contextRegistry->AssembleCached(agent, sessionAccess);

                // Build rendered composite
                std::string rendered;
                for (const auto& block : blocks) {
                    rendered += block.content;
                    rendered += "\n";
                }

                // Build JSON response
                Json::Value body(Json::objectValue);
                body["rendered"] = rendered;
                body["agent_id"] = agentId;
                body["is_synthetic_session"] = !sessionKeyStr.empty() ? false : true;
                if (!sessionKeyStr.empty()) {
                    body["session_key"] = sessionKeyStr;
                }

                // Per-block breakdown
                Json::Value blocksArr(Json::arrayValue);
                for (const auto& block : blocks) {
                    Json::Value b(Json::objectValue);
                    b["name"] = block.name;
                    b["content"] = block.content;
                    b["priority"] = block.priority;
                    blocksArr.append(b);
                }
                body["blocks"] = blocksArr;
                body["block_count"] = static_cast<Json::Int>(blocks.size());

                // Session flags (for debugging which sub-blocks were active)
                // Derive from whether session is synthetic or real
                Json::Value flags(Json::objectValue);
                bool isSynthetic = sessionAccess.Id() == 0;
                flags["is_synthetic"] = isSynthetic;
                flags["include_episodic"] = true;
                flags["include_diary"] = true;
                flags["include_ontology"] = !isSynthetic; // ontology needs tags/keywords from real turns
                body["flags"] = flags;

                callback(drogon::HttpResponse::newHttpJsonResponse(body));
            },
            {drogon::Get});

        // ====================================================================
        // Session Tags API
        // ====================================================================

        // GET /api/v1/sessions/{sessionKey}/tags
        drogon::app().registerHandler(
            "/api/v1/sessions/{1}/tags",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& sessionKey) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_sessionTagsStore) {
                    SendJsonError(callback, "session tags not available", drogon::k503ServiceUnavailable);
                    return;
                }

                auto tags = server->m_sessionTagsStore->ListForSession(sessionKey);
                Json::Value items(Json::arrayValue);
                for (const auto& t : tags) {
                    Json::Value item(Json::objectValue);
                    item["tag"] = t.tag;
                    item["source"] = t.source;
                    item["created_at_unix_ms"] = static_cast<Json::Int64>(t.created_at_unix_ms);
                    items.append(item);
                }
                Json::Value body(Json::objectValue);
                body["tags"] = items;
                body["count"] = static_cast<Json::Int>(items.size());
                callback(drogon::HttpResponse::newHttpJsonResponse(body));
            },
            {drogon::Get});

        // POST /api/v1/sessions/{sessionKey}/tags
        drogon::app().registerHandler(
            "/api/v1/sessions/{1}/tags",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& sessionKey) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_sessionTagsStore) {
                    SendJsonError(callback, "session tags not available", drogon::k503ServiceUnavailable);
                    return;
                }
                const auto body = req->getJsonObject();
                if (!body || !body->isMember("tag") || !(*body)["tag"].isString()) {
                    SendJsonError(callback, "tag is required", drogon::k400BadRequest);
                    return;
                }
                std::string agentId = (body->isMember("agent_id") && (*body)["agent_id"].isString())
                    ? (*body)["agent_id"].asString() : "default";
                std::string tag = (*body)["tag"].asString();
                std::string source = (body->isMember("source") && (*body)["source"].isString())
                    ? (*body)["source"].asString() : "agent";

                auto result = server->m_sessionTagsStore->Add(sessionKey, agentId, tag, source);

                Json::Value resp(Json::objectValue);
                resp["tag"] = result.tag;
                resp["source"] = result.source;
                callback(drogon::HttpResponse::newHttpJsonResponse(resp));
            },
            {drogon::Post});

        // DELETE /api/v1/sessions/{sessionKey}/tags
        drogon::app().registerHandler(
            "/api/v1/sessions/{1}/tags",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& sessionKey) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_sessionTagsStore) {
                    SendJsonError(callback, "session tags not available", drogon::k503ServiceUnavailable);
                    return;
                }
                std::string tag = req->getParameter("tag");
                std::string agentId = req->getParameter("agent_id");
                if (agentId.empty()) agentId = "default";
                if (tag.empty()) {
                    SendJsonError(callback, "tag query parameter is required", drogon::k400BadRequest);
                    return;
                }
                if (!server->m_sessionTagsStore->Remove(sessionKey, agentId, tag)) {
                    SendJsonError(callback, "tag not found", drogon::k404NotFound);
                    return;
                }
                Json::Value resp(Json::objectValue);
                resp["success"] = true;
                callback(drogon::HttpResponse::newHttpJsonResponse(resp));
            },
            {drogon::Delete});

        // ============================================================================
        // Session Reports — per-session temporal summaries
        // ============================================================================

        // GET /api/v1/sessions/{id}/report — get report for a session
        drogon::app().registerHandler(
            "/api/v1/sessions/{1}/report",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               std::string sessionIdStr) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server || !server->m_sessionReportStore) {
                    SendJsonError(callback, "session reports not available", drogon::k503ServiceUnavailable);
                    return;
                }

                int64_t sessionId = 0;
                try { sessionId = std::stoll(sessionIdStr); } catch (...) {
                    SendJsonError(callback, "invalid session id", drogon::k400BadRequest);
                    return;
                }

                std::string agentId = req->getParameter("agent_id");
                if (agentId.empty()) agentId = "default";

                auto report = server->m_sessionReportStore->GetForSession(sessionId, agentId);
                if (!report) {
                    SendJsonError(callback, "no report for this session", drogon::k404NotFound);
                    return;
                }

                Json::Value resp(Json::objectValue);
                resp["id"] = report->id;
                resp["session_id"] = report->session_id;
                resp["agent_id"] = report->agent_id;
                resp["summary"] = report->summary;
                resp["past_events"] = report->past_events;
                resp["current_activity"] = report->current_activity;
                resp["forward_look"] = report->forward_look;
                resp["created_at_unix_ms"] = report->created_at_unix_ms;
                resp["updated_at_unix_ms"] = report->updated_at_unix_ms;
                callback(drogon::HttpResponse::newHttpJsonResponse(resp));
            },
            {drogon::Get});

        // PUT /api/v1/sessions/{id}/report — create or update report
        drogon::app().registerHandler(
            "/api/v1/sessions/{1}/report",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               std::string sessionIdStr) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server || !server->m_sessionReportStore) {
                    SendJsonError(callback, "session reports not available", drogon::k503ServiceUnavailable);
                    return;
                }

                int64_t sessionId = 0;
                try { sessionId = std::stoll(sessionIdStr); } catch (...) {
                    SendJsonError(callback, "invalid session id", drogon::k400BadRequest);
                    return;
                }

                auto jsonBody = req->getJsonObject();
                if (!jsonBody) {
                    SendJsonError(callback, "JSON body required", drogon::k400BadRequest);
                    return;
                }

                std::string agentId = jsonBody->get("agent_id", "default").asString();
                std::string summary = jsonBody->get("summary", "").asString();
                std::string pastEvents = jsonBody->get("past_events", "").asString();
                std::string currentActivity = jsonBody->get("current_activity", "").asString();
                std::string forwardLook = jsonBody->get("forward_look", "").asString();

                auto report = server->m_sessionReportStore->Upsert(
                    sessionId, agentId, summary, pastEvents, currentActivity, forwardLook);

                Json::Value resp(Json::objectValue);
                resp["id"] = report.id;
                resp["session_id"] = report.session_id;
                resp["agent_id"] = report.agent_id;
                resp["summary"] = report.summary;
                resp["past_events"] = report.past_events;
                resp["current_activity"] = report.current_activity;
                resp["forward_look"] = report.forward_look;
                resp["created_at_unix_ms"] = report.created_at_unix_ms;
                resp["updated_at_unix_ms"] = report.updated_at_unix_ms;
                callback(drogon::HttpResponse::newHttpJsonResponse(resp));
            },
            {drogon::Put, drogon::Post});

        // DELETE /api/v1/sessions/{id}/report — remove report
        drogon::app().registerHandler(
            "/api/v1/sessions/{1}/report",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               std::string sessionIdStr) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server || !server->m_sessionReportStore) {
                    SendJsonError(callback, "session reports not available", drogon::k503ServiceUnavailable);
                    return;
                }

                int64_t sessionId = 0;
                try { sessionId = std::stoll(sessionIdStr); } catch (...) {
                    SendJsonError(callback, "invalid session id", drogon::k400BadRequest);
                    return;
                }

                std::string agentId = req->getParameter("agent_id");
                if (agentId.empty()) agentId = "default";

                if (!server->m_sessionReportStore->Delete(sessionId, agentId)) {
                    SendJsonError(callback, "report not found", drogon::k404NotFound);
                    return;
                }

                Json::Value resp(Json::objectValue);
                resp["success"] = true;
                callback(drogon::HttpResponse::newHttpJsonResponse(resp));
            },
            {drogon::Delete});

        // GET /api/v1/agents/{id}/session-reports — list recent reports
        drogon::app().registerHandler(
            "/api/v1/agents/{1}/session-reports",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               std::string agentId) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server || !server->m_sessionReportStore) {
                    SendJsonError(callback, "session reports not available", drogon::k503ServiceUnavailable);
                    return;
                }

                int limit = 20;
                std::string limitStr = req->getParameter("limit");
                if (!limitStr.empty()) {
                    try { limit = std::stoi(limitStr); } catch (...) {}
                    if (limit < 1) limit = 1;
                    if (limit > 100) limit = 100;
                }

                auto reports = server->m_sessionReportStore->ListRecent(agentId, limit);

                Json::Value arr(Json::arrayValue);
                for (const auto& r : reports) {
                    Json::Value item(Json::objectValue);
                    item["id"] = r.id;
                    item["session_id"] = r.session_id;
                    item["summary"] = r.summary;
                    item["current_activity"] = r.current_activity;
                    item["forward_look"] = r.forward_look;
                    item["updated_at_unix_ms"] = r.updated_at_unix_ms;
                    arr.append(item);
                }

                Json::Value resp(Json::objectValue);
                resp["reports"] = arr;
                resp["count"] = static_cast<Json::UInt>(arr.size());
                callback(drogon::HttpResponse::newHttpJsonResponse(resp));
            },
            {drogon::Get});

        // GET /api/v1/agents/{id}/session-reports/search — search reports
        drogon::app().registerHandler(
            "/api/v1/agents/{1}/session-reports/search",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               std::string agentId) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server || !server->m_sessionReportStore) {
                    SendJsonError(callback, "session reports not available", drogon::k503ServiceUnavailable);
                    return;
                }

                std::string query = req->getParameter("q");
                if (query.empty()) {
                    SendJsonError(callback, "query parameter 'q' is required", drogon::k400BadRequest);
                    return;
                }

                auto reports = server->m_sessionReportStore->Search(agentId, query, 20);

                Json::Value arr(Json::arrayValue);
                for (const auto& r : reports) {
                    Json::Value item(Json::objectValue);
                    item["id"] = r.id;
                    item["session_id"] = r.session_id;
                    item["summary"] = r.summary;
                    item["past_events"] = r.past_events;
                    item["current_activity"] = r.current_activity;
                    item["forward_look"] = r.forward_look;
                    item["updated_at_unix_ms"] = r.updated_at_unix_ms;
                    arr.append(item);
                }

                Json::Value resp(Json::objectValue);
                resp["reports"] = arr;
                resp["count"] = static_cast<Json::UInt>(arr.size());
                callback(drogon::HttpResponse::newHttpJsonResponse(resp));
            },
            {drogon::Get});
