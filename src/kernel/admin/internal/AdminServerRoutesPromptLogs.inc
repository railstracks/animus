// ============================================================================
// Prompt Logs API — admin-facing
//
// LLM call logging: token usage, model, provider, latency.
// Query by agent, get usage summaries. Full level includes content.
// ============================================================================

        // GET /api/v1/agents/{id}/prompt-logs?from={iso}&to={iso}&limit={n}
        drogon::app().registerHandler(
            "/api/v1/agents/{1}/prompt-logs",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& agentId) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_promptLogStore) {
                    SendJsonError(callback, "prompt log store not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }

                std::string fromIso = req->getParameter("from");
                std::string toIso = req->getParameter("to");

                int limit = 100;
                std::string limitStr = req->getParameter("limit");
                if (!limitStr.empty()) {
                    try { limit = std::stoi(limitStr); }
                    catch (...) {}
                }
                if (limit < 1) limit = 100;
                if (limit > 1000) limit = 1000;

                auto logs = server->m_promptLogStore->QueryByAgent(
                    agentId, fromIso, toIso, limit);

                Json::Value items(Json::arrayValue);
                for (const auto& e : logs) {
                    Json::Value v(Json::objectValue);
                    v["id"] = static_cast<Json::Int64>(e.id);
                    v["agent_id"] = e.agent_id;
                    if (e.session_id > 0) {
                        v["session_id"] = static_cast<Json::Int64>(e.session_id);
                    }
                    v["provider"] = e.provider;
                    v["model"] = e.model;
                    v["prompt_tokens"] = e.prompt_tokens;
                    v["completion_tokens"] = e.completion_tokens;
                    v["total_tokens"] = e.total_tokens;
                    v["latency_ms"] = e.latency_ms;
                    v["chain_step"] = e.chain_step;
                    v["created_at"] = e.created_at;

                    if (!e.prompt_content.empty()) {
                        v["prompt_content"] = e.prompt_content;
                    }
                    if (!e.response_content.empty()) {
                        v["response_content"] = e.response_content;
                    }
                    if (!e.tool_calls.empty()) {
                        Json::CharReaderBuilder builder;
                        std::istringstream stream(e.tool_calls);
                        Json::Value calls;
                        std::string errors;
                        if (Json::parseFromStream(builder, stream, &calls, &errors) && calls.isArray()) {
                            v["tool_calls"] = calls;
                        }
                    }

                    items.append(v);
                }

                Json::Value body(Json::objectValue);
                body["logs"] = items;
                body["count"] = static_cast<Json::Int>(logs.size());
                SendJsonResponse(callback, body);
            },
            {drogon::Get});

        // GET /api/v1/agents/{id}/prompt-logs/summary?from={iso}&to={iso}
        drogon::app().registerHandler(
            "/api/v1/agents/{1}/prompt-logs/summary",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& agentId) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_promptLogStore) {
                    SendJsonError(callback, "prompt log store not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }

                std::string fromIso = req->getParameter("from");
                std::string toIso = req->getParameter("to");

                auto summary = server->m_promptLogStore->GetUsageSummary(
                    agentId, fromIso, toIso);

                Json::Value v(Json::objectValue);
                v["total_calls"] = summary.total_calls;
                v["total_prompt_tokens"] = summary.total_prompt_tokens;
                v["total_completion_tokens"] = summary.total_completion_tokens;
                v["total_tokens"] = summary.total_tokens;
                v["avg_latency_ms"] = summary.avg_latency_ms;

                SendJsonResponse(callback, v);
            },
            {drogon::Get});
