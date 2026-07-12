// ============================================================================
// Gallivanting API — admin-facing
//
// CRUD for gallivanting threads, session history, SDT balance scores.
// Template loading uses existing /api/v1/memory/templates endpoint.
// ============================================================================

        // GET /api/v1/gallivanting/threads?agent_id={agentId}
        drogon::app().registerHandler(
            "/api/v1/gallivanting/threads",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_gallivantingStore) {
                    SendJsonError(callback, "gallivanting store not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }

                std::string agentId = req->getParameter("agent_id");
                if (agentId.empty()) agentId = "default";

                auto threads = server->m_gallivantingStore->ListThreads(agentId);

                Json::Value items(Json::arrayValue);
                for (const auto& t : threads) {
                    Json::Value v(Json::objectValue);
                    v["id"] = static_cast<Json::Int64>(t.id);
                    v["name"] = t.name;
                    v["description"] = t.description;

                    Json::CharReaderBuilder builder;
                    std::istringstream stream(t.sdt_tags_json);
                    Json::Value tags;
                    std::string errors;
                    if (Json::parseFromStream(builder, stream, &tags, &errors) && ((tags.isObject() || tags.isArray()))) {
                        v["sdt_tags"] = tags;
                    } else {
                        v["sdt_tags"] = Json::objectValue;
                    }

                    v["prompt_template"] = t.prompt_template;
                    v["enabled"] = t.enabled;
                    v["created_at_unix_ms"] = static_cast<Json::Int64>(t.created_at_unix_ms);
                    v["updated_at_unix_ms"] = static_cast<Json::Int64>(t.updated_at_unix_ms);
                    items.append(v);
                }

                Json::Value body(Json::objectValue);
                body["threads"] = items;
                body["count"] = static_cast<Json::Int>(threads.size());
                SendJsonResponse(callback, body);
            },
            {drogon::Get});

        // POST /api/v1/gallivanting/threads
        // Body: { agent_id, name, description?, sdt_tags?, prompt_template?, enabled? }
        drogon::app().registerHandler(
            "/api/v1/gallivanting/threads",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_gallivantingStore) {
                    SendJsonError(callback, "gallivanting store not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }

                Json::Value body;
                std::string parseErr;
                std::string bodyStr(req->getBody());
                if (!ParseJsonPayload(bodyStr, &body, &parseErr)) {
                    SendJsonError(callback, "invalid JSON: " + parseErr, drogon::k400BadRequest);
                    return;
                }

                if (!body.isMember("name") || !body["name"].isString() || body["name"].asString().empty()) {
                    SendJsonError(callback, "name is required", drogon::k400BadRequest);
                    return;
                }

                GallivantingThread thread;
                thread.agent_id = body.get("agent_id", "default").asString();
                thread.name = body["name"].asString();
                thread.description = body.get("description", "").asString();
                thread.prompt_template = body.get("prompt_template", "").asString();
                thread.enabled = body.get("enabled", true).asBool();

                if (body.isMember("sdt_tags") && (body["sdt_tags"].isObject() || body["sdt_tags"].isArray())) {
                    Json::StreamWriterBuilder wb;
                    wb.settings_["indentation"] = "";
                    thread.sdt_tags_json = Json::writeString(wb, body["sdt_tags"]);
                } else {
                    thread.sdt_tags_json = "[]";
                }

                auto created = server->m_gallivantingStore->CreateThread(thread);

                Json::Value v(Json::objectValue);
                v["id"] = static_cast<Json::Int64>(created.id);
                v["name"] = created.name;
                v["description"] = created.description;
                v["sdt_tags_json"] = created.sdt_tags_json;
                v["prompt_template"] = created.prompt_template;
                v["enabled"] = created.enabled;
                v["created_at_unix_ms"] = static_cast<Json::Int64>(created.created_at_unix_ms);

                SendJsonResponse(callback, v, drogon::k201Created);
            },
            {drogon::Post});

        // GET /api/v1/gallivanting/threads/{id}
        drogon::app().registerHandler(
            "/api/v1/gallivanting/threads/{1}",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               int64_t threadId) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_gallivantingStore) {
                    SendJsonError(callback, "gallivanting store not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }

                auto thread = server->m_gallivantingStore->GetThread(threadId);
                if (!thread) {
                    SendJsonError(callback, "thread not found", drogon::k404NotFound);
                    return;
                }

                Json::Value v(Json::objectValue);
                v["id"] = static_cast<Json::Int64>(thread->id);
                v["agent_id"] = thread->agent_id;
                v["name"] = thread->name;
                v["description"] = thread->description;

                Json::CharReaderBuilder builder;
                std::istringstream stream(thread->sdt_tags_json);
                Json::Value tags;
                std::string errors;
                if (Json::parseFromStream(builder, stream, &tags, &errors) && ((tags.isObject() || tags.isArray()))) {
                    v["sdt_tags"] = tags;
                } else {
                    v["sdt_tags"] = Json::objectValue;
                }

                v["prompt_template"] = thread->prompt_template;
                v["enabled"] = thread->enabled;
                v["created_at_unix_ms"] = static_cast<Json::Int64>(thread->created_at_unix_ms);
                v["updated_at_unix_ms"] = static_cast<Json::Int64>(thread->updated_at_unix_ms);

                SendJsonResponse(callback, v);
            },
            {drogon::Get});

        // PATCH /api/v1/gallivanting/threads/{id}
        // Body: { name?, description?, sdt_tags?, prompt_template?, enabled? }
        drogon::app().registerHandler(
            "/api/v1/gallivanting/threads/{1}",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               int64_t threadId) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_gallivantingStore) {
                    SendJsonError(callback, "gallivanting store not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }

                auto thread = server->m_gallivantingStore->GetThread(threadId);
                if (!thread) {
                    SendJsonError(callback, "thread not found", drogon::k404NotFound);
                    return;
                }

                Json::Value body;
                std::string parseErr;
                std::string bodyStr(req->getBody());
                if (!ParseJsonPayload(bodyStr, &body, &parseErr)) {
                    SendJsonError(callback, "invalid JSON: " + parseErr, drogon::k400BadRequest);
                    return;
                }

                if (body.isMember("name") && body["name"].isString()) {
                    thread->name = body["name"].asString();
                }
                if (body.isMember("description") && body["description"].isString()) {
                    thread->description = body["description"].asString();
                }
                if (body.isMember("prompt_template") && body["prompt_template"].isString()) {
                    thread->prompt_template = body["prompt_template"].asString();
                }
                if (body.isMember("enabled") && body["enabled"].isBool()) {
                    thread->enabled = body["enabled"].asBool();
                }
                if (body.isMember("sdt_tags") && (body["sdt_tags"].isObject() || body["sdt_tags"].isArray())) {
                    Json::StreamWriterBuilder wb;
                    wb.settings_["indentation"] = "";
                    thread->sdt_tags_json = Json::writeString(wb, body["sdt_tags"]);
                }

                server->m_gallivantingStore->UpdateThread(*thread);

                auto updated = server->m_gallivantingStore->GetThread(threadId);
                Json::Value v(Json::objectValue);
                v["id"] = static_cast<Json::Int64>(updated->id);
                v["name"] = updated->name;
                v["description"] = updated->description;
                v["sdt_tags_json"] = updated->sdt_tags_json;
                v["prompt_template"] = updated->prompt_template;
                v["enabled"] = updated->enabled;
                v["updated_at_unix_ms"] = static_cast<Json::Int64>(updated->updated_at_unix_ms);

                SendJsonResponse(callback, v);
            },
            {drogon::Patch});

        // DELETE /api/v1/gallivanting/threads/{id}
        drogon::app().registerHandler(
            "/api/v1/gallivanting/threads/{1}",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               int64_t threadId) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_gallivantingStore) {
                    SendJsonError(callback, "gallivanting store not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }

                if (!server->m_gallivantingStore->DeleteThread(threadId)) {
                    SendJsonError(callback, "thread not found or delete failed", drogon::k404NotFound);
                    return;
                }

                Json::Value v(Json::objectValue);
                v["success"] = true;
                v["deleted_thread_id"] = static_cast<Json::Int64>(threadId);
                SendJsonResponse(callback, v);
            },
            {drogon::Delete});

        // GET /api/v1/gallivanting/threads/{id}/sessions?limit={limit}
        drogon::app().registerHandler(
            "/api/v1/gallivanting/threads/{1}/sessions",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               int64_t threadId) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_gallivantingStore) {
                    SendJsonError(callback, "gallivanting store not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }

                int64_t limit = 20;
                std::string limitStr = req->getParameter("limit");
                if (!limitStr.empty()) {
                    try { limit = std::stoll(limitStr); }
                    catch (...) {}
                }
                if (limit < 1) limit = 20;
                if (limit > 100) limit = 100;

                auto sessions = server->m_gallivantingStore->ListSessionsForThread(threadId, limit);

                Json::Value items(Json::arrayValue);
                for (const auto& s : sessions) {
                    Json::Value v(Json::objectValue);
                    v["id"] = static_cast<Json::Int64>(s.id);
                    v["thread_id"] = static_cast<Json::Int64>(s.thread_id);
                    v["started_at_unix_ms"] = static_cast<Json::Int64>(s.started_at_unix_ms);
                    v["duration_ms"] = static_cast<Json::Int64>(s.duration_ms);
                    v["summary"] = s.summary;
                    v["outcome"] = s.outcome;

                    Json::CharReaderBuilder builder;
                    std::istringstream scoreStream(s.sdt_scores_json);
                    Json::Value scores;
                    std::string errors;
                    if (Json::parseFromStream(builder, scoreStream, &scores, &errors) && scores.isObject()) {
                        v["sdt_scores"] = scores;
                    } else {
                        v["sdt_scores"] = Json::objectValue;
                    }

                    std::istringstream toolsStream(s.tools_used_json);
                    Json::Value tools;
                    if (Json::parseFromStream(builder, toolsStream, &tools, &errors) && tools.isArray()) {
                        v["tools_used"] = tools;
                    } else {
                        v["tools_used"] = Json::arrayValue;
                    }

                    v["created_at_unix_ms"] = static_cast<Json::Int64>(s.created_at_unix_ms);
                    items.append(v);
                }

                Json::Value body(Json::objectValue);
                body["sessions"] = items;
                body["count"] = static_cast<Json::Int>(sessions.size());
                SendJsonResponse(callback, body);
            },
            {drogon::Get});

        // GET /api/v1/gallivanting/agents/{id}/balance?since={unix_ms}
        drogon::app().registerHandler(
            "/api/v1/gallivanting/agents/{1}/balance",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& agentId) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_gallivantingStore) {
                    SendJsonError(callback, "gallivanting store not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }

                // Default: 2 weeks ago
                int64_t sinceMs = 0;
                std::string sinceStr = req->getParameter("since");
                if (!sinceStr.empty()) {
                    try { sinceMs = std::stoll(sinceStr); }
                    catch (...) {}
                }
                if (sinceMs <= 0) {
                    // 14 days ago
                    sinceMs = static_cast<int64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count())
                        - (14LL * 24 * 60 * 60 * 1000);
                }

                auto balance = server->m_gallivantingStore->GetSdtBalance(agentId, sinceMs);

                Json::Value v(Json::objectValue);
                v["autonomy"] = balance.autonomy;
                v["competence"] = balance.competence;
                v["relatedness"] = balance.relatedness;
                v["personal_development"] = balance.personal_development;
                v["relaxation"] = balance.relaxation;
                v["meaning"] = balance.meaning;
                v["session_count"] = static_cast<Json::Int64>(balance.session_count);
                v["since_unix_ms"] = static_cast<Json::Int64>(sinceMs);

                SendJsonResponse(callback, v);
            },
            {drogon::Get});
