        // ===================================================================
        // SOP API (Ticket 125)
        // ===================================================================

        // GET /api/v1/sops — list all SOPs
        drogon::app().registerHandler(
            "/api/v1/sops",
            [this](const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                if (!m_sopStore) {
                    Json::Value body;
                    body["error"] = "SOP store not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                std::string category = req->getParameter("category");
                std::string search = req->getParameter("q");

                Json::Value body;
                Json::Value items(Json::arrayValue);

                std::vector<SopMeta> sops;
                if (!search.empty()) {
                    sops = m_sopStore->Search(search, 1, 100);
                } else {
                    sops = m_sopStore->List(category, 1, 100);
                }

                for (const auto& s : sops) {
                    Json::Value item;
                    item["name"] = s.name;
                    item["title"] = s.title;
                    item["category"] = s.category;
                    item["version"] = s.version;
                    item["description"] = s.description;
                    Json::Value tags(Json::arrayValue);
                    for (const auto& t : s.tags) tags.append(t);
                    item["tags"] = tags;
                    items.append(item);
                }

                body["sops"] = items;
                body["total"] = static_cast<Json::UInt64>(sops.size());
                callback(drogon::HttpResponse::newHttpJsonResponse(body));
            },
            {drogon::Get, drogon::Options});

        // GET /api/v1/sops/:name — get full SOP content
        drogon::app().registerHandler(
            "/api/v1/sops/{name}",
            [this](const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& name) {
                if (!m_sopStore) {
                    Json::Value body;
                    body["error"] = "SOP store not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                auto sop = m_sopStore->Get(name);
                if (!sop) {
                    Json::Value body;
                    body["error"] = "SOP not found: " + name;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }

                Json::Value body;
                body["name"] = sop->meta.name;
                body["title"] = sop->meta.title;
                body["category"] = sop->meta.category;
                body["version"] = sop->meta.version;
                body["description"] = sop->meta.description;
                Json::Value tags(Json::arrayValue);
                for (const auto& t : sop->meta.tags) tags.append(t);
                body["tags"] = tags;
                body["content"] = sop->content;

                callback(drogon::HttpResponse::newHttpJsonResponse(body));
            },
            {drogon::Get, drogon::Options});

        // POST /api/v1/sops/:name/install — install SOP to an agent's MemoryFiles
        drogon::app().registerHandler(
            "/api/v1/sops/{name}/install",
            [this](const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& name) {
                if (!m_sopStore) {
                    Json::Value body;
                    body["error"] = "SOP store not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                if (!m_memoryFileStore) {
                    Json::Value body;
                    body["error"] = "Memory file store not available";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                // Parse agent_id from request body
                Json::Value requestBody;
                auto bodyStr = req->getBody();
                if (!bodyStr.empty()) {
                    Json::CharReaderBuilder builder;
                    std::string errors;
                    std::istringstream stream(std::string(bodyStr.data(), bodyStr.size()));
                    if (!Json::parseFromStream(builder, stream, &requestBody, &errors)) {
                        Json::Value err;
                        err["error"] = "Invalid JSON body";
                        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
                        resp->setStatusCode(drogon::k400BadRequest);
                        callback(resp);
                        return;
                    }
                }

                int64_t agentId = requestBody.get("agent_id", 0).asInt64();
                if (agentId == 0) {
                    Json::Value err;
                    err["error"] = "Missing required field: agent_id";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                auto sop = m_sopStore->Get(name);
                if (!sop) {
                    Json::Value err;
                    err["error"] = "SOP not found: " + name;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }

                // Write to MemoryFiles
                std::string memoryPath = "sop." + sop->meta.name;
                memory::MemoryFile mf;
                mf.source_path = memoryPath;
                mf.file_type = memory::MemoryFileType::ExternalDoc;
                mf.content = sop->content;
                mf.content_mutable = true;
                mf.agent_id = agentId;
                mf.status = memory::MemoryFileStatus::Unprocessed;

                auto created = m_memoryFileStore->CreateFile(mf);

                Json::Value body;
                body["success"] = true;
                body["sop"] = name;
                body["title"] = sop->meta.title;
                body["agent_id"] = static_cast<Json::Int64>(agentId);
                body["memory_file_id"] = static_cast<Json::Int64>(created.id);
                body["memory_path"] = memoryPath;
                body["message"] = "SOP installed to agent memory. Will be processed by consolidation pipeline.";

                auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                resp->setStatusCode(drogon::k201Created);
                callback(resp);
            },
            {drogon::Post, drogon::Options});
