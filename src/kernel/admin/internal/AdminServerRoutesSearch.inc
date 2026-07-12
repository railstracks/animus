// ============================================================================
// AdminServer Routes — Web Search Configuration API
//
// GET    /api/v1/search/config    — get current search config (masked key)
// PUT    /api/v1/search/config    — update search config
// POST   /api/v1/search/test      — run a test query
// ============================================================================

{
    auto* server = g_activeAdminServer.load();

    // Helper to mask API key for GET responses.
    auto maskKey = [](const std::string& key) -> std::string {
        if (key.empty()) return "";
        if (key.size() <= 4U) return "****";
        return key.substr(0, 4) + "...****";
    };

    // GET /api/v1/search/config
    drogon::app().registerHandler(
        "/api/v1/search/config",
        [server, maskKey](const drogon::HttpRequestPtr&,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            AdminServer* srv = g_activeAdminServer.load();
            if (!srv) { SendServerNotActive(callback); return; }

            Json::Value body;
            if (srv->m_searchConfig) {
                body["provider"] = srv->m_searchConfig->provider;
                body["api_key"] = maskKey(srv->m_searchConfig->api_key);
                body["api_key_set"] = !srv->m_searchConfig->api_key.empty();
                body["endpoint"] = srv->m_searchConfig->endpoint;
                body["default_count"] = srv->m_searchConfig->default_count;
                body["rate_limit_per_minute"] = srv->m_searchConfig->rate_limit_per_minute;
            } else {
                body["provider"] = "";
                body["api_key"] = "";
                body["api_key_set"] = false;
                body["endpoint"] = "";
                body["default_count"] = 5;
                body["rate_limit_per_minute"] = 10;
            }
            callback(drogon::HttpResponse::newHttpJsonResponse(body));
        },
        {drogon::Get});

    // PUT /api/v1/search/config
    drogon::app().registerHandler(
        "/api/v1/search/config",
        [server](const drogon::HttpRequestPtr& req,
                  std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            AdminServer* srv = g_activeAdminServer.load();
            if (!srv) { SendServerNotActive(callback); return; }
            if (!srv->m_searchConfig) {
                SendJsonError(callback, "search config not available", drogon::k503ServiceUnavailable);
                return;
            }

            auto body = req->getJsonObject();
            if (!body) {
                SendJsonError(callback, "request body must be JSON", drogon::k400BadRequest);
                return;
            }

            // Update fields if present in the request.
            if ((*body).isMember("provider") && (*body)["provider"].isString()) {
                srv->m_searchConfig->provider = (*body)["provider"].asString();
            }
            // Only update api_key if it's not the masked version.
            if ((*body).isMember("api_key") && (*body)["api_key"].isString()) {
                std::string newKey = (*body)["api_key"].asString();
                // Skip masked values (they contain "...")
                if (newKey.find("...") == std::string::npos && !newKey.empty()) {
                    srv->m_searchConfig->api_key = newKey;
                } else if (newKey.empty()) {
                    srv->m_searchConfig->api_key.clear();
                }
            }
            if ((*body).isMember("endpoint") && (*body)["endpoint"].isString()) {
                srv->m_searchConfig->endpoint = (*body)["endpoint"].asString();
            }
            if ((*body).isMember("default_count") && (*body)["default_count"].isInt()) {
                srv->m_searchConfig->default_count = (*body)["default_count"].asInt();
            }
            if ((*body).isMember("rate_limit_per_minute") && (*body)["rate_limit_per_minute"].isInt()) {
                srv->m_searchConfig->rate_limit_per_minute = (*body)["rate_limit_per_minute"].asInt();
            }

            // Persist into agent config store under a reserved kernel scope
            // so settings survive restarts.
            if (srv->m_configStore) {
                Json::Value searchCfg(Json::objectValue);
                searchCfg["provider"] = srv->m_searchConfig->provider;
                searchCfg["api_key"] = srv->m_searchConfig->api_key;
                searchCfg["endpoint"] = srv->m_searchConfig->endpoint;
                searchCfg["default_count"] = srv->m_searchConfig->default_count;
                searchCfg["rate_limit_per_minute"] = srv->m_searchConfig->rate_limit_per_minute;
                Json::StreamWriterBuilder writer;
                srv->m_configStore->Set("__kernel__", "search_config",
                                        Json::writeString(writer, searchCfg));
            }

            Json::Value resp;
            resp["status"] = "ok";

            // Note: tool registration happens at kernel startup.
            // A kernel restart is needed for config changes to take effect on the tool.
            if (srv->m_toolRegistry && !srv->m_toolRegistry->Has("web_search")) {
                resp["warning"] = "Search config updated. Restart the kernel to activate the web_search tool.";
            }

            callback(drogon::HttpResponse::newHttpJsonResponse(resp));
        },
        {drogon::Put});

    // POST /api/v1/search/test
    drogon::app().registerHandler(
        "/api/v1/search/test",
        [server](const drogon::HttpRequestPtr& req,
                  std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            AdminServer* srv = g_activeAdminServer.load();
            if (!srv) { SendServerNotActive(callback); return; }
            if (!srv->m_searchConfig) {
                SendJsonError(callback, "search config not available", drogon::k503ServiceUnavailable);
                return;
            }
            if (!srv->m_httpClientPtr) {
                SendJsonError(callback, "HTTP client not available", drogon::k503ServiceUnavailable);
                return;
            }

            auto body = req->getJsonObject();
            std::string query = "test query";
            if (body && (*body).isMember("query") && (*body)["query"].isString()) {
                query = (*body)["query"].asString();
            }

            if (srv->m_searchConfig->api_key.empty()) {
                SendJsonError(callback, "No API key configured. Set the API key first.", drogon::k400BadRequest);
                return;
            }

            // Build and execute a Brave search request directly.
            BraveSearchProvider::Config braveCfg;
            braveCfg.api_key = srv->m_searchConfig->api_key;
            braveCfg.rate_limit_per_minute = srv->m_searchConfig->rate_limit_per_minute > 0
                                                ? srv->m_searchConfig->rate_limit_per_minute : 10;

            BraveSearchProvider provider(*srv->m_httpClientPtr, std::move(braveCfg));

            SearchQuery sq;
            sq.query = query;
            sq.count = 3;

            std::string searchError;
            auto results = provider.Search(sq, searchError);

            Json::Value resp;
            if (!searchError.empty()) {
                resp["status"] = "error";
                resp["error"] = searchError;
                auto httpResp = drogon::HttpResponse::newHttpJsonResponse(resp);
                httpResp->setStatusCode(drogon::k502BadGateway);
                callback(httpResp);
                return;
            }

            resp["status"] = "ok";
            resp["query"] = query;
            resp["count"] = static_cast<Json::UInt>(results.size());

            Json::Value resultsArray(Json::arrayValue);
            for (const auto& sr : results) {
                Json::Value item(Json::objectValue);
                item["title"] = sr.title;
                item["url"] = sr.url;
                item["snippet"] = sr.snippet;
                resultsArray.append(item);
            }
            resp["results"] = resultsArray;

            callback(drogon::HttpResponse::newHttpJsonResponse(resp));
        },
        {drogon::Post});
}
