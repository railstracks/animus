// ============================================================================
// AdminServer Routes — Node Federation Sync API (#78 P1c)
//
// GET  /api/v1/sync/handshake — identity + protocol + synced-table list
// GET  /api/v1/sync/outbox    — pull changes: ?since=<cursor>&limit=<n>
// GET  /api/v1/sync/status    — local view of peers (outbound + incoming)
//
// Auth: rides the standard bearer-token filter like every other /api route.
// A peer's pull carries X-Animus-Node (its node id) — the outbox handler
// records it as incoming liveness on the local PeerSyncService (if
// running), which is the "they are alive" half of the membership
// heartbeat; our own pulls of them are the other half.
// ============================================================================

{
    auto* server = g_activeAdminServer.load();

    // GET /api/v1/sync/handshake — who we are, what we speak, what we sync.
    drogon::app().registerHandler(
        "/api/v1/sync/handshake",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            AdminServer* srv = g_activeAdminServer.load();
            if (!srv) { SendServerNotActive(callback); return; }
            if (!srv->m_syncStore) {
                SendJsonError(callback, "sync store not available",
                              drogon::k503ServiceUnavailable);
                return;
            }
            Json::Value out(Json::objectValue);
            out["protocol"] = PeerSyncService::kProtocol;
            out["node_id"] = Json::UInt64(srv->m_syncStore->LocalNodeId());
            Json::Value tables(Json::arrayValue);
            for (const auto& t : srv->m_syncStore->SyncedTables()) {
                tables.append(t);
            }
            out["tables"] = tables;
            out["max_outbox_id"] = Json::Int64(srv->m_syncStore->MaxOutboxId());
            // Anti-entropy: per-table digests let a puller detect divergence
            // the outbox can't explain (restored datadir with stale cursor).
            Json::Value digests(Json::arrayValue);
            for (const auto& d : srv->m_syncStore->TableDigests()) {
                Json::Value j(Json::objectValue);
                j["table"] = d.table;
                j["count"] = Json::Int64(d.count);
                j["max_id"] = Json::Int64(d.maxId);
                j["sum_last_ms"] = Json::Int64(d.sumLastMs);
                digests.append(j);
            }
            out["digests"] = digests;
            callback(drogon::HttpResponse::newHttpJsonResponse(out));
        },
        {drogon::Get});

    // GET /api/v1/sync/outbox?since&limit — ordered change log for pullers.
    drogon::app().registerHandler(
        "/api/v1/sync/outbox",
        [server](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            AdminServer* srv = g_activeAdminServer.load();
            if (!srv) { SendServerNotActive(callback); return; }
            if (!srv->m_syncStore) {
                SendJsonError(callback, "sync store not available",
                              drogon::k503ServiceUnavailable);
                return;
            }

            // Incoming liveness: record the puller before serving.
            const std::string pullerHdr = req->getHeader("X-Animus-Node");
            if (srv->m_peerSyncService && !pullerHdr.empty()) {
                char* end = nullptr;
                const unsigned long long puller =
                    std::strtoull(pullerHdr.c_str(), &end, 10);
                if (end && *end == '\0' && puller != 0) {
                    srv->m_peerSyncService->RecordIncomingPull(
                        puller, req->getHeader("X-Animus-Url"));
                }
            }

            int64_t since = 0;
            {
                const std::string s = req->getParameter("since");
                if (!s.empty()) since = std::strtoll(s.c_str(), nullptr, 10);
            }
            int64_t limit = 500;
            {
                const std::string l = req->getParameter("limit");
                if (!l.empty()) limit = std::strtoll(l.c_str(), nullptr, 10);
            }
            if (limit < 1) limit = 1;
            if (limit > 5000) limit = 5000;  // body-size guard

            const auto records = srv->m_syncStore->FetchOutboxSince(since, limit);
            const int64_t maxId = srv->m_syncStore->MaxOutboxId();

            Json::Value out(Json::objectValue);
            out["node_id"] = Json::UInt64(srv->m_syncStore->LocalNodeId());
            Json::Value arr(Json::arrayValue);
            int64_t lastId = since;
            for (const auto& r : records) {
                Json::Value j(Json::objectValue);
                j["outbox_id"] = Json::Int64(r.outbox_id);
                j["origin_node"] = Json::Int64(r.origin_node);
                j["table_name"] = r.table_name;
                j["row_id"] = r.row_key;
                j["op"] = r.op;
                j["payload"] = r.payload;   // raw full-row JSON string
                j["unix_ms"] = Json::Int64(r.unix_ms);
                arr.append(j);
                lastId = r.outbox_id;
            }
            out["records"] = arr;
            out["max_id"] = Json::Int64(maxId);
            out["has_more"] = (lastId < maxId);
            callback(drogon::HttpResponse::newHttpJsonResponse(out));
        },
        {drogon::Get});

    // GET /api/v1/sync/status — outbound peer states + incoming pulls.
    drogon::app().registerHandler(
        "/api/v1/sync/status",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            AdminServer* srv = g_activeAdminServer.load();
            if (!srv) { SendServerNotActive(callback); return; }
            if (!srv->m_syncStore) {
                SendJsonError(callback, "sync store not available",
                              drogon::k503ServiceUnavailable);
                return;
            }
            Json::Value out(Json::objectValue);
            out["node_id"] = Json::UInt64(srv->m_syncStore->LocalNodeId());
            out["protocol"] = PeerSyncService::kProtocol;
            out["max_outbox_id"] = Json::Int64(srv->m_syncStore->MaxOutboxId());
            // Local digests for operator eyeballing / debugging heal
            // decisions: same shape the handshake reports to peers.
            Json::Value digests(Json::arrayValue);
            for (const auto& d : srv->m_syncStore->TableDigests()) {
                Json::Value j(Json::objectValue);
                j["table"] = d.table;
                j["count"] = Json::Int64(d.count);
                j["max_id"] = Json::Int64(d.maxId);
                j["sum_last_ms"] = Json::Int64(d.sumLastMs);
                digests.append(j);
            }
            out["digests"] = digests;
            if (srv->m_peerSyncService) {
                out["peer_sync"] = srv->m_peerSyncService->StatusJson();
            } else {
                out["peer_sync"] = Json::Value(Json::objectValue);
                out["peer_sync"]["running"] = false;
                out["peer_sync"]["note"] = "no peers configured (single-node)";
            }
            callback(drogon::HttpResponse::newHttpJsonResponse(out));
        },
        {drogon::Get});

    // ------------------------------------------------------------------
    // Federation identity + peer public-key exchange (#182, #273)
    // ------------------------------------------------------------------
    // GET /api/v1/sync/identity — this node's id + X25519 public key.
    drogon::app().registerHandler(
        "/api/v1/sync/identity",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            (void)req;
            AdminServer* srv = g_activeAdminServer.load();
            if (!srv) { SendServerNotActive(callback); return; }
            if (!srv->m_nodeIdentity) {
                SendJsonError(callback, "node identity not available (load failed at boot?)",
                              drogon::k503ServiceUnavailable);
                return;
            }
            Json::Value out(Json::objectValue);
            if (srv->m_syncStore)
                out["node_id"] = Json::UInt64(srv->m_syncStore->LocalNodeId());
            out["public_key"] = BytesToHex(srv->m_nodeIdentity->public_key.data(), 32);
            callback(drogon::HttpResponse::newHttpJsonResponse(out));
        },
        {drogon::Get});

    // GET /api/v1/sync/peers/keys — all ingested peer public keys.
    drogon::app().registerHandler(
        "/api/v1/sync/peers/keys",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            (void)req;
            AdminServer* srv = g_activeAdminServer.load();
            if (!srv) { SendServerNotActive(callback); return; }
            if (!srv->m_configStore) {
                SendJsonError(callback, "config store not available",
                              drogon::k503ServiceUnavailable);
                return;
            }
            Json::Value list(Json::arrayValue);
            const std::string prefix = "federation.node_keys.";
            for (const auto& [key, value] : srv->m_configStore->GetAll("")) {
                if (key.rfind(prefix, 0) != 0) continue;
                Json::Value j(Json::objectValue);
                j["node_id"] = key.substr(prefix.size());
                j["public_key"] = value;
                list.append(j);
            }
            Json::Value out(Json::objectValue);
            out["peers"] = list;
            callback(drogon::HttpResponse::newHttpJsonResponse(out));
        },
        {drogon::Get});

    // POST /api/v1/sync/peers/keys — ingest one peer public key
    // body: { "node_id": "...", "public_key": "<64 hex>" }.
    drogon::app().registerHandler(
        "/api/v1/sync/peers/keys",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            AdminServer* srv = g_activeAdminServer.load();
            if (!srv) { SendServerNotActive(callback); return; }
            if (!srv->m_configStore) {
                SendJsonError(callback, "config store not available",
                              drogon::k503ServiceUnavailable);
                return;
            }
            auto body = req->getJsonObject();
            if (!body || !body->isMember("node_id") || !body->isMember("public_key")) {
                SendJsonError(callback, "body must hold node_id and public_key",
                              drogon::k400BadRequest);
                return;
            }
            const std::string nodeId = (*body)["node_id"].asString();
            const std::string pubHex = (*body)["public_key"].asString();
            std::array<unsigned char, 32> pub{};
            if (nodeId.empty() || !HexToBytes32(pubHex, pub)) {
                SendJsonError(callback,
                              "node_id must be non-empty; public_key must be 64 hex chars",
                              drogon::k400BadRequest);
                return;
            }
            std::string err;
            if (!StorePeerNodeKey(*srv->m_configStore, nodeId, pub, err)) {
                SendJsonError(callback, err, drogon::k400BadRequest);
                return;
            }
            Json::Value out(Json::objectValue);
            out["stored"] = true;
            out["node_id"] = nodeId;
            callback(drogon::HttpResponse::newHttpJsonResponse(out));
        },
        {drogon::Post});
}
