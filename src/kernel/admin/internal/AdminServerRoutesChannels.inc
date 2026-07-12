// ============================================================================
// Channels API — unified CRUD for all communication channels
//
// GET    /api/v1/channels                     — list channels
// POST   /api/v1/channels                     — create channel
// GET    /api/v1/channels/{name}              — get channel detail
// PATCH  /api/v1/channels/{name}              — update channel config
// DELETE /api/v1/channels/{name}              — delete channel
// POST   /api/v1/channels/{name}/enable       — enable channel
// POST   /api/v1/channels/{name}/disable      — disable channel
// POST   /api/v1/channels/moltbook/register  — register a Moltbook account
// ============================================================================

        // Build JSON representation of a channel (without secrets)
        auto BuildChannelJson = [](const ChannelState& state, bool connected) {
            Json::Value item(Json::objectValue);
            item["name"] = state.name;
            item["type"] = state.type;
            item["enabled"] = state.enabled;
            item["connected"] = connected;
            item["last_event_unix_ms"] = Json::UInt64(state.lastEventUnixMs);

            // Config — mask sensitive fields
            Json::Value config = state.config;
            if (config.isMember("access_token") && !config["access_token"].asString().empty()) config["access_token"] = "***";
            if (config.isMember("app_password") && !config["app_password"].asString().empty()) config["app_password"] = "***";
            if (config.isMember("server_password") && !config["server_password"].asString().empty()) config["server_password"] = "***";
            if (config.isMember("api_key") && !config["api_key"].asString().empty()) config["api_key"] = "***";
            if (config.isMember("bot_token") && !config["bot_token"].asString().empty()) config["bot_token"] = "***";
            item["config"] = config;

            // Identity display
            if (state.type == "irc") {
                item["identity"] = config.get("nick", "").asString();
                item["endpoint"] = config.get("host", "").asString() + ":" +
                    std::to_string(config.get("port", 6667).asInt());
            } else if (state.type == "telegram") {
                item["identity"] = config.isMember("access_token") ? "Bot" : "—";
                item["endpoint"] = "api.telegram.org";
            } else if (state.type == "vk") {
                item["identity"] = config.isMember("group_id") && config["group_id"].isString()
                    ? ("Group " + config["group_id"].asString()) : "—";
                item["endpoint"] = "api.vk.ru";
            } else if (state.type == "bluesky") {
                item["identity"] = config.get("handle", "").asString();
                item["endpoint"] = config.get("pds", "bsky.social").asString();
            } else if (state.type == "twitter") {
                item["identity"] = config.get("handle", "").asString();
                item["endpoint"] = "api.twitter.com";
            } else if (state.type == "email") {
                item["identity"] = config.get("inbox_id", "").asString();
                item["endpoint"] = "api.agentmail.to";
            } else if (state.type == "moltbook") {
                item["identity"] = config.get("agent_name", "").asString();
                item["endpoint"] = "www.moltbook.com";
            } else {
                item["identity"] = config.get("handle", "").asString();
                item["endpoint"] = config.get("pds", "—").asString();
            }

            return item;
        };

        // GET /api/v1/channels — list all channels
        drogon::app().registerHandler(
            "/api/v1/channels",
            [&BuildChannelJson](
                const drogon::HttpRequestPtr&,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_channelManager) {
                    SendJsonError(callback, "channel manager not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }

                auto channels = server->m_channelManager->ListChannels();
                Json::Value list(Json::arrayValue);
                for (const auto& ch : channels) {
                    bool connected = server->m_channelManager->IsChannelConnected(ch.name);
                    list.append(BuildChannelJson(ch, connected));
                }

                Json::Value body(Json::objectValue);
                body["channels"] = list;
                body["total"] = static_cast<Json::UInt64>(list.size());
                SendJsonResponse(callback, body);
            },
            {drogon::Get});

        // POST /api/v1/channels — create channel
        drogon::app().registerHandler(
            "/api/v1/channels",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_channelManager) {
                    SendJsonError(callback, "channel manager not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }

                Json::Value body;
                std::string parseError;
                if (!ParseJsonPayload(std::string(req->body()), &body, &parseError)) {
                    SendJsonError(callback, "invalid JSON: " + parseError,
                        drogon::k400BadRequest);
                    return;
                }

                std::string name = body.get("name", "").asString();
                std::string type = body.get("type", "").asString();
                if (name.empty()) {
                    SendJsonError(callback, "name is required", drogon::k400BadRequest);
                    return;
                }
                if (type.empty()) {
                    SendJsonError(callback, "type is required", drogon::k400BadRequest);
                    return;
                }

                ChannelState state;
                state.name = name;
                state.type = type;
                state.enabled = body.get("enabled", true).asBool();
                state.config = body.get("config", Json::objectValue);
                if (state.config.isNull()) state.config = Json::objectValue;

                std::string error;
                if (!server->m_channelManager->CreateChannel(state, &error)) {
                    SendJsonError(callback, error, drogon::k400BadRequest);
                    return;
                }

                Json::Value response(Json::objectValue);
                response["name"] = name;
                response["type"] = type;
                response["created"] = true;
                SendJsonResponse(callback, response, drogon::k201Created);
            },
            {drogon::Post});

        // GET /api/v1/channels/{name} — get channel detail
        drogon::app().registerHandler(
            "/api/v1/channels/{name}",
            [&BuildChannelJson](
                const drogon::HttpRequestPtr&,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                const std::string& name) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_channelManager) {
                    SendJsonError(callback, "channel manager not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }

                auto state = server->m_channelManager->GetChannel(name);
                if (!state) {
                    SendJsonError(callback, "channel not found: " + name,
                        drogon::k404NotFound);
                    return;
                }

                bool connected = server->m_channelManager->IsChannelConnected(name);
                SendJsonResponse(callback, BuildChannelJson(*state, connected));
            },
            {drogon::Get});

        // PATCH /api/v1/channels/{name} — update channel config
        drogon::app().registerHandler(
            "/api/v1/channels/{name}",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& name) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_channelManager) {
                    SendJsonError(callback, "channel manager not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }

                Json::Value body;
                std::string parseError;
                if (!ParseJsonPayload(std::string(req->body()), &body, &parseError)) {
                    SendJsonError(callback, "invalid JSON: " + parseError,
                        drogon::k400BadRequest);
                    return;
                }

                // Merge: keep existing secrets if new values are empty/masked
                auto existing = server->m_channelManager->GetChannel(name);
                if (!existing) {
                    SendJsonError(callback, "channel not found: " + name,
                        drogon::k404NotFound);
                    return;
                }

                Json::Value config = body.get("config", Json::objectValue);
                if (config.isNull()) config = Json::objectValue;

                // Restore masked secrets
                for (const auto& key : {"access_token", "app_password", "server_password", "client_secret", "refresh_token", "api_key", "bot_token"}) {
                    if (config.isMember(key) && config[key].isString()) {
                        std::string val = config[key].asString();
                        if (val.empty() || val == "***") {
                            if (existing->config.isMember(key)) {
                                config[key] = existing->config[key];
                            }
                        }
                    }
                }

                std::string error;
                if (!server->m_channelManager->UpdateChannelConfig(name, config, &error)) {
                    SendJsonError(callback, error, drogon::k400BadRequest);
                    return;
                }

                Json::Value response(Json::objectValue);
                response["name"] = name;
                response["updated"] = true;
                SendJsonResponse(callback, response);
            },
            {drogon::Patch});

        // DELETE /api/v1/channels/{name} — delete channel
        drogon::app().registerHandler(
            "/api/v1/channels/{name}",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& name) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_channelManager) {
                    SendJsonError(callback, "channel manager not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }

                std::string error;
                if (!server->m_channelManager->DeleteChannel(name, &error)) {
                    SendJsonError(callback, error,
                        error.find("not found") != std::string::npos
                            ? drogon::k404NotFound : drogon::k400BadRequest);
                    return;
                }

                Json::Value response(Json::objectValue);
                response["deleted"] = true;
                response["name"] = name;
                SendJsonResponse(callback, response);
            },
            {drogon::Delete});

        // POST /api/v1/channels/{name}/enable
        drogon::app().registerHandler(
            "/api/v1/channels/{name}/enable",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& name) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_channelManager) {
                    SendJsonError(callback, "channel manager not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }

                std::string error;
                if (!server->m_channelManager->SetChannelEnabled(name, true, &error)) {
                    SendJsonError(callback, error, drogon::k400BadRequest);
                    return;
                }

                Json::Value response(Json::objectValue);
                response["name"] = name;
                response["enabled"] = true;
                SendJsonResponse(callback, response);
            },
            {drogon::Post});

        // POST /api/v1/channels/{name}/disable
        drogon::app().registerHandler(
            "/api/v1/channels/{name}/disable",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& name) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_channelManager) {
                    SendJsonError(callback, "channel manager not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }

                std::string error;
                if (!server->m_channelManager->SetChannelEnabled(name, false, &error)) {
                    SendJsonError(callback, error, drogon::k400BadRequest);
                    return;
                }

                Json::Value response(Json::objectValue);
                response["name"] = name;
                response["enabled"] = false;
                SendJsonResponse(callback, response);
            },
            {drogon::Post});

        // ====================================================================
        // Twitter OAuth 2.0 PKCE flow
        // ====================================================================

        // GET /api/v1/channels/{name}/auth/twitter — start OAuth flow
        // Returns: { authorize_url, state }
        drogon::app().registerHandler(
            "/api/v1/channels/{name}/auth/twitter",
            [](
                const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                const std::string& name) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_channelManager) {
                    SendJsonError(callback, "channel manager not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }

                auto state = server->m_channelManager->GetChannel(name);
                if (!state) {
                    SendJsonError(callback, "channel not found: " + name,
                        drogon::k404NotFound);
                    return;
                }

                if (state->type != "twitter") {
                    SendJsonError(callback, "channel is not a Twitter channel",
                        drogon::k400BadRequest);
                    return;
                }

                const Json::Value& config = state->config;
                std::string clientId = config.get("client_id", "").asString();
                if (clientId.empty()) {
                    SendJsonError(callback, "Twitter channel missing client_id in config",
                        drogon::k400BadRequest);
                    return;
                }

                // Build redirect URI from the admin server's base URL
                // Default: http://localhost:1455/api/v1/channels/{name}/auth/twitter/callback
                std::string baseUrl = req->getHeader("host");
                if (baseUrl.empty()) baseUrl = "localhost:1455";
                std::string scheme = "http";  // Admin server runs HTTP locally
                std::string redirectUri = scheme + "://" + baseUrl +
                    "/api/v1/channels/" + UrlEncode(name) + "/auth/twitter/callback";

                const auto nowMs = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                    .count());

                const auto result = server->m_oauthManager.StartTwitterOAuthFlow(
                    name, clientId, redirectUri, nowMs);

                if (result.httpStatusCode != 200) {
                    SendJsonError(callback, result.body.get("error", "failed to start OAuth flow").asString(),
                        static_cast<drogon::HttpStatusCode>(result.httpStatusCode));
                    return;
                }

                SendJsonResponse(callback, result.body,
                    static_cast<drogon::HttpStatusCode>(result.httpStatusCode));
            },
            {drogon::Get});

        // GET /api/v1/channels/{name}/auth/twitter/callback — handle OAuth callback
        // Twitter redirects here with ?code=...&state=...
        // Exchanges code for tokens and stores them in channel config
        drogon::app().registerHandler(
            "/api/v1/channels/{name}/auth/twitter/callback",
            [](
                const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                const std::string& name) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) { SendServerNotActive(callback); return; }
                if (!server->m_channelManager) {
                    SendJsonError(callback, "channel manager not available",
                        drogon::k503ServiceUnavailable);
                    return;
                }

                // Check for error parameter (user denied access, etc.)
                std::string errorParam;
                {
                    auto params = req->getParameters();
                    for (const auto& [key, val] : params) {
                        if (key == "error") errorParam = val;
                    }
                }
                if (!errorParam.empty()) {
                    SendJsonError(callback, "Twitter OAuth error: " + errorParam,
                        drogon::k400BadRequest);
                    return;
                }

                // Extract code and state from query parameters
                std::string code;
                std::string state;
                {
                    auto params = req->getParameters();
                    for (const auto& [key, val] : params) {
                        if (key == "code") code = val;
                        if (key == "state") state = val;
                    }
                }

                if (code.empty()) {
                    SendJsonError(callback, "Twitter OAuth callback missing authorization code",
                        drogon::k400BadRequest);
                    return;
                }
                if (state.empty()) {
                    SendJsonError(callback, "Twitter OAuth callback missing state parameter",
                        drogon::k400BadRequest);
                    return;
                }

                // Get channel config for client_id and client_secret
                auto channelState = server->m_channelManager->GetChannel(name);
                if (!channelState) {
                    SendJsonError(callback, "channel not found: " + name,
                        drogon::k404NotFound);
                    return;
                }

                const Json::Value& config = channelState->config;
                std::string clientId = config.get("client_id", "").asString();
                std::string clientSecret = config.get("client_secret", "").asString();

                // Build redirect URI (must match the one used in start flow)
                std::string baseUrl = req->getHeader("host");
                if (baseUrl.empty()) baseUrl = "localhost:1455";
                std::string redirectUri = "http://" + baseUrl +
                    "/api/v1/channels/" + UrlEncode(name) + "/auth/twitter/callback";

                const auto result = server->m_oauthManager.CompleteTwitterOAuthFlow(
                    name, clientId, clientSecret, code, state, redirectUri);

                if (result.httpStatusCode != 200) {
                    // Show a human-friendly error page since this is a browser redirect
                    std::string error = result.body.get("error", "unknown error").asString();
                    std::string html = "<!DOCTYPE html><html><head><title>Auth Failed</title>"
                        "<style>body{font-family:system-ui;display:flex;justify-content:center;"
                        "align-items:center;min-height:100vh;margin:0;background:#1a1a2e;color:#eee}"
                        ".card{background:#16213e;border-radius:12px;padding:2rem;max-width:480px}"
                        "h1{color:#e94560;margin-top:0}code{background:#0f3460;padding:2px 6px;"
                        "border-radius:4px;font-size:0.9em}</style></head>"
                        "<body><div class=\"card\"><h1>Twitter Auth Failed</h1>"
                        "<p>" + error + "</p>"
                        "<p><a href=\"/channels\">Back to Channels</a></p></div></body></html>";
                    auto resp = drogon::HttpResponse::newHttpResponse();
                    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(result.httpStatusCode));
                    resp->setContentTypeString("text/html");
                    resp->setBody(html);
                    callback(resp);
                    return;
                }

                // Store tokens in channel config
                const Json::Value& tokens = result.body["tokens"];
                Json::Value newConfig = config;
                newConfig["access_token"] = tokens.get("access_token", "").asString();
                newConfig["refresh_token"] = tokens.get("refresh_token", "").asString();
                newConfig["token_type"] = tokens.get("token_type", "bearer").asString();
                newConfig["token_expires_ms"] = tokens.get("expires_at_ms", 0).asInt64();

                std::string updateError;
                if (!server->m_channelManager->UpdateChannelConfig(name, newConfig, &updateError)) {
                    SendJsonError(callback, "tokens received but failed to update channel config: " + updateError,
                        drogon::k500InternalServerError);
                    return;
                }

                // Show success page in browser
                std::string html = "<!DOCTYPE html><html><head><title>Auth Success</title>"
                    "<style>body{font-family:system-ui;display:flex;justify-content:center;"
                    "align-items:center;min-height:100vh;margin:0;background:#1a1a2e;color:#eee}"
                    ".card{background:#16213e;border-radius:12px;padding:2rem;max-width:480px}"
                    "h1{color:#0f9d58;margin-top:0}</style></head>"
                    "<body><div class=\"card\">"
                    "<h1>&#x2713; Twitter Connected</h1>"
                    "<p>Tokens stored. You can close this tab.</p>"
                    "<p><a href=\"/channels\">Back to Channels</a></p>"
                    "</div></body></html>";
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k200OK);
                resp->setContentTypeString("text/html");
                resp->setBody(html);
                callback(resp);
            },
            {drogon::Get});

    // GET /api/v1/channels/{name}/whatsapp/qr — get WhatsApp QR code URL
    drogon::app().registerHandler(
        "/api/v1/channels/{name}/whatsapp/qr",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           const std::string& name) {
            AdminServer* server = g_activeAdminServer.load();
            if (!server) { SendServerNotActive(callback); return; }

            Json::Value result(Json::objectValue);

            if (!server->m_channelManager) {
                result["ok"] = false;
                result["error"] = "Channel manager not available";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                resp->setStatusCode(drogon::k503ServiceUnavailable);
                callback(resp);
                return;
            }

            auto channelOpt = server->m_channelManager->GetChannel(name);
            if (!channelOpt.has_value() || channelOpt->type != "whatsapp") {
                result["ok"] = false;
                result["error"] = "Not a WhatsApp channel";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                resp->setStatusCode(drogon::k404NotFound);
                callback(resp);
                return;
            }

            std::string qrUrl = server->m_channelManager->GetWhatsAppQrUrl(name);
            result["ok"] = true;
            if (qrUrl.empty()) {
                result["qr_url"] = "";
                result["status"] = "waiting";  // Gateway not yet connected or QR not generated
            } else {
                result["qr_url"] = qrUrl;
                result["status"] = "ready";
            }

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            resp->setStatusCode(drogon::k200OK);
            callback(resp);
        },
        {drogon::Get});

    // POST /api/v1/channels/{name}/whatsapp/clear-auth — clear WhatsApp auth state
    // Deletes the auth.json file and restarts the channel to force re-pairing
    drogon::app().registerHandler(
        "/api/v1/channels/{name}/whatsapp/clear-auth",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           const std::string& name) {
            AdminServer* server = g_activeAdminServer.load();
            if (!server) { SendServerNotActive(callback); return; }

            Json::Value result(Json::objectValue);

            if (!server->m_channelManager) {
                result["ok"] = false;
                result["error"] = "Channel manager not available";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                resp->setStatusCode(drogon::k503ServiceUnavailable);
                callback(resp);
                return;
            }

            auto channelOpt = server->m_channelManager->GetChannel(name);
            if (!channelOpt.has_value() || channelOpt->type != "whatsapp") {
                result["ok"] = false;
                result["error"] = "Not a WhatsApp channel";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                resp->setStatusCode(drogon::k404NotFound);
                callback(resp);
                return;
            }

            // Find the auth directory and delete auth.json
            std::string authDir = channelOpt->config.get("auth_dir", "/tmp/wa-auth").asString();
            std::string authFile = authDir + "/auth.json";
            bool deleted = false;
            if (std::filesystem::exists(authFile)) {
                std::filesystem::remove(authFile);
                deleted = true;
            }

            // Restart the channel to trigger fresh pairing
            std::string error;
            server->m_channelManager->SetChannelEnabled(name, false, &error);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            server->m_channelManager->SetChannelEnabled(name, true, &error);

            result["ok"] = true;
            result["deleted"] = deleted;
            result["message"] = "Auth state cleared. A new QR code will be generated for re-pairing.";
            SendJsonResponse(callback, result);
        },
        {drogon::Post});

    // ====================================================================
    // Moltbook account registration
    // ====================================================================

    // POST /api/v1/channels/moltbook/register — register a new Moltbook account
    // The daemon calls Moltbook's /agents/register endpoint on behalf of the Admin UI.
    // Returns: { api_key, claim_url, verification_code, tweet_text, agent }
    //
    // The human must then post the tweet_text from their personal Twitter account.
    // Moltbook verifies the tweet to activate the account.
    drogon::app().registerHandler(
        "/api/v1/channels/moltbook/register",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            Json::Value body;
            std::string parseError;
            if (!ParseJsonPayload(std::string(req->body()), &body, &parseError)) {
                SendJsonError(callback, "invalid JSON: " + parseError,
                    drogon::k400BadRequest);
                return;
            }

            std::string agentName = body.get("name", "").asString();
            std::string description = body.get("description", "").asString();

            if (agentName.empty()) {
                SendJsonError(callback, "name is required",
                    drogon::k400BadRequest);
                return;
            }

            // Build registration request to Moltbook API
            Json::Value regBody(Json::objectValue);
            regBody["name"] = agentName;
            if (!description.empty()) regBody["description"] = description;

            // Use the synchronous HttpClient (libcurl) to call Moltbook
            // Drogon's async HttpClient has issues with redirects on POST requests
            HttpClient http;
            http.SetAllowPrivateAddresses(false);
            http.SetMaxBodySize(64 * 1024);  // 64KB is plenty for registration response

            Json::StreamWriterBuilder wb;
            HttpClient::Request httpReq;
            httpReq.method = "POST";
            httpReq.url = "https://www.moltbook.com/api/v1/agents/register";
            httpReq.headers["Content-Type"] = "application/json";
            httpReq.headers["Accept"] = "application/json";
            httpReq.body = Json::writeString(wb, regBody);
            httpReq.timeout_seconds = 15;
            httpReq.follow_redirects = true;

            auto httpResp = http.Execute(httpReq);

            if (!httpResp.error.empty()) {
                SendJsonError(callback,
                    "Moltbook registration request failed: " + httpResp.error,
                    drogon::k502BadGateway);
                return;
            }

            Json::Value respBody;
            std::string parseErr;
            if (!ParseJsonPayload(httpResp.body, &respBody, &parseErr)) {
                SendJsonError(callback,
                    "invalid response from Moltbook: " + parseErr,
                    drogon::k502BadGateway);
                return;
            }

            if (httpResp.status_code >= 400) {
                std::string err = respBody.get("error", "").asString();
                if (err.empty()) err = respBody.get("message", "").asString();
                if (err.empty()) err = "HTTP " + std::to_string(httpResp.status_code);
                SendJsonError(callback, err,
                    static_cast<drogon::HttpStatusCode>(httpResp.status_code));
                return;
            }

            // Extract registration result
            Json::Value agent = respBody.get("agent", Json::objectValue);
            std::string apiKey = agent.get("api_key", "").asString();
            std::string claimUrl = agent.get("claim_url", "").asString();
            std::string verificationCode = agent.get("verification_code", "").asString();

            // Moltbook provides a ready-made tweet template — use it if available
            std::string tweetText = respBody.get("tweet_template", "").asString();
            if (tweetText.empty() && !verificationCode.empty()) {
                // Fallback: construct our own tweet text
                tweetText = "I'm verifying my Moltbook agent account: " + verificationCode;
            }

            Json::Value response(Json::objectValue);
            response["ok"] = true;
            response["api_key"] = apiKey;
            response["claim_url"] = claimUrl;
            response["verification_code"] = verificationCode;
            response["tweet_text"] = tweetText;
            response["agent_name"] = agentName;

            SendJsonResponse(callback, response);
        },
        {drogon::Post});
