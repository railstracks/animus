// ============================================================================
// Diary API — admin-facing
//
// Metadata: no content exposed (privacy contract).
// Export: encrypted .aidiary container using the agent's diary_secret.
// Import: decrypts .aidiary using the agent's diary_secret.
// The admin never provides or sees the encryption key.
// ====================================================================================

        // GET /api/v1/diary/{agentId}/metadata
        drogon::app().registerHandler(
            "/api/v1/diary/{agentId}/metadata",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& agentId) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                auto result = server->GetDiaryManager().GetMetadata(agentId);
                SendJsonResponse(callback, result.body,
                    static_cast<drogon::HttpStatusCode>(result.httpStatusCode));
            },
            {drogon::Get});

        // POST /api/v1/diary/{agentId}/export
        // No body required — uses agent's diary_secret automatically.
        // Returns: binary .aidiary file
        drogon::app().registerHandler(
            "/api/v1/diary/{agentId}/export",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& agentId) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                // Look up the agent's diary_secret
                if (!server->m_agentStore) {
                    SendJsonError(callback, "agent store not available", drogon::k500InternalServerError);
                    return;
                }
                auto agentOpt = server->m_agentStore->GetById(agentId);
                if (!agentOpt.has_value()) {
                    SendJsonError(callback, "agent not found", drogon::k404NotFound);
                    return;
                }
                const std::string& diarySecret = agentOpt->diary_secret;
                if (diarySecret.empty()) {
                    SendJsonError(callback, "agent has no diary secret — export not possible", drogon::k403Forbidden);
                    return;
                }

                // Export entries as JSON
                Json::Value entries = server->GetDiaryManager().ExportEntriesJson(agentId);
                if (entries.isNull() || entries.size() == 0) {
                    SendJsonError(callback, "no diary entries found for agent", drogon::k404NotFound);
                    return;
                }

                // Encrypt with AiDiaryFormat using the agent's diary_secret
                Json::Value payload(Json::objectValue);
                payload["version"] = 1;
                payload["agent_id"] = agentId;
                payload["entries"] = entries;

                auto container = AiDiaryFormat::Encrypt(payload, diarySecret);
                if (container.empty()) {
                    SendJsonError(callback, "encryption failed", drogon::k500InternalServerError);
                    return;
                }

                std::string binaryData(reinterpret_cast<const char*>(container.data()),
                                       container.size());
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k200OK);
                resp->setBody(std::move(binaryData));
                resp->setContentTypeString("application/octet-stream");
                resp->addHeader("Content-Disposition",
                    "attachment; filename=\"diary-" + agentId + ".aidiary\"");
                callback(resp);
            },
            {drogon::Post});

        // POST /api/v1/diary/{agentId}/import
        // Body: { "data": "<base64-encoded .aidiary>", "merge_strategy": "merge|replace" }
        // Uses agent's diary_secret to decrypt — admin never has the key.
        drogon::app().registerHandler(
            "/api/v1/diary/{agentId}/import",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& agentId) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                // Look up the agent's diary_secret
                if (!server->m_agentStore) {
                    SendJsonError(callback, "agent store not available", drogon::k500InternalServerError);
                    return;
                }
                auto agentOpt = server->m_agentStore->GetById(agentId);
                if (!agentOpt.has_value()) {
                    SendJsonError(callback, "agent not found", drogon::k404NotFound);
                    return;
                }
                const std::string& diarySecret = agentOpt->diary_secret;
                if (diarySecret.empty()) {
                    SendJsonError(callback, "agent has no diary secret — import not possible", drogon::k403Forbidden);
                    return;
                }

                Json::Value body;
                std::string parseErr;
                std::string bodyStr(req->getBody());
                if (!ParseJsonPayload(bodyStr, &body, &parseErr)) {
                    SendJsonError(callback, "invalid JSON: " + parseErr, drogon::k400BadRequest);
                    return;
                }

                if (!body.isMember("data") || !body["data"].isString()) {
                    SendJsonError(callback, "data (base64-encoded .aidiary) is required", drogon::k400BadRequest);
                    return;
                }

                std::string mergeStrategy = "merge";
                if (body.isMember("merge_strategy") && body["merge_strategy"].isString()) {
                    mergeStrategy = body["merge_strategy"].asString();
                }

                // Decode base64 data
                std::string b64Data = body["data"].asString();
                static const std::string base64_chars =
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                std::vector<uint8_t> container;
                std::vector<int> decoding(256, -1);
                for (int i = 0; i < 64; i++) decoding[static_cast<unsigned char>(base64_chars[i])] = i;

                std::string input;
                for (char c : b64Data) {
                    if (c == '\n' || c == '\r' || c == ' ') continue;
                    if (c == '=') break;
                    if (decoding[static_cast<unsigned char>(c)] == -1) continue;
                    input += c;
                }

                for (size_t i = 0; i < input.size(); i += 4) {
                    int sextet[4] = {0, 0, 0, 0};
                    for (int j = 0; j < 4 && i + j < input.size(); j++) {
                        sextet[j] = decoding[static_cast<unsigned char>(input[i + j])];
                    }
                    uint32_t triple = (sextet[0] << 18) | (sextet[1] << 12) | (sextet[2] << 6) | sextet[3];
                    container.push_back(static_cast<uint8_t>((triple >> 16) & 0xFF));
                    if (i + 2 < input.size()) container.push_back(static_cast<uint8_t>((triple >> 8) & 0xFF));
                    if (i + 3 < input.size()) container.push_back(static_cast<uint8_t>(triple & 0xFF));
                }

                // Decrypt with AiDiaryFormat using the agent's diary_secret
                auto decrypted = AiDiaryFormat::Decrypt(container, diarySecret);
                if (!decrypted.has_value()) {
                    SendJsonError(callback, "decryption failed — this .aidiary was not exported by this agent",
                        drogon::k400BadRequest);
                    return;
                }

                auto& payload = decrypted.value();
                if (!payload.isMember("entries") || !payload["entries"].isArray()) {
                    SendJsonError(callback, "invalid .aidiary format — missing entries",
                        drogon::k400BadRequest);
                    return;
                }

                // Import entries
                auto result = server->GetDiaryManager().ImportEntriesJson(
                    agentId, payload["entries"], mergeStrategy);
                SendJsonResponse(callback, result.body,
                    static_cast<drogon::HttpStatusCode>(result.httpStatusCode));
            },
            {drogon::Post});