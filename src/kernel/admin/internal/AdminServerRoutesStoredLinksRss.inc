// ============================================================================
// AdminServer Routes — Stored Links & RSS Feeds Configuration API
//
// GET    /api/v1/stored-links          — list configured stored links
// PUT    /api/v1/stored-links          — replace stored links config
// GET    /api/v1/rss-feeds             — list configured RSS feeds
// PUT    /api/v1/rss-feeds             — replace RSS feeds config
// ============================================================================

{
    auto* server = g_activeAdminServer.load();

    // GET /api/v1/stored-links
    drogon::app().registerHandler(
        "/api/v1/stored-links",
        [server](const drogon::HttpRequestPtr&,
                  std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            AdminServer* srv = g_activeAdminServer.load();
            if (!srv) { SendServerNotActive(callback); return; }

            Json::Value resp(Json::arrayValue);
            if (srv->m_configStore) {
                std::string persisted = srv->m_configStore->Get("__kernel__", "stored_links");
                if (!persisted.empty()) {
                    Json::Value linksJson;
                    Json::CharReaderBuilder reader;
                    std::istringstream ss(persisted);
                    std::string errs;
                    if (Json::parseFromStream(reader, ss, &linksJson, &errs) && linksJson.isArray()) {
                        resp = linksJson;
                    }
                }
            }
            callback(drogon::HttpResponse::newHttpJsonResponse(resp));
        },
        {drogon::Get});

    // PUT /api/v1/stored-links
    drogon::app().registerHandler(
        "/api/v1/stored-links",
        [server](const drogon::HttpRequestPtr& req,
                  std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            AdminServer* srv = g_activeAdminServer.load();
            if (!srv) { SendServerNotActive(callback); return; }
            if (!srv->m_configStore) {
                SendJsonError(callback, "config store not available", drogon::k503ServiceUnavailable);
                return;
            }

            auto body = req->getJsonObject();
            if (!body || !(*body).isArray()) {
                SendJsonError(callback, "request body must be a JSON array of {id, label, url}", drogon::k400BadRequest);
                return;
            }

            // Validate entries
            for (const auto& item : *body) {
                if (!item.isMember("id") || !item.isMember("url") ||
                    item["id"].asString().empty() || item["url"].asString().empty()) {
                    SendJsonError(callback, "Each entry must have non-empty 'id' and 'url'", drogon::k400BadRequest);
                    return;
                }
            }

            Json::StreamWriterBuilder writer;
            srv->m_configStore->Set("__kernel__", "stored_links",
                                    Json::writeString(writer, *body));

            Json::Value resp;
            resp["status"] = "ok";
            resp["warning"] = "Restart the kernel for stored_links tool to pick up the new configuration.";
            callback(drogon::HttpResponse::newHttpJsonResponse(resp));
        },
        {drogon::Put});

    // GET /api/v1/rss-feeds
    drogon::app().registerHandler(
        "/api/v1/rss-feeds",
        [server](const drogon::HttpRequestPtr&,
                  std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            AdminServer* srv = g_activeAdminServer.load();
            if (!srv) { SendServerNotActive(callback); return; }

            Json::Value resp(Json::arrayValue);
            if (srv->m_configStore) {
                std::string persisted = srv->m_configStore->Get("__kernel__", "rss_feeds");
                if (!persisted.empty()) {
                    Json::Value feedsJson;
                    Json::CharReaderBuilder reader;
                    std::istringstream ss(persisted);
                    std::string errs;
                    if (Json::parseFromStream(reader, ss, &feedsJson, &errs) && feedsJson.isArray()) {
                        resp = feedsJson;
                    }
                }
            }
            callback(drogon::HttpResponse::newHttpJsonResponse(resp));
        },
        {drogon::Get});

    // PUT /api/v1/rss-feeds
    drogon::app().registerHandler(
        "/api/v1/rss-feeds",
        [server](const drogon::HttpRequestPtr& req,
                  std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            AdminServer* srv = g_activeAdminServer.load();
            if (!srv) { SendServerNotActive(callback); return; }
            if (!srv->m_configStore) {
                SendJsonError(callback, "config store not available", drogon::k503ServiceUnavailable);
                return;
            }

            auto body = req->getJsonObject();
            if (!body || !(*body).isArray()) {
                SendJsonError(callback, "request body must be a JSON array of {id, label, url}", drogon::k400BadRequest);
                return;
            }

            // Validate entries
            for (const auto& item : *body) {
                if (!item.isMember("id") || !item.isMember("url") ||
                    item["id"].asString().empty() || item["url"].asString().empty()) {
                    SendJsonError(callback, "Each entry must have non-empty 'id' and 'url'", drogon::k400BadRequest);
                    return;
                }
            }

            Json::StreamWriterBuilder writer;
            srv->m_configStore->Set("__kernel__", "rss_feeds",
                                    Json::writeString(writer, *body));

            Json::Value resp;
            resp["status"] = "ok";
            resp["warning"] = "Restart the kernel for rss tool to pick up the new configuration.";
            callback(drogon::HttpResponse::newHttpJsonResponse(resp));
        },
        {drogon::Put});
}