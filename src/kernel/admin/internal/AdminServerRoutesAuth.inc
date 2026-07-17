        // ===================================================================
        // Authentication API
        // ===================================================================

        // GET /api/v1/auth/status — check if auth is required and users exist
        drogon::app().registerHandler(
            "/api/v1/auth/status",
            [this](const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                Json::Value body;
                body["auth_required"] = m_authManager.IsAuthRequired();
                body["has_users"] = m_authManager.HasUsers();
                body["has_static_token"] = !m_staticTokenRaw.empty();
                callback(drogon::HttpResponse::newHttpJsonResponse(body));
            },
            {drogon::Get, drogon::Options});

        // POST /api/v1/auth/login — login with username/password
        drogon::app().registerHandler(
            "/api/v1/auth/login",
            [this](const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                auto ip = ExtractClientIp(req);

                // Rate limit
                if (m_authManager.IsRateLimited(ip)) {
                    Json::Value body;
                    body["error"] = "Too many failed attempts. Try again later.";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k429TooManyRequests);
                    resp->addHeader("Retry-After", "60");
                    callback(resp);
                    return;
                }

                auto jsonBody = req->getJsonObject();
                if (!jsonBody || !jsonBody->isObject()) {
                    Json::Value body;
                    body["error"] = "Invalid request body";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                std::string username = jsonBody->get("username", "").asString();
                std::string password = jsonBody->get("password", "").asString();

                if (username.empty() || password.empty()) {
                    Json::Value body;
                    body["error"] = "Username and password required";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                auto user = m_authManager.GetUserByUsername(username);
                if (!user) {
                    m_authManager.RecordFailure(ip);
                    Json::Value body;
                    body["error"] = "Invalid credentials";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k401Unauthorized);
                    callback(resp);
                    return;
                }

                // Verify password — stored as "salt:hash"
                auto& ph = user->password_hash;
                auto colon = ph.find(':');
                if (colon == std::string::npos) {
                    m_authManager.RecordFailure(ip);
                    Json::Value body;
                    body["error"] = "Invalid credentials";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k401Unauthorized);
                    callback(resp);
                    return;
                }

                std::string salt = ph.substr(0, colon);
                std::string hash = ph.substr(colon + 1);

                if (!m_authManager.VerifyPassword(password, salt, hash)) {
                    m_authManager.RecordFailure(ip);
                    Json::Value body;
                    body["error"] = "Invalid credentials";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k401Unauthorized);
                    callback(resp);
                    return;
                }

                // Success — create session token
                m_authManager.RecordSuccess(ip);

                std::string rawToken;
                int64_t ttlMs = 168ULL * 3600 * 1000;  // 7 days
                if (!m_authManager.CreateSessionToken(user->id, ttlMs, rawToken)) {
                    Json::Value body;
                    body["error"] = "Failed to create session token";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                Json::Value body;
                body["token"] = rawToken;
                body["user_id"] = user->id;
                body["username"] = user->username;
                body["role"] = user->role;
                body["expires_at"] = static_cast<Json::Int64>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count() + ttlMs);
                callback(drogon::HttpResponse::newHttpJsonResponse(body));
            },
            {drogon::Post, drogon::Options});

        // POST /api/v1/auth/logout — invalidate current session token
        drogon::app().registerHandler(
            "/api/v1/auth/logout",
            [this](const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                std::string userId;
                if (!CheckAuth(req, std::move(callback), m_authManager, userId)) return;

                auto token = ExtractBearerToken(req);
                auto tokenHash = m_authManager.HashToken(token);
                m_authManager.RevokeToken(tokenHash);

                Json::Value body;
                body["status"] = "ok";
                callback(drogon::HttpResponse::newHttpJsonResponse(body));
            },
            {drogon::Post, drogon::Options});

        // GET /api/v1/auth/me — current user info
        drogon::app().registerHandler(
            "/api/v1/auth/me",
            [this](const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                std::string userId;
                if (!CheckAuth(req, std::move(callback), m_authManager, userId)) return;

                Json::Value body;
                if (userId.empty()) {
                    // Static token auth — no user
                    body["auth_method"] = "static_token";
                } else {
                    auto user = m_authManager.GetUserById(userId);
                    if (user) {
                        body["auth_method"] = "session";
                        body["user_id"] = user->id;
                        body["username"] = user->username;
                        body["role"] = user->role;
                    } else {
                        body["error"] = "User not found";
                        auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                        resp->setStatusCode(drogon::k401Unauthorized);
                        callback(resp);
                        return;
                    }
                }
                callback(drogon::HttpResponse::newHttpJsonResponse(body));
            },
            {drogon::Get, drogon::Options});

        // POST /api/v1/auth/setup — create first user account (requires static token)
        drogon::app().registerHandler(
            "/api/v1/auth/setup",
            [this](const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                // Must be authenticated with static token
                std::string userId;
                if (!CheckAuth(req, std::move(callback), m_authManager, userId)) return;

                if (!userId.empty()) {
                    Json::Value body;
                    body["error"] = "Setup can only be done with the static auth token";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k403Forbidden);
                    callback(resp);
                    return;
                }

                if (m_authManager.HasUsers()) {
                    Json::Value body;
                    body["error"] = "Users already exist. Use login instead.";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k409Conflict);
                    callback(resp);
                    return;
                }

                auto jsonBody = req->getJsonObject();
                if (!jsonBody || !jsonBody->isObject()) {
                    Json::Value body;
                    body["error"] = "Invalid request body";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                std::string username = jsonBody->get("username", "").asString();
                std::string password = jsonBody->get("password", "").asString();
                std::string role = jsonBody->get("role", "admin").asString();

                if (username.empty() || password.empty()) {
                    Json::Value body;
                    body["error"] = "Username and password required";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                if (!m_authManager.CreateUser(username, password, role)) {
                    Json::Value body;
                    body["error"] = "Failed to create user";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                Json::Value body;
                body["status"] = "ok";
                body["username"] = username;
                body["role"] = role;
                callback(drogon::HttpResponse::newHttpJsonResponse(body));
            },
            {drogon::Post, drogon::Options});

        // ===================================================================
        // User Management API (admin only)
        // ===================================================================

        // GET /api/v1/users — list all users
        drogon::app().registerHandler(
            "/api/v1/users",
            [this](const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                std::string userId;
                if (!CheckAuth(req, std::move(callback), m_authManager, userId)) return;

                auto users = m_authManager.ListUsers();
                Json::Value body(Json::arrayValue);
                for (const auto& u : users) {
                    Json::Value userObj;
                    userObj["id"] = u.id;
                    userObj["username"] = u.username;
                    userObj["role"] = u.role;
                    userObj["created_at"] = static_cast<Json::Int64>(u.created_at);
                    body.append(userObj);
                }
                callback(drogon::HttpResponse::newHttpJsonResponse(body));
            },
            {drogon::Get, drogon::Options});

        // POST /api/v1/users — create user
        drogon::app().registerHandler(
            "/api/v1/users",
            [this](const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                std::string userId;
                if (!CheckAuth(req, std::move(callback), m_authManager, userId)) return;

                auto jsonBody = req->getJsonObject();
                if (!jsonBody) {
                    Json::Value body;
                    body["error"] = "Invalid request body";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                std::string username = jsonBody->get("username", "").asString();
                std::string password = jsonBody->get("password", "").asString();
                std::string role = jsonBody->get("role", "admin").asString();

                if (username.empty() || password.empty()) {
                    Json::Value body;
                    body["error"] = "Username and password required";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                // Check for duplicate
                if (m_authManager.GetUserByUsername(username)) {
                    Json::Value body;
                    body["error"] = "Username already exists";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k409Conflict);
                    callback(resp);
                    return;
                }

                if (!m_authManager.CreateUser(username, password, role)) {
                    Json::Value body;
                    body["error"] = "Failed to create user";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                Json::Value body;
                body["status"] = "ok";
                body["username"] = username;
                callback(drogon::HttpResponse::newHttpJsonResponse(body));
            },
            {drogon::Post, drogon::Options});

        // PUT /api/v1/users/:id — update user (password and/or role)
        drogon::app().registerHandler(
            "/api/v1/users/{id}",
            [this](const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id) {
                std::string authUserId;
                if (!CheckAuth(req, std::move(callback), m_authManager, authUserId)) return;

                auto jsonBody = req->getJsonObject();
                if (!jsonBody) {
                    Json::Value body;
                    body["error"] = "Invalid request body";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                auto user = m_authManager.GetUserById(id);
                if (!user) {
                    Json::Value body;
                    body["error"] = "User not found";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }

                if (jsonBody->isMember("password")) {
                    std::string newPassword = jsonBody->get("password", "").asString();
                    if (!newPassword.empty()) {
                        m_authManager.UpdateUserPassword(id, newPassword);
                    }
                }
                if (jsonBody->isMember("role")) {
                    std::string newRole = (*jsonBody)["role"].asString();
                    m_authManager.UpdateUserRole(id, newRole);
                }

                Json::Value body;
                body["status"] = "ok";
                callback(drogon::HttpResponse::newHttpJsonResponse(body));
            },
            {drogon::Put, drogon::Options});

        // DELETE /api/v1/users/:id — delete user
        drogon::app().registerHandler(
            "/api/v1/users/{id}",
            [this](const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id) {
                std::string authUserId;
                if (!CheckAuth(req, std::move(callback), m_authManager, authUserId)) return;

                auto user = m_authManager.GetUserById(id);
                if (!user) {
                    Json::Value body;
                    body["error"] = "User not found";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }

                m_authManager.DeleteUser(id);

                Json::Value body;
                body["status"] = "ok";
                callback(drogon::HttpResponse::newHttpJsonResponse(body));
            },
            {drogon::Delete, drogon::Options});

        // GET /api/v1/users/:id/sessions — list active tokens for user
        drogon::app().registerHandler(
            "/api/v1/users/{id}/sessions",
            [this](const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id) {
                std::string authUserId;
                if (!CheckAuth(req, std::move(callback), m_authManager, authUserId)) return;

                auto tokens = m_authManager.ListTokensForUser(id);
                Json::Value body(Json::arrayValue);
                for (const auto& t : tokens) {
                    Json::Value tokenObj;
                    tokenObj["created_at"] = static_cast<Json::Int64>(t.created_at);
                    tokenObj["expires_at"] = static_cast<Json::Int64>(t.expires_at);
                    // Don't expose the token hash
                    body.append(tokenObj);
                }
                callback(drogon::HttpResponse::newHttpJsonResponse(body));
            },
            {drogon::Get, drogon::Options});

        // DELETE /api/v1/users/:id/sessions — revoke all tokens for user
        drogon::app().registerHandler(
            "/api/v1/users/{id}/sessions",
            [this](const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id) {
                std::string authUserId;
                if (!CheckAuth(req, std::move(callback), m_authManager, authUserId)) return;

                m_authManager.RevokeAllTokensForUser(id);

                Json::Value body;
                body["status"] = "ok";
                callback(drogon::HttpResponse::newHttpJsonResponse(body));
            },
            {drogon::Delete, drogon::Options});
