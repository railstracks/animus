// ============================================================================
// Agent CRUD API handlers — multi-tenant
// ============================================================================

        // GET /api/v1/agents — list all agents

        // ============================================================================
        // Agents (CRUD)
        // ====================================================================================
        drogon::app().registerHandler(
            "/api/v1/agents",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                const auto result = server->m_agentManager.ListAgents();
                if (result.statusCode != 200) {
                    Json::Value out;
                    out["error"] = result.error.empty() ? "failed to list agents" : result.error;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(result.statusCode));
                    callback(resp);
                    return;
                }

                Json::Value arr(Json::arrayValue);
                for (const auto& a : result.agents) {
                    arr.append(BuildAgentEntityJson(a));
                }

                Json::Value out(Json::objectValue);
                out["agents"] = arr;
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        // GET /api/v1/agents/{id} — get single agent
        drogon::app().registerHandler(
            "/api/v1/agents/{id}",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                const auto result = server->m_agentManager.GetAgent(id);
                if (result.statusCode != 200 || !result.agent.has_value()) {
                    Json::Value out;
                    out["error"] = result.error.empty() ? "agent not found" : result.error;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(
                        result.statusCode == 200 ? 404 : result.statusCode));
                    callback(resp);
                    return;
                }

                callback(drogon::HttpResponse::newHttpJsonResponse(BuildAgentEntityJson(*result.agent)));
            },
            {drogon::Get});

        // POST /api/v1/agents — create agent
        drogon::app().registerHandler(
            "/api/v1/agents",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                const auto body = req->getJsonObject();
                if (!body || !body->isObject()) {
                    Json::Value out;
                    AddBadRequestError("request body must be a JSON object", &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                const auto result = server->m_agentManager.CreateAgent(*body);
                if (result.statusCode != 201 || !result.agent.has_value()) {
                    Json::Value out;
                    out["error"] = result.error.empty() ? "failed to create agent" : result.error;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(result.statusCode));
                    callback(resp);
                    return;
                }

                auto resp = drogon::HttpResponse::newHttpJsonResponse(
                    BuildAgentEntityJson(*result.agent));
                resp->setStatusCode(drogon::k201Created);
                callback(resp);
            },
            {drogon::Post});

        // PATCH /api/v1/agents/{id} — update agent
        drogon::app().registerHandler(
            "/api/v1/agents/{id}",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                const auto body = req->getJsonObject();
                if (!body || !body->isObject()) {
                    Json::Value out;
                    AddBadRequestError("request body must be a JSON object", &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                const auto result = server->m_agentManager.UpdateAgent(id, *body);
                if (result.statusCode != 200 || !result.agent.has_value()) {
                    Json::Value out;
                    out["error"] = result.error.empty() ? "failed to update agent" : result.error;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(result.statusCode));
                    callback(resp);
                    return;
                }

                callback(drogon::HttpResponse::newHttpJsonResponse(
                    BuildAgentEntityJson(*result.agent)));
            },
            {drogon::Patch});

        // DELETE /api/v1/agents/{id}
        drogon::app().registerHandler(
            "/api/v1/agents/{id}",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                const auto result = server->m_agentManager.DeleteAgent(id);
                if (result.statusCode != 200) {
                    Json::Value out;
                    out["error"] = result.error.empty() ? "failed to delete agent" : result.error;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(result.statusCode));
                    callback(resp);
                    return;
                }

                Json::Value out(Json::objectValue);
                out["deleted"] = result.deletedId.empty() ? id : result.deletedId;
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Delete});

