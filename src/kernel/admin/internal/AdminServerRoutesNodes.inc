// ============================================================================
// AdminServer Routes — Node Management API
//
// POST   /api/v1/nodes/tokens          — generate auth token(s)
// GET    /api/v1/nodes/tokens          — list tokens (hash only)
// DELETE /api/v1/nodes/tokens/{id}     — revoke token
// GET    /api/v1/nodes                 — list connected nodes
// GET    /api/v1/nodes/{name}          — node details
// ============================================================================

{
    auto* server = g_activeAdminServer.load();

    // POST /api/v1/nodes/tokens — generate auth token
    drogon::app().registerHandler(
        "/api/v1/nodes/tokens",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            AdminServer* srv = g_activeAdminServer.load();
            if (!srv) { SendServerNotActive(callback); return; }
            if (!srv->m_nodeManager) {
                SendJsonError(callback, "node manager not available", drogon::k503ServiceUnavailable);
                return;
            }

            // Parse body for description
            std::string description = "node token";
            auto body = req->getJsonObject();
            if (body && body->isMember("description")) {
                description = (*body)["description"].asString();
            }

            std::string token = srv->m_nodeManager->GenerateToken(description);
            if (token.empty()) {
                SendJsonError(callback, "failed to generate token", drogon::k500InternalServerError);
                return;
            }

            Json::Value out(Json::objectValue);
            out["token"] = token;
            out["description"] = description;
            out["note"] = "Store this token securely. It will not be shown again.";

            callback(drogon::HttpResponse::newHttpJsonResponse(out));
        },
        {drogon::Post});

    // GET /api/v1/nodes/tokens — list tokens (masked)
    drogon::app().registerHandler(
        "/api/v1/nodes/tokens",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            AdminServer* srv = g_activeAdminServer.load();
            if (!srv) { SendServerNotActive(callback); return; }
            if (!srv->m_nodeManager) {
                SendJsonError(callback, "node manager not available", drogon::k503ServiceUnavailable);
                return;
            }

            auto tokens = srv->m_nodeManager->ListTokens();
            Json::Value out(Json::objectValue);
            Json::Value arr(Json::arrayValue);
            for (const auto& t : tokens) {
                Json::Value tObj(Json::objectValue);
                tObj["id"] = static_cast<Json::Int64>(t.id);
                tObj["token_hash"] = t.token_hash.substr(0, 8) + "...";
                tObj["description"] = t.description;
                tObj["created_at_unix_ms"] = static_cast<Json::UInt64>(t.created_at_unix_ms);
                tObj["revoked"] = t.revoked;
                arr.append(tObj);
            }
            out["tokens"] = arr;
            out["total"] = static_cast<Json::UInt>(tokens.size());

            callback(drogon::HttpResponse::newHttpJsonResponse(out));
        },
        {drogon::Get});

    // DELETE /api/v1/nodes/tokens/{id} — revoke token
    drogon::app().registerHandler(
        "/api/v1/nodes/tokens/{1}",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           const std::string& tokenIdText) {
            AdminServer* srv = g_activeAdminServer.load();
            if (!srv) { SendServerNotActive(callback); return; }
            if (!srv->m_nodeManager) {
                SendJsonError(callback, "node manager not available", drogon::k503ServiceUnavailable);
                return;
            }

            int64_t tokenId = 0;
            try { tokenId = std::stoll(tokenIdText); } catch (...) {}
            if (tokenId <= 0) {
                SendJsonError(callback, "invalid token id", drogon::k400BadRequest);
                return;
            }

            if (srv->m_nodeManager->RevokeToken(tokenId)) {
                Json::Value out(Json::objectValue);
                out["id"] = static_cast<Json::Int64>(tokenId);
                out["status"] = "revoked";
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            } else {
                SendJsonError(callback, "failed to revoke token", drogon::k500InternalServerError);
            }
        },
        {drogon::Delete});

    // GET /api/v1/nodes — list connected nodes
    drogon::app().registerHandler(
        "/api/v1/nodes",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            AdminServer* srv = g_activeAdminServer.load();
            if (!srv) { SendServerNotActive(callback); return; }
            if (!srv->m_nodeManager) {
                SendJsonError(callback, "node manager not available", drogon::k503ServiceUnavailable);
                return;
            }

            auto nodes = srv->m_nodeManager->ListNodes();
            Json::Value out(Json::objectValue);
            Json::Value arr(Json::arrayValue);
            int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            for (const auto& n : nodes) {
                Json::Value nObj(Json::objectValue);
                nObj["name"] = n.name;
                nObj["status"] = n.connected ? "connected" : "disconnected";
                nObj["hostname"] = n.hostname;
                nObj["os"] = n.os_info;
                nObj["uptime_seconds"] = static_cast<Json::UInt64>(
                    n.connected ? (nowMs - n.connected_at_unix_ms) / 1000 : 0);

                Json::Value toolsArr(Json::arrayValue);
                for (const auto& t : n.tools) toolsArr.append(t);
                nObj["tools"] = toolsArr;
                arr.append(nObj);
            }
            out["nodes"] = arr;
            out["total"] = static_cast<Json::UInt>(nodes.size());

            callback(drogon::HttpResponse::newHttpJsonResponse(out));
        },
        {drogon::Get});

    // GET /api/v1/nodes/{name} — node details
    drogon::app().registerHandler(
        "/api/v1/nodes/{1}",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           const std::string& name) {
            AdminServer* srv = g_activeAdminServer.load();
            if (!srv) { SendServerNotActive(callback); return; }
            if (!srv->m_nodeManager) {
                SendJsonError(callback, "node manager not available", drogon::k503ServiceUnavailable);
                return;
            }

            auto info = srv->m_nodeManager->GetNode(name);
            if (!info) {
                SendJsonError(callback, "node not found: " + name, drogon::k404NotFound);
                return;
            }

            int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            Json::Value out(Json::objectValue);
            out["name"] = info->name;
            out["status"] = info->connected ? "connected" : "disconnected";
            out["hostname"] = info->hostname;
            out["os"] = info->os_info;
            out["connected_at"] = static_cast<Json::UInt64>(info->connected_at_unix_ms);
            out["uptime_seconds"] = static_cast<Json::UInt64>(
                info->connected ? (nowMs - info->connected_at_unix_ms) / 1000 : 0);
            out["last_seen"] = static_cast<Json::UInt64>(info->last_seen_unix_ms);

            Json::Value toolsArr(Json::arrayValue);
            for (const auto& t : info->tools) toolsArr.append(t);
            out["tools"] = toolsArr;

            callback(drogon::HttpResponse::newHttpJsonResponse(out));
        },
        {drogon::Get});
}
