class AdminChatWebSocketController
    : public drogon::WebSocketController<AdminChatWebSocketController, false> {
public:
    struct ConnectionContext {
        std::uint64_t connectionId{0};
        std::string activeSessionId;
        std::shared_ptr<std::atomic<bool>> stopRequested{
            std::make_shared<std::atomic<bool>>(false)};
    };

    void handleNewMessage(
        const drogon::WebSocketConnectionPtr& wsConnPtr,
        std::string&& message,
        const drogon::WebSocketMessageType& type) override {
        if (type == drogon::WebSocketMessageType::Ping
            || type == drogon::WebSocketMessageType::Pong
            || type == drogon::WebSocketMessageType::Close) {
            return;
        }
        if (type != drogon::WebSocketMessageType::Text) {
            SendError(wsConnPtr, "unsupported websocket message type");
            return;
        }

        AdminServer* server = g_activeAdminServer.load();
        if (!server) {
            SendError(wsConnPtr, "admin server not active");
            return;
        }

        if (message.size() > server->m_config.websocketMaxMessageBytes) {
            SendError(wsConnPtr, "message exceeds websocket size limit");
            return;
        }

        Json::Value payload;
        std::string parseError;
        if (!ParseJsonMessage(message, &payload, &parseError)) {
            SendError(wsConnPtr, "invalid json payload: " + parseError);
            return;
        }

        const std::string typeName = payload.get("type", "").asString();
        if (typeName == "list_sessions") {
            std::size_t limit = static_cast<std::size_t>(
                payload.get("limit", 50).asInt64());
            std::size_t offset = static_cast<std::size_t>(
                payload.get("offset", 0).asInt64());
            std::string search = payload.get("search", "").asString();
            SendSessions(server, wsConnPtr, offset, limit, search);
            return;
        }
        if (typeName == "stop") {
            std::string sessionId;
            if (auto context = wsConnPtr->getContext<ConnectionContext>()) {
                if (context->stopRequested) {
                    context->stopRequested->store(true);
                }
                sessionId = context->activeSessionId;
            }
            Json::Value stopped(Json::objectValue);
            stopped["type"] = "stopped";
            if (!sessionId.empty()) {
                stopped["session_id"] = sessionId;
            }
            SendJson(wsConnPtr, stopped);
            return;
        }
        if (typeName != "message") {
            SendError(wsConnPtr, "unsupported message type");
            return;
        }

        std::string userContent;
        if (payload.isMember("content") && payload["content"].isString()) {
            userContent = payload["content"].asString();
        }
        if (userContent.empty()) {
            SendError(wsConnPtr, "content must be a non-empty string");
            return;
        }

        std::string requestedProviderOverride;
        std::string requestedModelOverride;
        if (payload.isMember("provider")) {
            if (!payload["provider"].isString()) {
                SendError(wsConnPtr, "provider must be a string");
                return;
            }
            requestedProviderOverride = payload["provider"].asString();
        }
        if (payload.isMember("model_id")) {
            if (!payload["model_id"].isString()) {
                SendError(wsConnPtr, "model_id must be a string");
                return;
            }
            requestedModelOverride = payload["model_id"].asString();
        }

        SessionId sessionId = 0;
        std::shared_ptr<Session> session;
        std::string requestedAgentId = "default";
        if (payload.isMember("session_id")) {
            std::string sessionIdText;
            if (payload["session_id"].isString()) {
                sessionIdText = payload["session_id"].asString();
            } else if (payload["session_id"].isUInt64()) {
                sessionIdText = std::to_string(payload["session_id"].asUInt64());
            } else {
                SendError(wsConnPtr, "session_id must be a string or unsigned integer");
                return;
            }

            if (!ParseSessionId(sessionIdText, &sessionId)) {
                SendError(wsConnPtr, "session_id is invalid");
                return;
            }
            session = server->m_sessions ? server->m_sessions->GetById(sessionId) : nullptr;
            if (!session) {
                SendError(wsConnPtr, "session_id not found");
                return;
            }
            std::cerr << "[chat] resolved session_id=" << sessionId
                      << " (conv_id=" << session->Key().conversation_id
                      << ", turns=" << session->Turns().size() << ")" << std::endl;
        } else {
            if (payload.isMember("agent_id")) {
                if (!payload["agent_id"].isString()) {
                    SendError(wsConnPtr, "agent_id must be a string");
                    return;
                }
                requestedAgentId = payload["agent_id"].asString();
                if (requestedAgentId.empty()) {
                    SendError(wsConnPtr, "agent_id must be non-empty");
                    return;
                }
            }

            const std::uint64_t conversationId = server->m_nextWsConversationId.fetch_add(1);
            const std::string sessionSource = payload.get("source", "ws_chat").asString();
            SessionKey key{
                sessionSource,
                sessionSource + "-conversation-" + std::to_string(conversationId),
                "conn-" + std::to_string(GetConnectionId(wsConnPtr))};
            session = server->m_sessions ? server->m_sessions->GetOrCreate(key) : nullptr;
            if (!session) {
                SendError(wsConnPtr, "session system unavailable");
                return;
            }
            if (server->m_agentStore && !server->m_agentStore->GetById(requestedAgentId).has_value()) {
                SendError(wsConnPtr, "agent_id not found: " + requestedAgentId);
                return;
            }
            session->SetAgentId(requestedAgentId);
            sessionId = session->Id();
            std::cerr << "[chat] created new session_id=" << sessionId
                      << " (conv_id=" << key.conversation_id
                      << ", thread=" << key.thread_id
                      << ", agent_id=" << requestedAgentId << ")" << std::endl;
        }

        if (!session) {
            SendError(wsConnPtr, "session system unavailable");
            return;
        }

        // User turn is added by echo fallback or ChainRunner below

        KernelConfig::MemoryObservation capturedObservation =
            server->m_memoryManager.CaptureObservation(userContent, {"ws_chat", "captured"});
        std::string persistError;
        (void)server->SaveMemoryToDisk(&persistError);
        server->m_observationStreamHub.Broadcast(capturedObservation);

        if (auto context = wsConnPtr->getContext<ConnectionContext>()) {
            context->activeSessionId = std::to_string(sessionId);
            if (context->stopRequested) {
                context->stopRequested->store(false);
            }
        }

        std::shared_ptr<std::atomic<bool>> stopSignal;
        if (auto context = wsConnPtr->getContext<ConnectionContext>()) {
            stopSignal = context->stopRequested;
        }

        Json::Value contextMsg(Json::objectValue);
        contextMsg["type"] = "context";
        contextMsg["session_id"] = std::to_string(sessionId);
        Json::Value layers(Json::arrayValue);
        for (const auto& layerName : server->m_memoryManager.ListLayerNames()) {
            layers.append(layerName);
        }
        contextMsg["layers"] = layers;
        contextMsg["tools"] = Json::Value(Json::arrayValue);
        SendJson(wsConnPtr, contextMsg);

        ChatSessionService::Request request;
        request.wsConnection = wsConnPtr;
        request.session = session;
        request.sessionId = sessionId;
        request.userContent = userContent;
        request.stopSignal = stopSignal;
        request.requestedProviderOverride = requestedProviderOverride;
        request.requestedModelOverride = requestedModelOverride;

        if (!server->m_chatSessionService.EnqueueStreamingResponse(request)) {
            SendError(wsConnPtr, "job system not available");
            return;
        }
    }

    void handleConnectionClosed(const drogon::WebSocketConnectionPtr&) override {
    }

    void handleNewConnection(
        const drogon::HttpRequestPtr& req,
        const drogon::WebSocketConnectionPtr& wsConnPtr) override {
        AdminServer* server = g_activeAdminServer.load();
        if (!server) {
            SendError(wsConnPtr, "admin server not active");
            wsConnPtr->shutdown();
            return;
        }

        if (!IsAuthorizedConnection(*server, req, wsConnPtr)) {
            SendError(wsConnPtr, "unauthorized");
            wsConnPtr->shutdown();
            return;
        }

        ConnectionContext context;
        context.connectionId = server->m_nextWsConnectionId.fetch_add(1);
        wsConnPtr->setContext(std::make_shared<ConnectionContext>(std::move(context)));
    }

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/ws/chat", drogon::Get);
    WS_PATH_LIST_END

private:
    static bool ParseJsonMessage(
        const std::string& message,
        Json::Value* out,
        std::string* error) {
        if (!out) {
            if (error) {
                *error = "internal parser output is null";
            }
            return false;
        }
        Json::CharReaderBuilder readerBuilder;
        std::istringstream in(message);
        std::string parseErrors;
        if (!Json::parseFromStream(readerBuilder, in, out, &parseErrors)) {
            if (error) {
                *error = parseErrors;
            }
            return false;
        }
        if (!out->isObject()) {
            if (error) {
                *error = "payload must be a json object";
            }
            return false;
        }
        return true;
    }

    static std::string JsonToString(const Json::Value& value) {
        Json::StreamWriterBuilder writerBuilder;
        writerBuilder["indentation"] = "";
        return Json::writeString(writerBuilder, value);
    }

    static void SendJson(
        const drogon::WebSocketConnectionPtr& conn,
        const Json::Value& payload) {
        if (!conn || !conn->connected()) {
            return;
        }
        conn->send(JsonToString(payload));
    }

    static void SendError(
        const drogon::WebSocketConnectionPtr& conn,
        const std::string& message) {
        Json::Value error(Json::objectValue);
        error["type"] = "error";
        error["message"] = message;
        SendJson(conn, error);
    }

    static void SendSessions(
        AdminServer* server,
        const drogon::WebSocketConnectionPtr& conn,
        std::size_t offset = 0,
        std::size_t limit = 50,
        const std::string& search = "") {
        Json::Value payload(Json::objectValue);
        payload["type"] = "sessions";
        Json::Value data(Json::arrayValue);
        std::size_t total = 0;
        if (server && server->m_sessions) {
            auto page = server->m_sessions->ListSessionsPaginated(offset, limit, search);
            total = page.total;
            for (const auto& session : page.sessions) {
                if (!session) {
                    continue;
                }
                data.append(BuildSessionSummaryJson(*session));
            }
        }
        payload["data"] = data;
        payload["total"] = static_cast<Json::UInt64>(total);
        payload["offset"] = static_cast<Json::UInt64>(offset);
        payload["limit"] = static_cast<Json::UInt64>(limit);
        SendJson(conn, payload);
    }

    static bool IsLoopbackIp(const std::string& ip) {
        return ip == "127.0.0.1" || ip == "::1" || ip == "::ffff:127.0.0.1";
    }

    static bool IsAuthorizedConnection(
        const AdminServer& server,
        const drogon::HttpRequestPtr& req,
        const drogon::WebSocketConnectionPtr& conn) {
        if (server.m_config.authToken.empty()) {
            return true;
        }

        const std::string providedToken = req->getParameter("token");
        if (!providedToken.empty() && providedToken == server.m_config.authToken) {
            return true;
        }

        const std::string authHeader = req->getHeader("Authorization");
        static const std::string kBearerPrefix = "Bearer ";
        if (authHeader.rfind(kBearerPrefix, 0) == 0) {
            const std::string token = authHeader.substr(kBearerPrefix.size());
            if (!token.empty() && token == server.m_config.authToken) {
                return true;
            }
        }

        if (server.m_config.allowUnauthenticatedLocalhost && conn) {
            return IsLoopbackIp(conn->peerAddr().toIp());
        }
        return false;
    }

    static std::uint64_t GetConnectionId(const drogon::WebSocketConnectionPtr& conn) {
        auto context = conn->getContext<ConnectionContext>();
        if (!context) {
            return 0;
        }
        return context->connectionId;
    }
};

