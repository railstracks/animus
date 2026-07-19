        // ===================================================================
        // Diffusion Provider API (admin only)
        // ===================================================================

        // GET /api/v1/diffusion/providers — list all providers
        drogon::app().registerHandler(
            "/api/v1/diffusion/providers",
            [this](const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                auto providers = m_diffusionStore->ListProviders();
                Json::Value body(Json::arrayValue);
                for (const auto& p : providers) {
                    Json::Value obj;
                    obj["id"] = p.id;
                    obj["type"] = p.type;
                    obj["base_url"] = p.base_url;
                    obj["api_key"] = p.api_key;
                    obj["default_model"] = p.default_model;
                    obj["default_aspect_ratio"] = p.default_aspect_ratio;
                    obj["created_at"] = static_cast<Json::Int64>(p.created_at);
                    obj["updated_at"] = static_cast<Json::Int64>(p.updated_at);
                    body.append(obj);
                }
                callback(drogon::HttpResponse::newHttpJsonResponse(body));
            },
            {drogon::Get, drogon::Options});

        // POST /api/v1/diffusion/providers — create provider
        drogon::app().registerHandler(
            "/api/v1/diffusion/providers",
            [this](const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                auto jsonBody = req->getJsonObject();
                if (!jsonBody) {
                    Json::Value err;
                    err["error"] = "Invalid request body";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                DiffusionProviderConfig config;
                config.id = jsonBody->get("id", "").asString();
                config.type = jsonBody->get("type", "").asString();
                config.base_url = jsonBody->get("base_url", "").asString();
                config.api_key = jsonBody->get("api_key", "").asString();
                config.default_model = jsonBody->get("default_model", "").asString();
                config.default_aspect_ratio = jsonBody->get("default_aspect_ratio", "").asString();
                config.created_at = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                config.updated_at = config.created_at;

                if (config.id.empty() || config.type.empty() || config.base_url.empty()) {
                    Json::Value err;
                    err["error"] = "id, type, and base_url are required";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                if (!m_diffusionStore->CreateProvider(config)) {
                    Json::Value err;
                    err["error"] = "Failed to create provider (id may already exist)";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                Json::Value body;
                body["status"] = "ok";
                body["id"] = config.id;
                callback(drogon::HttpResponse::newHttpJsonResponse(body));
            },
            {drogon::Post, drogon::Options});

        // PUT /api/v1/diffusion/providers/:id — update provider
        drogon::app().registerHandler(
            "/api/v1/diffusion/providers/{id}",
            [this](const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id) {
                auto jsonBody = req->getJsonObject();
                if (!jsonBody) {
                    Json::Value err;
                    err["error"] = "Invalid request body";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                auto existing = m_diffusionStore->GetProvider(id);
                if (!existing) {
                    Json::Value err;
                    err["error"] = "Provider not found";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }

                DiffusionProviderConfig config = *existing;
                config.id = id;
                if (jsonBody->isMember("type")) config.type = (*jsonBody)["type"].asString();
                if (jsonBody->isMember("base_url")) config.base_url = (*jsonBody)["base_url"].asString();
                if (jsonBody->isMember("api_key")) config.api_key = (*jsonBody)["api_key"].asString();
                if (jsonBody->isMember("default_model")) config.default_model = (*jsonBody)["default_model"].asString();
                if (jsonBody->isMember("default_aspect_ratio")) config.default_aspect_ratio = (*jsonBody)["default_aspect_ratio"].asString();
                config.updated_at = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();

                if (!m_diffusionStore->UpdateProvider(config)) {
                    Json::Value err;
                    err["error"] = "Failed to update provider";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                Json::Value body;
                body["status"] = "ok";
                callback(drogon::HttpResponse::newHttpJsonResponse(body));
            },
            {drogon::Put, drogon::Options});

        // DELETE /api/v1/diffusion/providers/:id — delete provider
        drogon::app().registerHandler(
            "/api/v1/diffusion/providers/{id}",
            [this](const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id) {
                if (!m_diffusionStore->DeleteProvider(id)) {
                    Json::Value err;
                    err["error"] = "Provider not found or delete failed";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }

                Json::Value body;
                body["status"] = "ok";
                callback(drogon::HttpResponse::newHttpJsonResponse(body));
            },
            {drogon::Delete, drogon::Options});

        // GET /api/v1/diffusion/providers/:id/models — list models from provider
        drogon::app().registerHandler(
            "/api/v1/diffusion/providers/{id}/models",
            [this](const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id) {
                auto config = m_diffusionStore->GetProvider(id);
                if (!config) {
                    Json::Value err;
                    err["error"] = "Provider not found";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }

                // Create provider instance and list models
                // We need the HttpClient — use the static one from kernel
                // For now, return models via the tool's provider factory
                Json::Value body(Json::arrayValue);
                // TODO: create provider instance and call ListModels()
                // For now, return empty — the admin UI will call the tool's list_models action
                callback(drogon::HttpResponse::newHttpJsonResponse(body));
            },
            {drogon::Get, drogon::Options});