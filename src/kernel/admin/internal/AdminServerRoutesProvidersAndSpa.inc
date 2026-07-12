        // ============================================================================
        // Status
        // ====================================================================================
        drogon::app().registerHandler(
            "/api/v1/status",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                Json::Value body;

                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    body["status"] = "error";
                    body["error"] = "admin server not active";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                body["status"] = "ok";
                body["uptime"] = static_cast<Json::UInt64>(server->UptimeSeconds());
                callback(drogon::HttpResponse::newHttpJsonResponse(body));
            },
            {drogon::Get});



        // ===================================================================
        // Provider Configuration API
        // ===================================================================

        // GET /api/v1/providers — list all providers

        // ===================================================================
        // Provider Configuration API
        // ===================================================================

        // GET /api/v1/system/first-run — check if wizard is needed
        drogon::app().registerHandler(
            "/api/v1/system/first-run",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                Json::Value body;
                if (!server || !server->m_agentStore) {
                    body["wizard_required"] = true;
                } else {
                    body["wizard_required"] = !server->m_agentStore->HasAnyAgent();
                }
                callback(drogon::HttpResponse::newHttpJsonResponse(body));
            },
            {drogon::Get});

        // ============================================================================
        // Vision models — list all vision-capable models across all providers
        // ====================================================================================
        drogon::app().registerHandler(
            "/api/v1/vision-models",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                Json::Value models(Json::arrayValue);

                for (const auto& state : server->m_providerManager.ListProviders()) {
                    auto configOpt = server->m_providerManager.GetProviderConfig(state.providerId);
                    if (!configOpt) continue;

                    auto provider = server->m_providerRegistry->Create(*configOpt);
                    if (!provider) continue;

                    auto* baseProvider = dynamic_cast<llm::LLMProviderBase*>(provider.get());
                    if (!baseProvider) continue;

                    // FetchCapabilities populates raw_features with all model IDs
                    std::string modelForCap = state.defaultModel.empty()
                        ? "_list" : state.defaultModel;
                    baseProvider->FetchCapabilities(modelForCap);

                    // GetVisionModelIds returns vision-capable model IDs
                    auto visionModels = baseProvider->GetVisionModelIds();
                    for (const auto& m : visionModels) {
                        Json::Value entry(Json::objectValue);
                        entry["provider_id"] = state.providerId;
                        entry["model_id"] = m;
                        entry["label"] = state.providerId + "/" + m;
                        models.append(entry);
                    }
                }

                Json::Value out(Json::objectValue);
                out["models"] = models;
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        // ============================================================================
        // Providers (CRUD + test + capabilities + models + default)
        // ====================================================================================
        drogon::app().registerHandler(
            "/api/v1/providers",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                Json::Value out(Json::objectValue);
                out["default_provider"] = server->m_providerManager.GetDefaultProviderId();
                Json::Value providers(Json::arrayValue);
                for (const auto& state : server->m_providerManager.ListProviders()) {
                    providers.append(server->m_providerManager.BuildProviderJson(state, true));
                }
                out["providers"] = providers;
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        // GET /api/v1/providers/{id}
        // GET /api/v1/providers/{id}/models — list available models
        drogon::app().registerHandler(
            "/api/v1/providers/{id}/models",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                Json::Value models(Json::arrayValue);
                bool ok = false;
                std::string modelsErr;
                if (!server->m_providerManager.FetchProviderModels(
                        id, server->m_providerRegistry, &models, &ok, &modelsErr)) {
                    Json::Value out;
                    out["error"] = modelsErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    if (modelsErr.rfind("provider not found:", 0) == 0) {
                        resp->setStatusCode(drogon::k404NotFound);
                    } else {
                        resp->setStatusCode(drogon::k500InternalServerError);
                    }
                    callback(resp);
                    return;
                }

                Json::Value out(Json::objectValue);
                out["provider_id"] = id;
                out["models"] = models;
                out["fetched"] = ok;

                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        // GET /api/v1/providers/{id}/capabilities — fetch fresh capabilities
        drogon::app().registerHandler(
            "/api/v1/providers/{id}/capabilities",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                llm::ProviderCapabilities caps;
                bool ok = false;
                std::string capErr;
                if (!server->m_providerManager.RefreshProviderCapabilities(
                        id,
                        server->m_providerRegistry,
                        std::string(req->getParameter("model")),
                        &caps,
                        &ok,
                        &capErr)) {
                    Json::Value out;
                    out["error"] = capErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    if (capErr.rfind("provider not found:", 0) == 0) {
                        resp->setStatusCode(drogon::k404NotFound);
                    } else if (capErr == "no default model configured for this provider") {
                        resp->setStatusCode(drogon::k400BadRequest);
                    } else {
                        resp->setStatusCode(drogon::k500InternalServerError);
                    }
                    callback(resp);
                    return;
                }

                Json::Value out(Json::objectValue);
                out["provider_id"] = id;
                out["model_id"] = caps.model_id;
                out["fetched"] = ok;
                Json::Value capsJson(Json::objectValue);
                capsJson["model_id"] = caps.model_id;
                capsJson["context_length"] = caps.context_length;
                capsJson["supports_tools"] = caps.supports_tools;
                capsJson["supports_tool_choice"] = caps.supports_tool_choice;
                capsJson["supports_reasoning"] = caps.supports_reasoning;
                capsJson["supports_streaming"] = caps.supports_streaming;
                capsJson["supports_json_mode"] = caps.supports_json_mode;
                capsJson["supports_vision"] = caps.supports_vision;
                Json::Value rawFeatures(Json::arrayValue);
                for (const auto& f : caps.raw_features) {
                    rawFeatures.append(f);
                }
                capsJson["raw_features"] = rawFeatures;
                out["capabilities"] = capsJson;

                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        // GET /api/v1/providers/{id} — single provider
        drogon::app().registerHandler(
            "/api/v1/providers/{id}",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                auto state = server->m_providerManager.GetProvider(id);
                if (!state.has_value()) {
                    Json::Value out;
                    out["error"] = "provider not found: " + id;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }
                callback(drogon::HttpResponse::newHttpJsonResponse(
                    server->m_providerManager.BuildProviderJson(*state, true)));
            },
            {drogon::Get});

        // POST /api/v1/providers — create provider
        drogon::app().registerHandler(
            "/api/v1/providers",
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
                    out["error"] = "request body must be a JSON object";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                std::string validationErr;
                if (!server->m_providerManager.ValidateProviderPayload(*body, true, &validationErr)) {
                    Json::Value out;
                    out["error"] = validationErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                const std::string id = (*body)["provider_id"].asString();
                ProviderState state;
                std::string parseErr;
                if (!server->m_providerManager.ParseProviderPayload(*body, ProviderState{}, &state, &parseErr)) {
                    Json::Value out;
                    out["error"] = parseErr.empty() ? "failed to parse provider payload" : parseErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }
                state.status = "untested";

                std::string createErr;
                if (!server->m_providerManager.CreateProvider(state, &createErr)) {
                    Json::Value out;
                    out["error"] = createErr.empty() ? ("provider already exists: " + id) : createErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k409Conflict);
                    callback(resp);
                    return;
                }

                std::string persistErr;
                if (!server->SaveProvidersToDisk(&persistErr)) {
                    ProviderState ignored;
                    std::string ignoreErr;
                    (void)server->m_providerManager.DeleteProvider(id, &ignored, &ignoreErr);
                    Json::Value out;
                    out["error"] = persistErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                auto resp = drogon::HttpResponse::newHttpJsonResponse(
                    server->m_providerManager.BuildProviderJson(state, true));
                resp->setStatusCode(drogon::k201Created);
                callback(resp);
            },
            {drogon::Post});

        // PUT /api/v1/providers/{id} — update provider
        drogon::app().registerHandler(
            "/api/v1/providers/{id}",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                const auto body = req->getJsonObject();
                if (!body || !body->isObject()) {
                    Json::Value out;
                    out["error"] = "request body must be a JSON object";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                std::string validationErr;
                if (!server->m_providerManager.ValidateProviderPayload(*body, false, &validationErr)) {
                    Json::Value out;
                    out["error"] = validationErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                ProviderState before;
                auto beforeOpt = server->m_providerManager.GetProvider(id);
                if (!beforeOpt.has_value()) {
                    Json::Value out;
                    out["error"] = "provider not found: " + id;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }
                before = *beforeOpt;

                ProviderState updated;
                std::string parseErr;
                if (!server->m_providerManager.ParseProviderPayload(*body, before, &updated, &parseErr)) {
                    Json::Value out;
                    out["error"] = parseErr.empty() ? "failed to parse provider payload" : parseErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }
                updated.providerId = id; // Ensure ID doesn't change
                updated.status = "untested"; // Reset status on config change
                updated.lastError.clear();

                std::string updateErr;
                if (!server->m_providerManager.UpdateProvider(id, updated, &updateErr)) {
                    Json::Value out;
                    out["error"] = updateErr.empty() ? ("provider not found: " + id) : updateErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }

                std::string persistErr;
                if (!server->SaveProvidersToDisk(&persistErr)) {
                    std::string rollbackErr;
                    (void)server->m_providerManager.UpdateProvider(id, before, &rollbackErr);
                    Json::Value out;
                    out["error"] = persistErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                callback(drogon::HttpResponse::newHttpJsonResponse(
                    server->m_providerManager.BuildProviderJson(updated, true)));
            },
            {drogon::Put});

        // DELETE /api/v1/providers/{id}
        drogon::app().registerHandler(
            "/api/v1/providers/{id}",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                ProviderState removed;
                std::string deleteErr;
                if (!server->m_providerManager.DeleteProvider(id, &removed, &deleteErr)) {
                    Json::Value out;
                    out["error"] = deleteErr.empty() ? ("provider not found: " + id) : deleteErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }

                std::string persistErr;
                if (!server->SaveProvidersToDisk(&persistErr)) {
                    std::string rollbackErr;
                    (void)server->m_providerManager.CreateProvider(removed, &rollbackErr);
                    Json::Value out;
                    out["error"] = persistErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                Json::Value out(Json::objectValue);
                out["deleted"] = id;
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Delete});

        // POST /api/v1/providers/{id}/test — test connectivity
        drogon::app().registerHandler(
            "/api/v1/providers/{id}/test",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                bool success = false;
                std::string testError;
                std::string status;
                std::string testErr;
                if (!server->m_providerManager.TestProviderConnectivity(
                        id, &success, &testError, &status, &testErr)) {
                    Json::Value out;
                    out["error"] = testErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    if (testErr.rfind("provider not found:", 0) == 0) {
                        resp->setStatusCode(drogon::k404NotFound);
                    } else {
                        resp->setStatusCode(drogon::k500InternalServerError);
                    }
                    callback(resp);
                    return;
                }

                Json::Value out(Json::objectValue);
                out["provider_id"] = id;
                out["success"] = success;
                if (!testError.empty()) {
                    out["error"] = testError;
                }
                out["status"] = status;
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Post});

        // GET /api/v1/providers/{id}/oauth/status — inspect persisted OAuth token state
        drogon::app().registerHandler(
            "/api/v1/providers/{id}/oauth/status",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                Json::Value out(Json::objectValue);
                int statusCode = 200;
                std::string statusErr;
                if (!server->m_providerManager.BuildOAuthStatus(id, &out, &statusCode, &statusErr)) {
                    Json::Value err(Json::objectValue);
                    err["error"] = statusErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
                    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(statusCode));
                    callback(resp);
                    return;
                }

                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        // POST /api/v1/providers/{id}/oauth/browser/start — start PKCE browser flow
        drogon::app().registerHandler(
            "/api/v1/providers/{id}/oauth/browser/start",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                int providerStatusCode = 200;
                std::string providerErr;
                if (!server->m_providerManager.ValidateOAuthProvider(
                        id, true, nullptr, &providerStatusCode, &providerErr)) {
                    Json::Value out;
                    out["error"] = providerErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(providerStatusCode));
                    callback(resp);
                    return;
                }

                std::string redirectUri = "http://localhost:1455/auth/callback";
                if (const auto body = req->getJsonObject(); body && body->isObject()) {
                    const std::string candidate = (*body).get("redirect_uri", "").asString();
                    if (!candidate.empty()) {
                        redirectUri = candidate;
                    }
                }

                if (!(redirectUri.rfind("http://", 0) == 0 || redirectUri.rfind("https://", 0) == 0)) {
                    Json::Value out;
                    out["error"] = "redirect_uri must start with http:// or https://";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                const auto startResult = server->m_oauthManager.StartOpenAICodexBrowserFlow(
                    id, redirectUri, NowUnixMs());
                auto successResp = drogon::HttpResponse::newHttpJsonResponse(startResult.body);
                successResp->setStatusCode(
                    static_cast<drogon::HttpStatusCode>(startResult.httpStatusCode));
                callback(successResp);
            },
            {drogon::Post});

        // POST /api/v1/providers/{id}/oauth/browser/exchange — paste callback URL/code and exchange tokens
        drogon::app().registerHandler(
            "/api/v1/providers/{id}/oauth/browser/exchange",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                int providerStatusCode = 200;
                std::string providerErr;
                if (!server->m_providerManager.ValidateOAuthProvider(
                        id, true, nullptr, &providerStatusCode, &providerErr)) {
                    Json::Value out;
                    out["error"] = providerErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(providerStatusCode));
                    callback(resp);
                    return;
                }

                const auto body = req->getJsonObject();
                if (!body || !body->isObject()) {
                    Json::Value out;
                    out["error"] = "request body must be a JSON object";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                std::string code = (*body).get("code", "").asString();
                std::string returnedState = (*body).get("state", "").asString();
                const std::string callbackUrl = (*body).get("callback_url", "").asString();
                const auto exchangeResult = server->m_oauthManager.CompleteOpenAICodexBrowserFlow(
                    id, code, returnedState, callbackUrl, NowUnixMs());
                if (exchangeResult.httpStatusCode != 200) {
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(exchangeResult.body);
                    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(exchangeResult.httpStatusCode));
                    callback(resp);
                    return;
                }

                Json::Value authEntry = exchangeResult.body["auth_entry"];
                const Json::Int64 expiresAtMs = exchangeResult.body.get("expires", 0).asInt64();

                std::string saveErr;
                if (!server->SaveAuthToDisk(id, authEntry, &saveErr)) {
                    Json::Value out;
                    out["error"] = saveErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                Json::Value out(Json::objectValue);
                out["provider_id"] = id;
                out["status"] = "authorized";
                out["expires"] = expiresAtMs;
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Post});

        // POST /api/v1/providers/{id}/oauth/device/start — request device code
        drogon::app().registerHandler(
            "/api/v1/providers/{id}/oauth/device/start",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                ProviderState state;
                int providerStatusCode = 200;
                std::string providerErr;
                if (!server->m_providerManager.ValidateOAuthProvider(
                        id, false, &state, &providerStatusCode, &providerErr)) {
                    Json::Value out;
                    out["error"] = providerErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(providerStatusCode));
                    callback(resp);
                    return;
                }

                if (state.providerType != "openai-codex" && state.providerId != "openai-codex") {
                    Json::Value out;
                    out["error"] = "oauth device flow currently supported for openai-codex only";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                const auto startResult = server->m_oauthManager.StartOpenAICodexDeviceFlow(id);
                auto resp = drogon::HttpResponse::newHttpJsonResponse(startResult.body);
                resp->setStatusCode(static_cast<drogon::HttpStatusCode>(startResult.httpStatusCode));
                callback(resp);
            },
            {drogon::Post});

        // POST /api/v1/providers/{id}/oauth/device/poll — poll + exchange + persist tokens
        drogon::app().registerHandler(
            "/api/v1/providers/{id}/oauth/device/poll",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                int providerStatusCode = 200;
                std::string providerErr;
                if (!server->m_providerManager.ValidateOAuthProvider(
                        id, false, nullptr, &providerStatusCode, &providerErr)) {
                    Json::Value out;
                    out["error"] = providerErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(providerStatusCode));
                    callback(resp);
                    return;
                }

                const auto body = req->getJsonObject();
                const auto pollResult = server->m_oauthManager.PollOpenAICodexDeviceFlow(
                    id, body ? *body : Json::Value(Json::nullValue));
                if (pollResult.httpStatusCode != 200) {
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(pollResult.body);
                    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(pollResult.httpStatusCode));
                    callback(resp);
                    return;
                }

                const std::string stateValue = pollResult.body.get("status", "").asString();
                if (stateValue == "pending") {
                    callback(drogon::HttpResponse::newHttpJsonResponse(pollResult.body));
                    return;
                }

                Json::Value authEntry = pollResult.body["auth_entry"];
                const Json::Int64 expiresAtMs = pollResult.body.get("expires", 0).asInt64();

                std::string saveErr;
                if (!server->SaveAuthToDisk(id, authEntry, &saveErr)) {
                    Json::Value out;
                    out["error"] = saveErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                Json::Value out = pollResult.body;
                out.removeMember("auth_entry");
                out["expires"] = expiresAtMs;
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Post});

        // PATCH /api/v1/providers/default — set default provider
        drogon::app().registerHandler(
            "/api/v1/providers/default",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                const auto body = req->getJsonObject();
                if (!body || !body->isObject() || !body->isMember("provider_id")) {
                    Json::Value out;
                    out["error"] = "provider_id is required";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                const std::string id = (*body)["provider_id"].asString();
                std::string setDefaultErr;
                if (!server->m_providerManager.SetDefaultProviderId(id, &setDefaultErr)) {
                    Json::Value out;
                    out["error"] = setDefaultErr.empty() ? ("provider not found: " + id) : setDefaultErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }

                std::string persistErr;
                if (!server->SaveProvidersToDisk(&persistErr)) {
                    Json::Value out;
                    out["error"] = persistErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                Json::Value out(Json::objectValue);
                out["default_provider"] = id;
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Patch});


        auto serveSpaIndex =
            [](std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                const bool forceFilesystem = ShouldServeAdminUiFromFilesystem();

                if (!forceFilesystem && HasEmbeddedAdminUiAssets()) {
                    auto embeddedResponse = BuildEmbeddedAdminUiResponse("index.html", false);
                    if (embeddedResponse) {
                        callback(embeddedResponse);
                        return;
                    }
                }

                std::filesystem::path indexPath;
                if (ResolveAdminUiFilePath("index.html", &indexPath)) {
                    auto fileResponse = drogon::HttpResponse::newFileResponse(indexPath.string());
                    if (fileResponse) {
                        callback(fileResponse);
                        return;
                    }
                }

                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k200OK);
                resp->setContentTypeCode(drogon::CT_TEXT_HTML);
                resp->setBody(
                    "<!doctype html><html><head><meta charset=\"utf-8\"><title>Animus Admin</title>"
                    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                    "</head><body><h1>Animus Admin Server</h1>"
                    "<p>SPA build not found. Build the UI with <code>cd admin-ui && npm install && npm run build</code>.</p>"
                    "<p>Or enable embedded assets via CMake option <code>-DANIMUS_ADMIN_UI_EMBED=ON</code>.</p>"
                    "</body></html>");
                callback(resp);
            };

        auto serveSpaAsset =
            [](const std::string& relativePath,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                if (!IsSafeAdminAssetPath(relativePath)) {
                    auto resp = drogon::HttpResponse::newHttpResponse();
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                const bool forceFilesystem = ShouldServeAdminUiFromFilesystem();
                if (!forceFilesystem && HasEmbeddedAdminUiAssets()) {
                    auto embeddedResponse = BuildEmbeddedAdminUiResponse(relativePath, true);
                    if (embeddedResponse) {
                        callback(embeddedResponse);
                        return;
                    }
                }

                std::filesystem::path assetPath;
                if (!ResolveAdminUiFilePath(relativePath, &assetPath)) {
                    auto resp = drogon::HttpResponse::newHttpResponse();
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);

                }

                auto fileResponse = drogon::HttpResponse::newFileResponse(assetPath.string());
                if (!fileResponse) {
                    auto resp = drogon::HttpResponse::newHttpResponse();
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }
                callback(fileResponse);
            };

        drogon::app().registerHandler(
            "/",
            [serveSpaIndex](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                serveSpaIndex(std::move(callback));
            },
            {drogon::Get});

        // Wildcard SPA fallback — serve index.html for any single-segment path
        // that wasn't matched by an API handler above. This lets the Vue router
        // handle all client-side routes without needing to register each one here.
        drogon::app().registerHandler(
            "/{1}",
            [serveSpaIndex](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& pathSegment) {
                // Don't intercept API or asset routes
                if (pathSegment == "api" || pathSegment == "assets" || pathSegment == "favicon.ico") {
                    auto resp = drogon::HttpResponse::newHttpResponse();
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }
                serveSpaIndex(std::move(callback));
            },
            {drogon::Get});

        drogon::app().registerHandler(
            "/assets/{1}",
            [serveSpaAsset](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& fileName) {
                serveSpaAsset("assets/" + fileName, std::move(callback));
            },
            {drogon::Get});

        drogon::app().registerHandler(
            "/favicon.ico",
            [serveSpaAsset](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                serveSpaAsset("favicon.ico", std::move(callback));
            },
            {drogon::Get});