class AdminObservationWebSocketController
    : public drogon::WebSocketController<AdminObservationWebSocketController, false> {
public:
    struct ConnectionContext {
        std::uint64_t subscriberId{0};
    };

    static std::string JsonToString(const Json::Value& value) {
        Json::StreamWriterBuilder writerBuilder;
        writerBuilder["indentation"] = "";
        return Json::writeString(writerBuilder, value);
    }

    static void SendJson(
        const drogon::WebSocketConnectionPtr& conn,
        const Json::Value& payload) {
        if (!conn || !conn->connected()) {
            return;
        }
        conn->send(JsonToString(payload));
    }


    void handleNewMessage(
        const drogon::WebSocketConnectionPtr& wsConnPtr,
        std::string&& message,
        const drogon::WebSocketMessageType& type) override {
        if (type == drogon::WebSocketMessageType::Ping
            || type == drogon::WebSocketMessageType::Pong
            || type == drogon::WebSocketMessageType::Close) {
            return;
        }
        if (type != drogon::WebSocketMessageType::Text) {
            Json::Value out(Json::objectValue);
            out["type"] = "error";
            out["error"] = "unsupported websocket message type";
            SendJson(wsConnPtr, out);
            return;
        }

        Json::Value payload(Json::objectValue);
        std::string parseError;
        if (!ParseJsonMessage(message, &payload, &parseError)) {
            Json::Value out(Json::objectValue);
            out["type"] = "error";
            out["error"] = "invalid payload: " + parseError;
            SendJson(wsConnPtr, out);
            return;
        }

        const std::string typeName = payload.get("type", "").asString();
        if (typeName != "filter") {
            Json::Value out(Json::objectValue);
            out["type"] = "error";
            out["error"] = "unsupported payload type";
            SendJson(wsConnPtr, out);
            return;
        }

        ObservationStreamFilter filter;
        std::string filterError;
        if (!ParseFilter(payload, &filter, &filterError)) {
            Json::Value out(Json::objectValue);
            out["type"] = "error";
            out["error"] = "invalid filter: " + filterError;
            SendJson(wsConnPtr, out);
            return;
        }

        AdminServer* server = g_activeAdminServer.load();
        if (!server) {
            Json::Value out(Json::objectValue);
            out["type"] = "error";
            out["error"] = "admin server not active";
            SendJson(wsConnPtr, out);
            return;
        }

        const std::uint64_t subscriberId = GetSubscriberId(wsConnPtr);
        server->m_observationStreamHub.UpdateFilter(subscriberId, filter);

        Json::Value ack(Json::objectValue);
        ack["type"] = "filter_applied";
        ack["subscriber_id"] = static_cast<Json::UInt64>(subscriberId);
        SendJson(wsConnPtr, ack);

        ReplayObservations(server, wsConnPtr, filter);
    }

    void handleNewConnection(
        const drogon::HttpRequestPtr& req,
        const drogon::WebSocketConnectionPtr& wsConnPtr) override {
        AdminServer* server = g_activeAdminServer.load();
        if (!server) {
            return;
        }

        ObservationStreamFilter initialFilter;
        std::string filterError;
        if (!ParseFilterFromQuery(req, &initialFilter, &filterError)) {
            Json::Value out(Json::objectValue);
            out["type"] = "error";
            out["error"] = "invalid filter query: " + filterError;
            SendJson(wsConnPtr, out);
            return;
        }

        ConnectionContext context;
        context.subscriberId = server->m_observationStreamHub.AddSubscriber(wsConnPtr, initialFilter);
        wsConnPtr->setContext(std::make_shared<ConnectionContext>(std::move(context)));

        ReplayObservations(server, wsConnPtr, initialFilter);
    }

    void handleConnectionClosed(const drogon::WebSocketConnectionPtr& wsConnPtr) override {
        const std::uint64_t subscriberId = GetSubscriberId(wsConnPtr);
        if (subscriberId == 0) {
            return;
        }
        AdminServer* server = g_activeAdminServer.load();
        if (!server) {
            return;
        }
        server->m_observationStreamHub.RemoveSubscriber(subscriberId);
    }


    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/ws/observations", drogon::Get);
    WS_PATH_LIST_END

private:
    static bool ParseJsonMessage(
        const std::string& message,
        Json::Value* out,
        std::string* error) {
        if (!out) {
            if (error) {
                *error = "internal parser output is null";
            }
            return false;
        }
        Json::CharReaderBuilder readerBuilder;
        std::istringstream in(message);
        std::string parseErrors;
        if (!Json::parseFromStream(readerBuilder, in, out, &parseErrors)) {
            if (error) {
                *error = parseErrors;
            }
            return false;
        }
        if (!out->isObject()) {
            if (error) {
                *error = "payload must be a json object";
            }
            return false;
        }
        return true;
    }

    static bool ParseStringArray(
        const Json::Value& object,
        const char* key,
        std::vector<std::string>* out,
        std::string* error) {
        if (!object.isMember(key)) {
            return true;
        }
        const Json::Value& v = object[key];
        if (!v.isArray()) {
            if (error) {
                *error = std::string(key) + " must be an array of strings";
            }
            return false;
        }
        std::vector<std::string> values;
        for (Json::ArrayIndex i = 0; i < v.size(); ++i) {
            if (!v[i].isString()) {
                if (error) {
                    *error = std::string(key) + " must be an array of strings";
                }
                return false;
            }
            values.push_back(v[i].asString());
        }
        *out = std::move(values);
        return true;
    }

    static bool ParseFilter(
        const Json::Value& object,
        ObservationStreamFilter* out,
        std::string* error) {
        if (!object.isObject()) {
            if (error) {
                *error = "filter payload must be a json object";
            }
            return false;
        }
        ObservationStreamFilter parsed;
        if (!ParseStringArray(object, "layers", &parsed.layers, error)) {
            return false;
        }
        if (!ParseStringArray(object, "tags", &parsed.tags, error)) {
            return false;
        }
        if (object.isMember("last_seen_timestamp")) {
            const Json::Value& ts = object["last_seen_timestamp"];
            if (ts.isUInt64()) {
                parsed.lastSeenTimestampUnixMs = ts.asUInt64();
            } else if (ts.isString()) {
                try {
                    parsed.lastSeenTimestampUnixMs = static_cast<std::uint64_t>(std::stoull(ts.asString()));
                } catch (...) {
                    if (error) {
                        *error = "last_seen_timestamp must be a uint64 or numeric string";
                    }
                    return false;
                }
            } else {
                if (error) {
                    *error = "last_seen_timestamp must be a uint64 or numeric string";
                }
                return false;
            }
        }
        if (object.isMember("last_seen_timestamp_unix_ms")) {
            const Json::Value& ts = object["last_seen_timestamp_unix_ms"];
            if (ts.isUInt64()) {
                parsed.lastSeenTimestampUnixMs = ts.asUInt64();
            } else if (ts.isString()) {
                try {
                    parsed.lastSeenTimestampUnixMs = static_cast<std::uint64_t>(std::stoull(ts.asString()));
                } catch (...) {
                    if (error) {
                        *error = "last_seen_timestamp_unix_ms must be a uint64 or numeric string";
                    }
                    return false;
                }
            } else {
                if (error) {
                    *error = "last_seen_timestamp_unix_ms must be a uint64 or numeric string";
                }
                return false;
            }
        }
        *out = std::move(parsed);
        return true;
    }

    static bool ParseCsvList(const std::string& raw, std::vector<std::string>* out) {
        if (!out) {
            return false;
        }
        out->clear();
        if (raw.empty()) {
            return true;
        }
        std::istringstream ss(raw);
        std::string item;
        while (std::getline(ss, item, ',')) {
            if (!item.empty()) {
                out->push_back(item);
            }
        }
        return true;
    }

    static bool ParseFilterFromQuery(
        const drogon::HttpRequestPtr& req,
        ObservationStreamFilter* out,
        std::string* error) {
        ObservationStreamFilter filter;
        ParseCsvList(req->getParameter("layers"), &filter.layers);
        ParseCsvList(req->getParameter("tags"), &filter.tags);
        const std::string lastSeen = req->getParameter("last_seen_timestamp");
        const std::string lastSeenUnixMs = req->getParameter("last_seen_timestamp_unix_ms");
        const std::string ts = lastSeenUnixMs.empty() ? lastSeen : lastSeenUnixMs;
        if (!ts.empty()) {
            try {
                filter.lastSeenTimestampUnixMs = static_cast<std::uint64_t>(std::stoull(ts));
            } catch (...) {
                if (error) {
                    *error = "last_seen_timestamp must be a numeric value";
                }
                return false;
            }
        }
        *out = std::move(filter);
        return true;
    }

    static std::uint64_t GetSubscriberId(const drogon::WebSocketConnectionPtr& conn) {
        auto context = conn->getContext<ConnectionContext>();
        if (!context) {
            return 0;
        }
        return context->subscriberId;
    }





    static void ReplayObservations(
        AdminServer* server,
        const drogon::WebSocketConnectionPtr& connection,
        const ObservationStreamFilter& filter) {
        if (!server || !connection || !connection->connected()) {
            return;
        }
        std::vector<KernelConfig::MemoryObservation> candidates;
        const auto observations = server->m_memoryManager.ListAllObservations();
        for (const auto& observation : observations) {
            if (observation.timestampUnixMs <= filter.lastSeenTimestampUnixMs) {
                continue;
            }
            if (ObservationMatchesFilter(observation, filter)) {
                candidates.push_back(observation);
            }
        }

        std::sort(
            candidates.begin(),
            candidates.end(),
            [](const KernelConfig::MemoryObservation& a, const KernelConfig::MemoryObservation& b) {
                return a.timestampUnixMs < b.timestampUnixMs;
            });
        for (const auto& observation : candidates) {
            SendJson(connection, BuildObservationStreamEvent(observation));
        }
    }
};