// ============================================================================
// Tool registry endpoint
// ============================================================================

        // ============================================================================
        // Tools (list)
        // ====================================================================================

        drogon::app().registerHandler(
            "/api/v1/tools",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                const auto& tools = server->m_toolRegistry->GetAllDefinitions();
                Json::Value arr(Json::arrayValue);
                std::function<Json::Value(const ToolParameter&)> serializeParam;
                serializeParam = [&serializeParam](const ToolParameter& p) -> Json::Value {
                    Json::Value param(Json::objectValue);
                    param["name"] = p.name;
                    param["type"] = p.type;
                    param["description"] = p.description;
                    param["required"] = p.required;
                    if (!p.default_value_json.empty()) {
                        Json::Value parsedDefault;
                        std::string parseErr;
                        if (ParseJsonPayload(p.default_value_json, &parsedDefault, &parseErr)) {
                            param["default"] = parsedDefault;
                        }
                    }
                    if (!p.enum_values.empty()) {
                        Json::Value enums(Json::arrayValue);
                        for (const auto& value : p.enum_values) {
                            enums.append(value);
                        }
                        param["enum"] = enums;
                    }
                    if (!p.properties.empty()) {
                        Json::Value props(Json::arrayValue);
                        for (const auto& child : p.properties) {
                            props.append(serializeParam(child));
                        }
                        param["properties"] = props;
                    }
                    return param;
                };
                for (const auto& def : tools) {
                    Json::Value t(Json::objectValue);
                    t["name"] = def.name;
                    t["description"] = def.description;
                    Json::Value params(Json::arrayValue);
                    for (const auto& p : def.parameters) {
                        params.append(serializeParam(p));
                    }
                    t["parameters"] = params;
                    Json::Value configParams(Json::arrayValue);
                    for (const auto& p : def.config_parameters) {
                        configParams.append(serializeParam(p));
                    }
                    t["config_parameters"] = configParams;
                    arr.append(t);
                }

                Json::Value out(Json::objectValue);
                out["tools"] = arr;
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

// ============================================================================
// Scheduler endpoints
// ============================================================================

        drogon::app().registerHandler(
            "/api/v1/scheduler/status",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_scheduler) {
                    SendJsonError(callback, "scheduler is not configured", drogon::k503ServiceUnavailable);
                    return;
                }

                Json::Value out(Json::objectValue);
                out["running"] = server->m_scheduler->IsRunning();
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        drogon::app().registerHandler(
            "/api/v1/scheduler/schedules",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_scheduler) {
                    SendJsonError(callback, "scheduler is not configured", drogon::k503ServiceUnavailable);
                    return;
                }

                const std::string agentId = std::string(req->getParameter("agent_id"));
                if (agentId.empty()) {
                    SendJsonError(callback, "agent_id query parameter is required", drogon::k400BadRequest);
                    return;
                }
                const std::string tag = std::string(req->getParameter("tag"));
                const std::string includeDisabledParam = std::string(req->getParameter("include_disabled"));
                const bool includeDisabled = includeDisabledParam != "0"
                    && includeDisabledParam != "false"
                    && includeDisabledParam != "False"
                    && includeDisabledParam != "FALSE";

                const auto schedules = server->m_scheduler->List(agentId, tag);
                Json::Value arr(Json::arrayValue);
                for (const auto& schedule : schedules) {
                    if (!includeDisabled && !schedule.enabled) {
                        continue;
                    }

                    Json::Value item(Json::objectValue);
                    item["id"] = schedule.id;
                    item["agent_id"] = schedule.agent_id;
                    item["tag"] = schedule.tag;
                    item["schedule_type"] =
                        schedule.type == ScheduleType::Recurring ? "recurring" : "one_shot";
                    item["next_fire"] = schedule.next_fire;
                    item["cron_expr"] = schedule.cron_expr;
                    item["timezone"] = schedule.timezone;
                    item["message"] = schedule.message;
                    item["enabled"] = schedule.enabled;
                    item["created_at"] = schedule.created_at;
                    item["last_fire"] = schedule.last_fire;
                    item["fire_count"] = schedule.fire_count;
                    item["max_fires"] = schedule.max_fires;
                    arr.append(item);
                }

                Json::Value out(Json::objectValue);
                out["agent_id"] = agentId;
                out["tag"] = tag;
                out["total"] = static_cast<Json::UInt64>(arr.size());
                out["schedules"] = arr;
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        drogon::app().registerHandler(
            "/api/v1/scheduler/schedules/{1}",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_scheduler) {
                    SendJsonError(callback, "scheduler is not configured", drogon::k503ServiceUnavailable);
                    return;
                }

                const auto schedule = server->m_scheduler->Get(id);
                if (!schedule.has_value()) {
                    SendJsonError(callback, "schedule not found", drogon::k404NotFound);
                    return;
                }

                Json::Value out(Json::objectValue);
                out["id"] = schedule->id;
                out["agent_id"] = schedule->agent_id;
                out["tag"] = schedule->tag;
                out["schedule_type"] =
                    schedule->type == ScheduleType::Recurring ? "recurring" : "one_shot";
                out["next_fire"] = schedule->next_fire;
                out["cron_expr"] = schedule->cron_expr;
                out["timezone"] = schedule->timezone;
                out["message"] = schedule->message;
                out["enabled"] = schedule->enabled;
                out["created_at"] = schedule->created_at;
                out["last_fire"] = schedule->last_fire;
                out["fire_count"] = schedule->fire_count;
                out["max_fires"] = schedule->max_fires;
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        drogon::app().registerHandler(
            "/api/v1/scheduler/schedules/{1}",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_scheduler) {
                    SendJsonError(callback, "scheduler is not configured", drogon::k503ServiceUnavailable);
                    return;
                }

                std::string err;
                if (!server->m_scheduler->Cancel(id, &err)) {
                    SendJsonError(
                        callback,
                        err.empty() ? "schedule not found or cannot be canceled" : err,
                        drogon::k404NotFound);
                    return;
                }

                Json::Value out(Json::objectValue);
                out["deleted"] = id;
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Delete});

        // POST /api/v1/scheduler/schedules — create schedule
        drogon::app().registerHandler(
            "/api/v1/scheduler/schedules",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                if (!server->m_scheduler) {
                    SendJsonError(callback, "scheduler is not configured",
                        drogon::k503ServiceUnavailable);
                    return;
                }

                Json::Value body;
                std::string parseErr;
                std::string bodyStr(req->getBody());
                if (!ParseJsonPayload(bodyStr, &body, &parseErr)) {
                    SendJsonError(callback, "invalid JSON: " + parseErr,
                        drogon::k400BadRequest);
                    return;
                }

                if (!body.isMember("agent_id") || !body["agent_id"].isString()) {
                    SendJsonError(callback, "agent_id is required",
                        drogon::k400BadRequest);
                    return;
                }
                if (!body.isMember("when") || !body["when"].isString()) {
                    SendJsonError(callback, "when is required (ISO timestamp or cron expression)",
                        drogon::k400BadRequest);
                    return;
                }

                ScheduleDescriptor sched;
                sched.agent_id = body["agent_id"].asString();
                sched.tag = body.get("tag", "").asString();
                if (sched.tag.empty()) {
                    sched.tag = "scheduled";
                }
                sched.next_fire = body["when"].asString();
                sched.timezone = body.get("timezone", "UTC").asString();
                sched.message = body.get("message", "").asString();
                sched.enabled = body.get("enabled", true).asBool();

                const bool repeat = body.get("repeat", false).asBool();
                sched.type = repeat ? ScheduleType::Recurring : ScheduleType::OneShot;

                // If updating existing by id
                if (body.isMember("id") && body["id"].isString() && !body["id"].asString().empty()) {
                    auto existing = server->m_scheduler->Get(body["id"].asString());
                    if (!existing) {
                        SendJsonError(callback, "schedule not found: " + body["id"].asString(),
                            drogon::k404NotFound);
                        return;
                    }
                    sched.id = body["id"].asString();
                }

                std::string error;
                const std::string newId = server->m_scheduler->Create(sched, &error);
                if (newId.empty()) {
                    SendJsonError(callback, "failed to create schedule: " + error,
                        drogon::k500InternalServerError);
                    return;
                }

                auto created = server->m_scheduler->Get(newId);
                if (!created) {
                    SendJsonError(callback, "schedule created but not retrievable",
                        drogon::k500InternalServerError);
                    return;
                }

                Json::Value out(Json::objectValue);
                out["id"] = created->id;
                out["agent_id"] = created->agent_id;
                out["tag"] = created->tag;
                out["schedule_type"] =
                    created->type == ScheduleType::Recurring ? "recurring" : "one_shot";
                out["next_fire"] = created->next_fire;
                out["cron_expr"] = created->cron_expr;
                out["timezone"] = created->timezone;
                out["message"] = created->message;
                out["enabled"] = created->enabled;
                out["created_at"] = created->created_at;

                auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                resp->setStatusCode(drogon::k201Created);
                callback(resp);
            },
            {drogon::Post});

