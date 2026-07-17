#pragma once

#include <string>
#include <functional>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include "animus_kernel/AuthManager.h"

namespace animus::kernel {

// ============================================================================
// AuthHelpers — utilities for extracting auth info from Drogon requests
// ============================================================================

// Extract Bearer token from Authorization header
inline std::string ExtractBearerToken(const drogon::HttpRequestPtr& req) {
    auto authHeader = req->getHeader("Authorization");
    if (authHeader.size() > 7 && authHeader.substr(0, 7) == "Bearer ") {
        return authHeader.substr(7);
    }
    return "";
}

// Extract client IP from request
inline std::string ExtractClientIp(const drogon::HttpRequestPtr& req) {
    // Check X-Forwarded-For first (behind reverse proxy)
    auto xff = req->getHeader("X-Forwarded-For");
    if (!xff.empty()) {
        // Take the first IP in the list
        auto comma = xff.find(',');
        return comma != std::string::npos ? xff.substr(0, comma) : xff;
    }
    return req->getPeerAddr().toIp();
}

// Check auth on a request. Returns true if authorized.
// If false, sends the appropriate error response and the caller should return.
inline bool CheckAuth(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    AuthManager& authManager,
    std::string& outUserId) {

    auto ip = ExtractClientIp(req);

    // Rate limit check
    if (authManager.IsRateLimited(ip)) {
        Json::Value body;
        body["error"] = "Too many failed authentication attempts. Try again later.";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
        resp->setStatusCode(drogon::k429TooManyRequests);
        resp->addHeader("Retry-After", "60");
        callback(resp);
        return false;
    }

    // Auth not required
    if (!authManager.IsAuthRequired()) {
        outUserId = "";
        return true;
    }

    auto token = ExtractBearerToken(req);
    auto [result, userId] = authManager.ValidateToken(token);

    switch (result) {
        case AuthResult::Ok:
        case AuthResult::AuthNotRequired:
            authManager.RecordSuccess(ip);
            outUserId = userId;
            return true;

        case AuthResult::InvalidToken:
        case AuthResult::NoTokenProvided:
            authManager.RecordFailure(ip);
            {
                Json::Value body;
                body["error"] = "Unauthorized";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                resp->setStatusCode(drogon::k401Unauthorized);
                callback(resp);
            }
            return false;

        default:
            {
                Json::Value body;
                body["error"] = "Authentication error";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
            return false;
    }
}

// Convenience wrapper — discards user ID
inline bool CheckAuth(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    AuthManager& authManager) {
    std::string userId;
    return CheckAuth(req, std::move(callback), authManager, userId);
}

} // namespace animus::kernel
