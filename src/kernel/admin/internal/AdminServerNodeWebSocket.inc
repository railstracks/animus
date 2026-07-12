// ============================================================================
// NodeWebSocketController — accepts external node connections
//
// Path: /ws/node
// Auth: Authorization: Bearer <token> header during upgrade
//
// Protocol:
//   Node → Server: {"type": "register", "name": "...", "tools": [...], "hostname": "...", "os": "..."}
//   Server → Node: {"type": "tool_call", "call_id": "...", "tool": "...", "arguments": {...}}
//   Node → Server: {"type": "tool_result", "call_id": "...", "success": true, "output": "...", "error": "..."}
//   Node → Server: {"type": "heartbeat"}
// ============================================================================

class NodeWebSocketController
    : public drogon::WebSocketController<NodeWebSocketController, false> {
public:
    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/ws/node", drogon::Get);
    WS_PATH_LIST_END

    struct NodeContext {
        std::string nodeName;
        std::string token;
        bool authenticated{false};
        std::unordered_map<std::string, std::promise<ToolResult>> pendingCalls;
        std::mutex pendingMutex;
    };

    void handleNewConnection(
        const drogon::HttpRequestPtr& req,
        const drogon::WebSocketConnectionPtr& wsConnPtr) override {

        AdminServer* server = g_activeAdminServer.load();
        if (!server || !server->m_nodeManager) {
            wsConnPtr->shutdown();
            return;
        }

        // Extract auth token from header
        std::string authHeader = req->getHeader("Authorization");
        std::string token;
        if (authHeader.substr(0, 7) == "Bearer ") {
            token = authHeader.substr(7);
        }

        if (token.empty()) {
            std::cerr << "[node-ws] Connection rejected: no token\n";
            wsConnPtr->shutdown();
            return;
        }

        int64_t tokenId = server->m_nodeManager->ValidateToken(token);
        if (tokenId < 0) {
            std::cerr << "[node-ws] Connection rejected: invalid token\n";
            wsConnPtr->shutdown();
            return;
        }

        auto ctx = std::make_shared<NodeContext>();
        ctx->token = token;
        wsConnPtr->setContext(ctx);

        std::cerr << "[node-ws] Connection accepted (token id=" << tokenId << ")\n";
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
        if (type != drogon::WebSocketMessageType::Text) return;

        AdminServer* server = g_activeAdminServer.load();
        if (!server || !server->m_nodeManager) return;

        Json::Value payload;
        Json::CharReaderBuilder builder;
        std::string parseErr;
        auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
        if (!reader->parse(message.c_str(), message.c_str() + message.size(), &payload, &parseErr)) {
            std::cerr << "[node-ws] Failed to parse message: " << parseErr << "\n";
            return;
        }

        std::string msgType = payload.get("type", "").asString();

        if (msgType == "register") {
            HandleRegister(wsConnPtr, payload);
        } else if (msgType == "tool_result") {
            HandleToolResult(wsConnPtr, payload);
        } else if (msgType == "heartbeat") {
            // Just keep the connection alive
        } else {
            std::cerr << "[node-ws] Unknown message type: " << msgType << "\n";
        }
    }

    void handleConnectionClosed(
        const drogon::WebSocketConnectionPtr& wsConnPtr) override {

        auto ctx = wsConnPtr->getContext<NodeContext>();
        if (!ctx) return;

        AdminServer* server = g_activeAdminServer.load();
        if (server && server->m_nodeManager && !ctx->nodeName.empty()) {
            server->m_nodeManager->RemoveNode(ctx->nodeName);
        }

        // Fail any pending tool calls
        {
            std::lock_guard<std::mutex> lock(ctx->pendingMutex);
            for (auto& [id, promise] : ctx->pendingCalls) {
                ToolResult result;
                result.call_id = id;
                result.success = false;
                result.error = "node disconnected";
                promise.set_value(std::move(result));
            }
            ctx->pendingCalls.clear();
        }

        std::cerr << "[node-ws] Node disconnected: " << ctx->nodeName << "\n";
    }

private:
    void HandleRegister(const drogon::WebSocketConnectionPtr& wsConnPtr, const Json::Value& payload) {
        auto ctx = wsConnPtr->getContext<NodeContext>();
        if (!ctx) return;

        AdminServer* server = g_activeAdminServer.load();
        if (!server || !server->m_nodeManager) return;

        ctx->nodeName = payload.get("name", "").asString();
        if (ctx->nodeName.empty()) {
            std::cerr << "[node-ws] Register: missing name\n";
            wsConnPtr->shutdown();
            return;
        }

        NodeInfo info;
        info.name = ctx->nodeName;
        info.hostname = payload.get("hostname", "").asString();
        info.os_info = payload.get("os", "").asString();

        Json::Value toolsArr = payload["tools"];
        for (const auto& t : toolsArr) {
            info.tools.push_back(t.asString());
        }

        // Create execute function that sends tool calls over this WebSocket
        auto wsConn = wsConnPtr;  // capture by value (shared_ptr)
        auto ctxPtr = ctx;        // capture by value (shared_ptr)
        NodeExecuteFn execFn = [wsConn, ctxPtr](const ToolCall& call) -> ToolResult {
            // Generate a unique call_id for this request
            static std::atomic<int64_t> callCounter{1};
            std::string callId = "node_req_" + std::to_string(callCounter.fetch_add(1));

            // Create promise/future for async result
            auto& promise = ctxPtr->pendingCalls[callId];

            // Send tool_call message to node
            Json::Value msg(Json::objectValue);
            msg["type"] = "tool_call";
            msg["call_id"] = callId;
            msg["tool"] = call.name;

            // Parse and re-serialize arguments
            Json::Value argsJson;
            Json::CharReaderBuilder rb;
            std::string parseErr;
            auto reader = std::unique_ptr<Json::CharReader>(rb.newCharReader());
            if (reader->parse(call.arguments.c_str(),
                              call.arguments.c_str() + call.arguments.size(),
                              &argsJson, &parseErr)) {
                msg["arguments"] = argsJson;
            } else {
                msg["arguments"] = call.arguments; // fallback: raw string
            }

            Json::StreamWriterBuilder wb;
            wb["indentation"] = "";
            std::string msgStr = Json::writeString(wb, msg);
            wsConn->send(msgStr);

            // Wait for result (with timeout)
            auto future = promise.get_future();
            if (future.wait_for(std::chrono::seconds(30)) == std::future_status::timeout) {
                std::lock_guard<std::mutex> lock(ctxPtr->pendingMutex);
                ctxPtr->pendingCalls.erase(callId);
                ToolResult result;
                result.call_id = call.id;
                result.success = false;
                result.error = "node tool call timeout (30s)";
                return result;
            }

            ToolResult result = future.get();
            result.call_id = call.id; // Use original call ID
            return result;
        };

        server->m_nodeManager->RegisterNode(ctx->nodeName, info, execFn);
        ctx->authenticated = true;

        // Send acknowledgment
        Json::Value ack(Json::objectValue);
        ack["type"] = "registered";
        ack["name"] = ctx->nodeName;
        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        wsConnPtr->send(Json::writeString(wb, ack));

        std::cerr << "[node-ws] Node registered: " << ctx->nodeName
                  << " (" << info.tools.size() << " tools)\n";
    }

    void HandleToolResult(const drogon::WebSocketConnectionPtr& wsConnPtr, const Json::Value& payload) {
        auto ctx = wsConnPtr->getContext<NodeContext>();
        if (!ctx) return;

        std::string callId = payload.get("call_id", "").asString();
        bool success = payload.get("success", false).asBool();
        std::string output = payload.get("output", "").asString();
        std::string error = payload.get("error", "").asString();

        std::lock_guard<std::mutex> lock(ctx->pendingMutex);
        auto it = ctx->pendingCalls.find(callId);
        if (it == ctx->pendingCalls.end()) {
            std::cerr << "[node-ws] Tool result for unknown call_id: " << callId << "\n";
            return;
        }

        ToolResult result;
        result.call_id = callId;
        result.success = success;
        result.output = output;
        result.error = error;
        it->second.set_value(std::move(result));
        ctx->pendingCalls.erase(it);
    }
};