// ============================================================================
// Legacy single-agent config API
// ============================================================================

        // ============================================================================
        // Agent config (runtime config, model, constitution read/write)
        // ====================================================================================

        drogon::app().registerHandler(
            "/api/v1/agent",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                KernelConfig::AgentRuntimeConfig snapshot;
                {
                    std::lock_guard<std::mutex> lock(server->m_agentMutex);
                    snapshot = server->m_agentConfig;
                }

                const Json::Value out = BuildAgentJson(snapshot, server->m_modelReinitCount.load());
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        drogon::app().registerHandler(
            "/api/v1/constitution",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                callback(drogon::HttpResponse::newHttpJsonResponse(
                    server->m_constitutionManager.BuildConstitutionResponse()));
            },
            {drogon::Get});

        drogon::app().registerHandler(
            "/api/v1/constitution/core",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                callback(drogon::HttpResponse::newHttpJsonResponse(
                    server->m_constitutionManager.BuildCoreLayerResponse()));
            },
            {drogon::Get});

        drogon::app().registerHandler(
            "/api/v1/constitution/contracts",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                callback(drogon::HttpResponse::newHttpJsonResponse(
                    server->m_constitutionManager.BuildContractsLayerResponse()));
            },
            {drogon::Get});

        drogon::app().registerHandler(
            "/api/v1/constitution/adaptation",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }
                callback(drogon::HttpResponse::newHttpJsonResponse(
                    server->m_constitutionManager.BuildAdaptationLayerResponse()));
            },
            {drogon::Get});

        drogon::app().registerHandler(
            "/api/v1/constitution/adaptation",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                const auto body = req->getJsonObject();
                const auto result = server->m_constitutionManager.PatchAdaptationFromBody(
                    body ? *body : Json::Value(Json::nullValue));
                auto resp = drogon::HttpResponse::newHttpJsonResponse(result.body);
                resp->setStatusCode(static_cast<drogon::HttpStatusCode>(result.httpStatusCode));
                callback(resp);
            },
            {drogon::Patch});

        // ============================================================================
        // Constitution (core/contracts/adaptation GET + PATCH)
        // ====================================================================================

        drogon::app().registerHandler(
            "/api/v1/constitution/core",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                Json::Value out;
                out["error"] =
                    "core constitution layer is read-only via API; use governance process";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                resp->setStatusCode(drogon::k405MethodNotAllowed);
                callback(resp);
            },
            {drogon::Patch});

        drogon::app().registerHandler(
            "/api/v1/constitution/contracts",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                Json::Value out;
                out["error"] =
                    "contract constitution layer is read-only via API; use governance process";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                resp->setStatusCode(drogon::k405MethodNotAllowed);
                callback(resp);
            },
            {drogon::Patch});

        drogon::app().registerHandler(
            "/api/v1/agent",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                const auto body = req->getJsonObject();
                if (!body || !body->isObject()) {
                    Json::Value out;
                    AddBadRequestError("request body must be a JSON object", &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                KernelConfig::AgentRuntimeConfig before;
                KernelConfig::AgentRuntimeConfig proposed;
                bool modelChanged = false;
                {
                    std::lock_guard<std::mutex> lock(server->m_agentMutex);
                    before = server->m_agentConfig;
                    proposed = before;
                }

                std::string patchErr;
                if (!server->m_agentManager.ApplyRuntimeConfigPatch(
                        *body, &proposed, &modelChanged, &patchErr)) {
                    Json::Value out;
                    AddBadRequestError(patchErr, &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                if (modelChanged) {
                    std::string reinitErr;
                    if (!server->TriggerModelClientReinitialization(&reinitErr)) {
                        Json::Value out;
                        out["error"] = reinitErr.empty()
                            ? "failed to reinitialize model client"
                            : reinitErr;
                        auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                        resp->setStatusCode(drogon::k500InternalServerError);
                        callback(resp);
                        return;
                    }
                }

                std::string persistErr;
                if (!server->SaveAgentConfigToDisk(proposed, &persistErr)) {
                    Json::Value out;
                    out["error"] = persistErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                {
                    std::lock_guard<std::mutex> lock(server->m_agentMutex);
                    server->m_agentConfig = proposed;
                }

                // Propagate reasoning mode changes to the chain runner
                if (server->m_chainRunner) {
                    server->m_chainRunner->UpdateReasoningMode(
                        proposed.reasoningEnabled, proposed.reasoningInstruction);
                    server->m_chainRunner->SetReasoningEffort(proposed.reasoning_effort);
                }

                const Json::Value out = BuildAgentJson(proposed, server->m_modelReinitCount.load());
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },

        // ============================================================================
        // Chat / agent runtime (prompt, model, interfaces)
        // ====================================================================================
            {drogon::Patch});

        drogon::app().registerHandler(
            "/api/v1/agent/model",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                KernelConfig::AgentRuntimeConfig snapshot;
                {
                    std::lock_guard<std::mutex> lock(server->m_agentMutex);
                    snapshot = server->m_agentConfig;
                }

                const Json::Value out = BuildModelJson(snapshot, server->m_modelReinitCount.load());
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        drogon::app().registerHandler(
            "/api/v1/agent/model",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                const auto body = req->getJsonObject();
                if (!body || !body->isObject()) {
                    Json::Value out;
                    AddBadRequestError("request body must be a JSON object", &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                if (!body->isMember("provider") || !body->isMember("model_id")) {
                    Json::Value out;
                    AddBadRequestError("provider and model_id are required", &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                KernelConfig::AgentRuntimeConfig before;
                KernelConfig::AgentRuntimeConfig proposed;
                {
                    std::lock_guard<std::mutex> lock(server->m_agentMutex);
                    before = server->m_agentConfig;
                    proposed = before;
                }

                Json::Value modelPatch(Json::objectValue);
                modelPatch["model"] = *body;

                bool modelChanged = false;
                std::string patchErr;
                if (!server->m_agentManager.ApplyRuntimeConfigPatch(
                        modelPatch, &proposed, &modelChanged, &patchErr)) {
                    Json::Value out;
                    AddBadRequestError(patchErr, &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                std::string reinitErr;
                if (!server->TriggerModelClientReinitialization(&reinitErr)) {
                    Json::Value out;
                    out["error"] = reinitErr.empty()
                        ? "failed to reinitialize model client"
                        : reinitErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                std::string persistErr;
                if (!server->SaveAgentConfigToDisk(proposed, &persistErr)) {
                    Json::Value out;
                    out["error"] = persistErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                {
                    std::lock_guard<std::mutex> lock(server->m_agentMutex);
                    server->m_agentConfig = proposed;
                }

                // Propagate reasoning mode changes to the chain runner
                if (server->m_chainRunner) {
                    server->m_chainRunner->UpdateReasoningMode(
                        proposed.reasoningEnabled, proposed.reasoningInstruction);
                    server->m_chainRunner->SetReasoningEffort(proposed.reasoning_effort);
                }

                const Json::Value out = BuildModelJson(proposed, server->m_modelReinitCount.load());
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Put});

        // ============================================================================
        // System Logs
        // ====================================================================================

        // GET /api/v1/logs?lines=N&offset=M — tail the daemon log
        drogon::app().registerHandler(
            "/api/v1/logs",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    SendServerNotActive(callback);
                    return;
                }

                // Parameters
                int maxLines = 500;
                int offset = 0;
                auto maxLinesParam = req->getParameter("lines");
                auto offsetParam = req->getParameter("offset");
                if (!maxLinesParam.empty()) {
                    try { maxLines = std::stoi(maxLinesParam); } catch (...) {}
                    if (maxLines < 1) maxLines = 100;
                    if (maxLines > 5000) maxLines = 5000;
                }
                if (!offsetParam.empty()) {
                    try { offset = std::stoi(offsetParam); } catch (...) {}
                    if (offset < 0) offset = 0;
                }

                // Resolve log file path: check ANIMUS_LOG_FILE env, then dataDir/log/animusd.log
                namespace fs = std::filesystem;
                fs::path logPath;
                const char* envLog = std::getenv("ANIMUS_LOG_FILE");
                if (envLog && *envLog) {
                    logPath = fs::path(envLog);
                    if (logPath.is_relative()) {
                        logPath = server->m_dataDir / "log" / logPath;
                    }
                }
                if (logPath.empty() || !fs::exists(logPath)) {
                    logPath = server->m_dataDir / "log" / "animusd.log";
                }
                if (!fs::exists(logPath)) {
                    // Fallback: try legacy path
                    logPath = "dist/log/animusd.log";
                }
                if (!fs::exists(logPath)) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "log file not found";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }

                // Read file, collect last N lines starting from offset
                std::ifstream logStream(logPath);
                if (!logStream.is_open()) {
                    Json::Value out(Json::objectValue);
                    out["error"] = "failed to open log file";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                // Read all lines (for simplicity — log files are typically manageable)
                // For very large logs, we'd seek from the end, but std::ifstream
                // doesn't easily support that line-by-line. This is fine for typical use.
                std::vector<std::string> allLines;
                std::string line;
                while (std::getline(logStream, line)) {
                    allLines.push_back(std::move(line));
                }

                int totalLines = static_cast<int>(allLines.size());
                int startIdx = offset;
                if (startIdx > totalLines) startIdx = totalLines;
                int endIdx = startIdx + maxLines;
                if (endIdx > totalLines) endIdx = totalLines;

                Json::Value linesArray(Json::arrayValue);
                for (int i = startIdx; i < endIdx; i++) {
                    linesArray.append(allLines[i]);
                }

                Json::Value out(Json::objectValue);
                out["lines"] = linesArray;
                out["total_lines"] = totalLines;
                out["offset"] = startIdx;
                out["returned"] = endIdx - startIdx;

                auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                callback(resp);
            },
            {drogon::Get});
