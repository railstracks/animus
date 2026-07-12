#include "animus_kernel/admin/OAuthManager.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <curl/curl.h>

namespace animus::kernel {
namespace {

constexpr const char* kOpenAICodexClientId = "app_EMoamEEZ73f0CkXaXp7hrann";
constexpr const char* kOpenAICodexTokenEndpoint = "https://auth.openai.com/oauth/token";
constexpr const char* kOpenAICodexAuthorizeEndpoint = "https://auth.openai.com/oauth/authorize";
constexpr std::uint64_t kOpenAICodexBrowserFlowTtlMs = 15ULL * 60ULL * 1000ULL;

struct CurlHttpResult {
    long status{0};
    std::string body;
    std::string error;
};

size_t CurlWriteToString(void* ptr, size_t size, size_t nmemb, void* data) {
    const size_t total = size * nmemb;
    auto* out = static_cast<std::string*>(data);
    out->append(static_cast<const char*>(ptr), total);
    return total;
}

CurlHttpResult CurlPost(
    const std::string& url,
    const std::string& body,
    const std::vector<std::string>& headers,
    long timeoutMs = 20000) {
    CurlHttpResult result;
    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = "curl init failed";
        return result;
    }

    struct curl_slist* headerList = nullptr;
    for (const auto& h : headers) {
        headerList = curl_slist_append(headerList, h.c_str());
    }

    char errbuf[CURL_ERROR_SIZE] = {};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

    const CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        result.error = errbuf[0] ? errbuf : curl_easy_strerror(res);
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
    }

    curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);
    return result;
}

std::string JsonErrorMessage(const Json::Value& root) {
    if (root.isMember("error") && root["error"].isObject()) {
        const auto& err = root["error"];
        if (err.isMember("message") && err["message"].isString()) {
            return err["message"].asString();
        }
        if (err.isMember("code") && err["code"].isString()) {
            return err["code"].asString();
        }
    }
    if (root.isMember("detail") && root["detail"].isString()) {
        return root["detail"].asString();
    }
    return {};
}

bool ParseJsonPayload(const std::string& payload, Json::Value* out, std::string* error) {
    if (!out) {
        return false;
    }
    Json::CharReaderBuilder builder;
    std::istringstream stream(payload);
    std::string parseErrors;
    if (!Json::parseFromStream(builder, stream, out, &parseErrors)) {
        if (error) {
            *error = parseErrors;
        }
        return false;
    }
    return true;
}

std::string UrlEncode(const std::string& input) {
    CURL* curl = curl_easy_init();
    if (!curl) return {};
    char* encoded = curl_easy_escape(curl, input.c_str(), static_cast<int>(input.size()));
    std::string out = encoded ? std::string(encoded) : std::string();
    if (encoded) curl_free(encoded);
    curl_easy_cleanup(curl);
    return out;
}

std::string UrlDecode(const std::string& input) {
    std::string normalized = input;
    for (char& c : normalized) {
        if (c == '+') {
            c = ' ';
        }
    }
    CURL* curl = curl_easy_init();
    if (!curl) return normalized;
    int outLen = 0;
    char* decoded = curl_easy_unescape(curl, normalized.c_str(), static_cast<int>(normalized.size()), &outLen);
    std::string out = decoded ? std::string(decoded, static_cast<std::size_t>(std::max(outLen, 0))) : normalized;
    if (decoded) curl_free(decoded);
    curl_easy_cleanup(curl);
    return out;
}

std::vector<std::uint8_t> RandomBytes(std::size_t count) {
    thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 255);
    std::vector<std::uint8_t> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(static_cast<std::uint8_t>(dist(rng)));
    }
    return out;
}

std::string HexEncodeLower(const std::vector<std::uint8_t>& bytes) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (std::uint8_t b : bytes) {
        out.push_back(kHex[(b >> 4U) & 0x0FU]);
        out.push_back(kHex[b & 0x0FU]);
    }
    return out;
}

std::string Base64UrlEncode(const std::vector<std::uint8_t>& bytes) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve(((bytes.size() + 2U) / 3U) * 4U);

    std::size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const std::uint32_t n = (static_cast<std::uint32_t>(bytes[i]) << 16U) |
                                (static_cast<std::uint32_t>(bytes[i + 1]) << 8U) |
                                static_cast<std::uint32_t>(bytes[i + 2]);
        out.push_back(kAlphabet[(n >> 18U) & 0x3FU]);
        out.push_back(kAlphabet[(n >> 12U) & 0x3FU]);
        out.push_back(kAlphabet[(n >> 6U) & 0x3FU]);
        out.push_back(kAlphabet[n & 0x3FU]);
    }

    const std::size_t rem = bytes.size() - i;
    if (rem == 1U) {
        const std::uint32_t n = (static_cast<std::uint32_t>(bytes[i]) << 16U);
        out.push_back(kAlphabet[(n >> 18U) & 0x3FU]);
        out.push_back(kAlphabet[(n >> 12U) & 0x3FU]);
    } else if (rem == 2U) {
        const std::uint32_t n = (static_cast<std::uint32_t>(bytes[i]) << 16U) |
                                (static_cast<std::uint32_t>(bytes[i + 1]) << 8U);
        out.push_back(kAlphabet[(n >> 18U) & 0x3FU]);
        out.push_back(kAlphabet[(n >> 12U) & 0x3FU]);
        out.push_back(kAlphabet[(n >> 6U) & 0x3FU]);
    }

    return out;
}

std::vector<std::uint8_t> Sha256Bytes(const std::string& input) {
    static constexpr std::uint32_t k[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
        0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
        0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
        0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
        0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
        0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
        0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
        0xc67178f2U};

    auto rotr = [](std::uint32_t x, std::uint32_t n) -> std::uint32_t {
        return (x >> n) | (x << (32U - n));
    };

    std::vector<std::uint8_t> msg(input.begin(), input.end());
    const std::uint64_t bitLen = static_cast<std::uint64_t>(msg.size()) * 8ULL;
    msg.push_back(0x80U);
    while ((msg.size() % 64U) != 56U) {
        msg.push_back(0x00U);
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<std::uint8_t>((bitLen >> (i * 8)) & 0xFFULL));
    }

    std::uint32_t h0 = 0x6a09e667U;
    std::uint32_t h1 = 0xbb67ae85U;
    std::uint32_t h2 = 0x3c6ef372U;
    std::uint32_t h3 = 0xa54ff53aU;
    std::uint32_t h4 = 0x510e527fU;
    std::uint32_t h5 = 0x9b05688cU;
    std::uint32_t h6 = 0x1f83d9abU;
    std::uint32_t h7 = 0x5be0cd19U;

    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64U) {
        std::uint32_t w[64] = {};
        for (int i = 0; i < 16; ++i) {
            const std::size_t idx = chunk + static_cast<std::size_t>(i) * 4U;
            w[i] = (static_cast<std::uint32_t>(msg[idx]) << 24U) |
                   (static_cast<std::uint32_t>(msg[idx + 1]) << 16U) |
                   (static_cast<std::uint32_t>(msg[idx + 2]) << 8U) |
                   static_cast<std::uint32_t>(msg[idx + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7U) ^ rotr(w[i - 15], 18U) ^ (w[i - 15] >> 3U);
            const std::uint32_t s1 = rotr(w[i - 2], 17U) ^ rotr(w[i - 2], 19U) ^ (w[i - 2] >> 10U);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::uint32_t a = h0;
        std::uint32_t b = h1;
        std::uint32_t c = h2;
        std::uint32_t d = h3;
        std::uint32_t e = h4;
        std::uint32_t f = h5;
        std::uint32_t g = h6;
        std::uint32_t h = h7;

        for (int i = 0; i < 64; ++i) {
            const std::uint32_t S1 = rotr(e, 6U) ^ rotr(e, 11U) ^ rotr(e, 25U);
            const std::uint32_t ch = (e & f) ^ ((~e) & g);
            const std::uint32_t temp1 = h + S1 + ch + k[i] + w[i];
            const std::uint32_t S0 = rotr(a, 2U) ^ rotr(a, 13U) ^ rotr(a, 22U);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = S0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
        h5 += f;
        h6 += g;
        h7 += h;
    }

    std::vector<std::uint8_t> out(32U, 0U);
    auto writeWord = [&out](std::size_t offset, std::uint32_t value) {
        out[offset + 0] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
        out[offset + 1] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
        out[offset + 2] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
        out[offset + 3] = static_cast<std::uint8_t>(value & 0xFFU);
    };
    writeWord(0U, h0);
    writeWord(4U, h1);
    writeWord(8U, h2);
    writeWord(12U, h3);
    writeWord(16U, h4);
    writeWord(20U, h5);
    writeWord(24U, h6);
    writeWord(28U, h7);
    return out;
}

std::unordered_map<std::string, std::string> ParseQueryParams(const std::string& input) {
    std::unordered_map<std::string, std::string> out;
    std::string query = input;
    const std::size_t q = query.find('?');
    if (q != std::string::npos) {
        query = query.substr(q + 1U);
    }
    const std::size_t hash = query.find('#');
    if (hash != std::string::npos) {
        query = query.substr(0, hash);
    }
    std::size_t start = 0;
    while (start <= query.size()) {
        const std::size_t amp = query.find('&', start);
        const std::string pair = amp == std::string::npos
            ? query.substr(start)
            : query.substr(start, amp - start);
        if (!pair.empty()) {
            const std::size_t eq = pair.find('=');
            const std::string key = UrlDecode(pair.substr(0, eq));
            const std::string value = eq == std::string::npos
                ? std::string()
                : UrlDecode(pair.substr(eq + 1U));
            if (!key.empty()) {
                out[key] = value;
            }
        }
        if (amp == std::string::npos) break;
        start = amp + 1U;
    }
    return out;
}

std::string ExtractCallbackRedirectUri(const std::string& callbackUrl) {
    if (callbackUrl.empty()) return {};
    const std::size_t schemePos = callbackUrl.find("://");
    if (schemePos == std::string::npos) return {};
    const std::size_t hostStart = schemePos + 3U;
    const std::size_t slashPos = callbackUrl.find('/', hostStart);
    if (slashPos == std::string::npos) return callbackUrl;
    const std::size_t queryPos = callbackUrl.find('?', slashPos);
    const std::size_t fragmentPos = callbackUrl.find('#', slashPos);
    std::size_t endPos = std::string::npos;
    if (queryPos != std::string::npos && fragmentPos != std::string::npos) {
        endPos = std::min(queryPos, fragmentPos);
    } else if (queryPos != std::string::npos) {
        endPos = queryPos;
    } else if (fragmentPos != std::string::npos) {
        endPos = fragmentPos;
    }
    return endPos == std::string::npos
        ? callbackUrl
        : callbackUrl.substr(0, endPos);
}

bool IsOpenAIDeviceAuthCallbackUrl(const std::string& callbackUrl) {
    const std::string redirectUri = ExtractCallbackRedirectUri(callbackUrl);
    return redirectUri == "https://auth.openai.com/deviceauth/callback";
}

} // namespace

void OAuthManager::StoreBrowserFlow(
    const std::string& providerId,
    const PendingOAuthCodeFlowState& flow,
    std::uint64_t nowUnixMs) {
    std::lock_guard<std::mutex> lock(m_oauthFlowMutex);
    PruneExpiredLocked(nowUnixMs);
    m_pendingOauthCodeFlowsByProviderId[providerId] = flow;
}

OAuthManager::BrowserFlowLookupStatus OAuthManager::GetActiveBrowserFlow(
    const std::string& providerId,
    std::uint64_t nowUnixMs,
    PendingOAuthCodeFlowState* outFlow) {
    std::lock_guard<std::mutex> lock(m_oauthFlowMutex);
    auto it = m_pendingOauthCodeFlowsByProviderId.find(providerId);
    if (it == m_pendingOauthCodeFlowsByProviderId.end()) {
        return BrowserFlowLookupStatus::NotFound;
    }
    if (it->second.expiresUnixMs > 0 && it->second.expiresUnixMs <= nowUnixMs) {
        m_pendingOauthCodeFlowsByProviderId.erase(it);
        return BrowserFlowLookupStatus::Expired;
    }
    if (outFlow) {
        *outFlow = it->second;
    }
    return BrowserFlowLookupStatus::Found;
}

void OAuthManager::ClearBrowserFlow(const std::string& providerId) {
    std::lock_guard<std::mutex> lock(m_oauthFlowMutex);
    m_pendingOauthCodeFlowsByProviderId.erase(providerId);
}

OAuthManager::ParsedCallback OAuthManager::ParseBrowserCallback(const std::string& callbackUrl) const {
    ParsedCallback parsed;
    if (callbackUrl.empty()) {
        return parsed;
    }
    parsed.redirectUri = ExtractCallbackRedirectUri(callbackUrl);
    parsed.isOpenAIDeviceAuthCallback = IsOpenAIDeviceAuthCallbackUrl(callbackUrl);
    const auto params = ParseQueryParams(callbackUrl);
    auto itCode = params.find("code");
    if (itCode != params.end()) {
        parsed.code = itCode->second;
    }
    auto itState = params.find("state");
    if (itState != params.end()) {
        parsed.state = itState->second;
    }
    return parsed;
}

OAuthManager::OperationResult OAuthManager::ExchangeBrowserAuthorizationCode(
    const std::string& code,
    const std::string& redirectUri,
    const std::string& codeVerifier) const {
    OperationResult out;
    const std::string exchangeBody =
        "grant_type=authorization_code"
        "&code=" + UrlEncode(code) +
        "&redirect_uri=" + UrlEncode(redirectUri) +
        "&client_id=" + UrlEncode(kOpenAICodexClientId) +
        "&code_verifier=" + UrlEncode(codeVerifier);

    const auto exchange = CurlPost(
        kOpenAICodexTokenEndpoint,
        exchangeBody,
        {
            "Content-Type: application/x-www-form-urlencoded",
            "Accept: application/json",
            "originator: animus",
            "User-Agent: animus"
        });

    if (!exchange.error.empty()) {
        out.httpStatusCode = 502;
        out.body["error"] = exchange.error;
        return out;
    }

    Json::Value exchangeJson;
    std::string exchangeParseErr;
    if (!ParseJsonPayload(exchange.body, &exchangeJson, &exchangeParseErr)) {
        out.httpStatusCode = 502;
        out.body["error"] = "failed to parse oauth exchange response";
        out.body["body"] = exchange.body;
        return out;
    }

    if (exchange.status != 200) {
        out.httpStatusCode = 502;
        out.body["error"] = JsonErrorMessage(exchangeJson);
        if (out.body["error"].asString().empty()) {
            out.body["error"] = "oauth token exchange failed";
        }
        out.body["status_code"] = static_cast<Json::Int64>(exchange.status);
        return out;
    }

    const std::string access = exchangeJson.get("access_token", "").asString();
    const std::string refresh = exchangeJson.get("refresh_token", "").asString();
    const Json::Int64 expiresIn = exchangeJson.get("expires_in", 3600).asInt64();
    if (access.empty() || refresh.empty()) {
        out.httpStatusCode = 502;
        out.body["error"] = "oauth token response missing access_token or refresh_token";
        return out;
    }

    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    const Json::Int64 expiresAtMs = nowMs + std::max<Json::Int64>(1, expiresIn) * 1000;

    Json::Value authEntry(Json::objectValue);
    authEntry["type"] = "oauth";
    authEntry["access"] = access;
    authEntry["refresh"] = refresh;
    authEntry["expires"] = expiresAtMs;

    out.httpStatusCode = 200;
    out.body["auth_entry"] = authEntry;
    out.body["expires"] = expiresAtMs;
    return out;
}

OAuthManager::OperationResult OAuthManager::StartOpenAICodexBrowserFlow(
    const std::string& providerId,
    const std::string& redirectUri,
    std::uint64_t nowUnixMs) {
    OperationResult out;
    const std::string codeVerifier = Base64UrlEncode(RandomBytes(48U));
    const std::string codeChallenge = Base64UrlEncode(Sha256Bytes(codeVerifier));
    const std::string flowState = HexEncodeLower(RandomBytes(16U));

    PendingOAuthCodeFlowState flow;
    flow.state = flowState;
    flow.codeVerifier = codeVerifier;
    flow.redirectUri = redirectUri;
    flow.createdUnixMs = nowUnixMs;
    flow.expiresUnixMs = nowUnixMs + kOpenAICodexBrowserFlowTtlMs;
    StoreBrowserFlow(providerId, flow, nowUnixMs);

    std::string authorizationUrl = std::string(kOpenAICodexAuthorizeEndpoint) +
        "?response_type=code" +
        "&client_id=" + UrlEncode(kOpenAICodexClientId) +
        "&redirect_uri=" + UrlEncode(redirectUri) +
        "&scope=" + UrlEncode("openid profile email offline_access") +
        "&id_token_add_organizations=true" +
        "&codex_cli_simplified_flow=true" +
        "&originator=" + UrlEncode("animus") +
        "&state=" + UrlEncode(flowState) +
        "&code_challenge=" + UrlEncode(codeChallenge) +
        "&code_challenge_method=S256";

    out.httpStatusCode = 200;
    out.body["provider_id"] = providerId;
    out.body["status"] = "pending";
    out.body["authorization_url"] = authorizationUrl;
    out.body["redirect_uri"] = redirectUri;
    out.body["state"] = flowState;
    out.body["expires"] = static_cast<Json::UInt64>(flow.expiresUnixMs);
    out.body["expires_in_seconds"] = static_cast<Json::UInt64>(kOpenAICodexBrowserFlowTtlMs / 1000ULL);
    return out;
}

OAuthManager::OperationResult OAuthManager::CompleteOpenAICodexBrowserFlow(
    const std::string& providerId,
    const std::string& code,
    const std::string& state,
    const std::string& callbackUrl,
    std::uint64_t nowUnixMs) {
    OperationResult out;

    std::string effectiveCode = code;
    std::string returnedState = state;
    const auto parsedCallback = ParseBrowserCallback(callbackUrl);
    const bool callbackIsOpenAIDevice = parsedCallback.isOpenAIDeviceAuthCallback;
    if (effectiveCode.empty()) {
        effectiveCode = parsedCallback.code;
    }
    if (returnedState.empty()) {
        returnedState = parsedCallback.state;
    }
    if (effectiveCode.empty()) {
        out.httpStatusCode = 400;
        out.body["error"] = "missing authorization code (paste full callback URL or provide code)";
        return out;
    }

    PendingOAuthCodeFlowState flow;
    const auto flowStatus = GetActiveBrowserFlow(providerId, nowUnixMs, &flow);
    if (flowStatus == BrowserFlowLookupStatus::NotFound) {
        out.httpStatusCode = 400;
        out.body["error"] = "no active browser oauth flow for provider; start flow first";
        return out;
    }
    if (flowStatus == BrowserFlowLookupStatus::Expired) {
        out.httpStatusCode = 400;
        out.body["error"] = "browser oauth flow expired; start flow again";
        return out;
    }

    if (!callbackIsOpenAIDevice) {
        if (returnedState.empty()) {
            out.httpStatusCode = 400;
            out.body["error"] = "missing state in callback data";
            return out;
        }
        if (returnedState != flow.state) {
            out.httpStatusCode = 400;
            out.body["error"] = "callback state mismatch";
            return out;
        }
    }

    std::string effectiveRedirectUri = flow.redirectUri;
    if (!parsedCallback.redirectUri.empty()) {
        effectiveRedirectUri = parsedCallback.redirectUri;
    }

    const auto exchangeResult = ExchangeBrowserAuthorizationCode(
        effectiveCode, effectiveRedirectUri, flow.codeVerifier);
    if (exchangeResult.httpStatusCode != 200) {
        return exchangeResult;
    }

    ClearBrowserFlow(providerId);
    out.httpStatusCode = 200;
    out.body = exchangeResult.body;
    return out;
}

OAuthManager::OperationResult OAuthManager::StartOpenAICodexDeviceFlow(
    const std::string& providerId) const {
    const auto deviceCodeResult = RequestOpenAIDeviceCode();
    if (deviceCodeResult.httpStatusCode != 200) {
        return deviceCodeResult;
    }

    OperationResult out;
    out.httpStatusCode = 200;
    out.body["provider_id"] = providerId;
    out.body["status"] = "pending";
    out.body["device_auth_id"] = deviceCodeResult.body.get("device_auth_id", "");
    out.body["user_code"] = deviceCodeResult.body.get("user_code", "");
    out.body["verification_url"] = "https://auth.openai.com/codex/device";
    out.body["interval_seconds"] = deviceCodeResult.body.get("interval_seconds", 5);
    out.body["expires_at"] = deviceCodeResult.body.get("expires_at", "");
    return out;
}

OAuthManager::OperationResult OAuthManager::PollOpenAICodexDeviceFlow(
    const std::string& providerId,
    const Json::Value& body) const {
    OperationResult out;
    if (!body.isObject()) {
        out.httpStatusCode = 400;
        out.body["error"] = "request body must be a JSON object";
        return out;
    }

    const std::string deviceAuthId = body.get("device_auth_id", "").asString();
    const std::string userCode = body.get("user_code", "").asString();
    if (deviceAuthId.empty() || userCode.empty()) {
        out.httpStatusCode = 400;
        out.body["error"] = "device_auth_id and user_code are required";
        return out;
    }

    const auto pollResult = PollOpenAIDeviceCodeAndExchange(deviceAuthId, userCode);
    if (pollResult.httpStatusCode != 200) {
        return pollResult;
    }

    const std::string stateValue = pollResult.body.get("status", "").asString();
    if (stateValue == "pending") {
        out.httpStatusCode = 200;
        out.body["provider_id"] = providerId;
        out.body["status"] = "pending";
        if (pollResult.body.isMember("message")) {
            out.body["message"] = pollResult.body["message"];
        }
        return out;
    }

    out.httpStatusCode = 200;
    out.body["provider_id"] = providerId;
    out.body["status"] = "authorized";
    out.body["expires"] = pollResult.body.get("expires", 0);
    out.body["auth_entry"] = pollResult.body["auth_entry"];
    return out;
}

OAuthManager::OperationResult OAuthManager::RequestOpenAIDeviceCode() const {
    OperationResult out;
    Json::Value req(Json::objectValue);
    req["client_id"] = kOpenAICodexClientId;

    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "";
    const std::string body = Json::writeString(writerBuilder, req);

    const auto result = CurlPost(
        "https://auth.openai.com/api/accounts/deviceauth/usercode",
        body,
        {
            "Content-Type: application/json",
            "Accept: application/json",
            "originator: animus",
            "User-Agent: animus"
        });

    if (!result.error.empty()) {
        out.httpStatusCode = 502;
        out.body["error"] = result.error;
        return out;
    }

    Json::Value parsed;
    std::string parseErr;
    if (!ParseJsonPayload(result.body, &parsed, &parseErr)) {
        out.httpStatusCode = 502;
        out.body["error"] = "failed to parse auth response";
        out.body["body"] = result.body;
        return out;
    }

    if (result.status != 200) {
        out.httpStatusCode = 502;
        out.body["error"] = JsonErrorMessage(parsed);
        if (out.body["error"].asString().empty()) {
            out.body["error"] = "device code request failed";
        }
        out.body["status_code"] = static_cast<Json::Int64>(result.status);
        return out;
    }

    const std::string deviceAuthId = parsed.get("device_auth_id", "").asString();
    const std::string userCode = parsed.get("user_code", parsed.get("usercode", "")).asString();
    if (deviceAuthId.empty() || userCode.empty()) {
        out.httpStatusCode = 502;
        out.body["error"] = "device auth response missing required fields";
        return out;
    }

    out.httpStatusCode = 200;
    out.body["device_auth_id"] = deviceAuthId;
    out.body["user_code"] = userCode;
    out.body["interval_seconds"] = parsed.get("interval", 5);
    out.body["expires_at"] = parsed.get("expires_at", "");
    return out;
}

OAuthManager::OperationResult OAuthManager::PollOpenAIDeviceCodeAndExchange(
    const std::string& deviceAuthId,
    const std::string& userCode) const {
    OperationResult out;
    Json::Value pollReq(Json::objectValue);
    pollReq["device_auth_id"] = deviceAuthId;
    pollReq["user_code"] = userCode;
    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "";
    const std::string pollBody = Json::writeString(writerBuilder, pollReq);

    const auto pollResult = CurlPost(
        "https://auth.openai.com/api/accounts/deviceauth/token",
        pollBody,
        {
            "Content-Type: application/json",
            "Accept: application/json",
            "originator: animus",
            "User-Agent: animus"
        });

    if (!pollResult.error.empty()) {
        out.httpStatusCode = 502;
        out.body["error"] = pollResult.error;
        return out;
    }

    Json::Value pollJson;
    std::string pollParseErr;
    if (!ParseJsonPayload(pollResult.body, &pollJson, &pollParseErr)) {
        out.httpStatusCode = 502;
        out.body["error"] = "failed to parse auth poll response";
        out.body["body"] = pollResult.body;
        return out;
    }

    if (pollResult.status == 403 || pollResult.status == 404) {
        out.httpStatusCode = 200;
        out.body["status"] = "pending";
        const std::string msg = JsonErrorMessage(pollJson);
        if (!msg.empty()) {
            out.body["message"] = msg;
        }
        return out;
    }

    if (pollResult.status != 200) {
        out.httpStatusCode = 502;
        out.body["error"] = JsonErrorMessage(pollJson);
        if (out.body["error"].asString().empty()) {
            out.body["error"] = "device auth poll failed";
        }
        out.body["status_code"] = static_cast<Json::Int64>(pollResult.status);
        return out;
    }

    const std::string authorizationCode = pollJson.get("authorization_code", "").asString();
    const std::string codeVerifier = pollJson.get("code_verifier", "").asString();
    if (authorizationCode.empty() || codeVerifier.empty()) {
        out.httpStatusCode = 502;
        out.body["error"] = "authorization response missing exchange credentials";
        return out;
    }

    const std::string exchangeBody =
        "grant_type=authorization_code"
        "&code=" + UrlEncode(authorizationCode) +
        "&redirect_uri=" + UrlEncode("https://auth.openai.com/deviceauth/callback") +
        "&client_id=" + UrlEncode(kOpenAICodexClientId) +
        "&code_verifier=" + UrlEncode(codeVerifier);

    const auto exchange = CurlPost(
        kOpenAICodexTokenEndpoint,
        exchangeBody,
        {
            "Content-Type: application/x-www-form-urlencoded",
            "Accept: application/json",
            "originator: animus",
            "User-Agent: animus"
        });

    if (!exchange.error.empty()) {
        out.httpStatusCode = 502;
        out.body["error"] = exchange.error;
        return out;
    }

    Json::Value exchangeJson;
    std::string exchangeParseErr;
    if (!ParseJsonPayload(exchange.body, &exchangeJson, &exchangeParseErr)) {
        out.httpStatusCode = 502;
        out.body["error"] = "failed to parse oauth exchange response";
        out.body["body"] = exchange.body;
        return out;
    }

    if (exchange.status != 200) {
        out.httpStatusCode = 502;
        out.body["error"] = JsonErrorMessage(exchangeJson);
        if (out.body["error"].asString().empty()) {
            out.body["error"] = "oauth token exchange failed";
        }
        out.body["status_code"] = static_cast<Json::Int64>(exchange.status);
        return out;
    }

    const std::string access = exchangeJson.get("access_token", "").asString();
    const std::string refresh = exchangeJson.get("refresh_token", "").asString();
    const Json::Int64 expiresIn = exchangeJson.get("expires_in", 3600).asInt64();
    if (access.empty() || refresh.empty()) {
        out.httpStatusCode = 502;
        out.body["error"] = "oauth token response missing access_token or refresh_token";
        return out;
    }

    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    const Json::Int64 expiresAtMs = nowMs + std::max<Json::Int64>(1, expiresIn) * 1000;

    Json::Value authEntry(Json::objectValue);
    authEntry["type"] = "oauth";
    authEntry["access"] = access;
    authEntry["refresh"] = refresh;
    authEntry["expires"] = expiresAtMs;

    out.httpStatusCode = 200;
    out.body["status"] = "authorized";
    out.body["auth_entry"] = authEntry;
    out.body["expires"] = expiresAtMs;
    return out;
}

void OAuthManager::PruneExpiredLocked(std::uint64_t nowUnixMs) {
    for (auto it = m_pendingOauthCodeFlowsByProviderId.begin();
         it != m_pendingOauthCodeFlowsByProviderId.end();) {
        if (it->second.expiresUnixMs > 0 && it->second.expiresUnixMs <= nowUnixMs) {
            it = m_pendingOauthCodeFlowsByProviderId.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_pendingTwitterFlowsByChannelId.begin();
         it != m_pendingTwitterFlowsByChannelId.end();) {
        if (it->second.expiresUnixMs > 0 && it->second.expiresUnixMs <= nowUnixMs) {
            it = m_pendingTwitterFlowsByChannelId.erase(it);
        } else {
            ++it;
        }
    }
}




// ============================================================================
// Twitter OAuth 2.0 PKCE browser flow
// ============================================================================

OAuthManager::OperationResult OAuthManager::StartTwitterOAuthFlow(
    const std::string& channelId,
    const std::string& clientId,
    const std::string& redirectUri,
    std::uint64_t nowUnixMs) {
    OperationResult out;

    // Generate PKCE code_verifier (32 random bytes, base64url-encoded)
    const auto verifierBytes = RandomBytes(32);
    const std::string codeVerifier = Base64UrlEncode(verifierBytes);

    // Compute code_challenge = BASE64URL(SHA256(code_verifier))
    const auto challengeHash = Sha256Bytes(codeVerifier);
    const std::string codeChallenge = Base64UrlEncode(challengeHash);

    // Generate state parameter (16 random bytes, hex-encoded)
    const auto stateBytes = RandomBytes(16);
    const std::string state = HexEncodeLower(stateBytes);

    // Store pending flow state
    PendingOAuthCodeFlowState flow;
    flow.state = state;
    flow.codeVerifier = codeVerifier;
    flow.redirectUri = redirectUri;
    flow.clientId = clientId;
    flow.clientSecret = "";  // public client, no secret
    flow.createdUnixMs = nowUnixMs;
    flow.expiresUnixMs = nowUnixMs + 15ULL * 60ULL * 1000ULL;  // 15 min TTL
    {
        std::lock_guard<std::mutex> lock(m_oauthFlowMutex);
        PruneExpiredLocked(nowUnixMs);
        m_pendingTwitterFlowsByChannelId[channelId] = flow;
    }

    // Build authorize URL
    const std::string scope = "tweet.read tweet.write users.read follows.read offline.access";
    std::string authorizeUrl =
        std::string("https://twitter.com/i/oauth2/authorize")
        + "?response_type=code"
        + "&client_id=" + UrlEncode(clientId)
        + "&redirect_uri=" + UrlEncode(redirectUri)
        + "&scope=" + UrlEncode(scope)
        + "&state=" + state
        + "&code_challenge=" + UrlEncode(codeChallenge)
        + "&code_challenge_method=S256";

    out.httpStatusCode = 200;
    out.body["authorize_url"] = authorizeUrl;
    out.body["state"] = state;
    return out;
}

OAuthManager::OperationResult OAuthManager::CompleteTwitterOAuthFlow(
    const std::string& channelId,
    const std::string& clientId,
    const std::string& clientSecret,
    const std::string& authorizationCode,
    const std::string& state,
    const std::string& redirectUri) {
    OperationResult out;

    // Look up pending flow
    PendingOAuthCodeFlowState flow;
    {
        std::lock_guard<std::mutex> lock(m_oauthFlowMutex);
        const auto nowMs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
            .count());
        PruneExpiredLocked(nowMs);

        auto it = m_pendingTwitterFlowsByChannelId.find(channelId);
        if (it == m_pendingTwitterFlowsByChannelId.end()) {
            out.httpStatusCode = 400;
            out.body["error"] = "no pending Twitter OAuth flow for channel '" + channelId + "'; start flow first";
            return out;
        }
        flow = it->second;
        m_pendingTwitterFlowsByChannelId.erase(it);
    }

    // Validate state parameter (CSRF protection)
    if (state != flow.state) {
        out.httpStatusCode = 400;
        out.body["error"] = "state mismatch: possible CSRF attack";
        return out;
    }

    // Use the client_id from the stored flow if not explicitly provided
    const std::string effectiveClientId = clientId.empty() ? flow.clientId : clientId;
    const std::string effectiveClientSecret = clientSecret.empty() ? flow.clientSecret : clientSecret;

    // Exchange authorization code for tokens
    // Twitter v2 token endpoint: POST https://api.twitter.com/2/oauth2/token
    // For confidential clients: Authorization: Basic base64(client_id:client_secret)
    // For public clients: client_id in body, no Authorization header
    std::string exchangeBody =
        "grant_type=authorization_code"
        "&code=" + UrlEncode(authorizationCode)
        + "&redirect_uri=" + UrlEncode(redirectUri)
        + "&code_verifier=" + UrlEncode(flow.codeVerifier)
        + "&client_id=" + UrlEncode(effectiveClientId);

    std::vector<std::string> headers = {
        "Content-Type: application/x-www-form-urlencoded",
        "Accept: application/json"
    };

    // For confidential clients (with client_secret), use HTTP Basic auth
    // and remove client_id from the body (per Twitter docs)
    if (!effectiveClientSecret.empty()) {
        // Basic auth: base64(client_id:client_secret)
        const std::string credentials = effectiveClientId + ":" + effectiveClientSecret;
        const auto credBytes = std::vector<std::uint8_t>(credentials.begin(), credentials.end());
        const std::string encoded = Base64UrlEncode(credBytes);
        headers.push_back("Authorization: Basic " + encoded);
    }

    const auto exchange = CurlPost(
        "https://api.twitter.com/2/oauth2/token",
        exchangeBody,
        headers);

    if (!exchange.error.empty()) {
        out.httpStatusCode = 502;
        out.body["error"] = "Twitter token exchange network error: " + exchange.error;
        return out;
    }

    Json::Value exchangeJson;
    std::string exchangeParseErr;
    if (!ParseJsonPayload(exchange.body, &exchangeJson, &exchangeParseErr)) {
        out.httpStatusCode = 502;
        out.body["error"] = "failed to parse Twitter OAuth exchange response";
        out.body["body"] = exchange.body;
        return out;
    }

    if (exchange.status != 200) {
        out.httpStatusCode = 502;
        out.body["error"] = JsonErrorMessage(exchangeJson);
        if (out.body["error"].asString().empty()) {
            out.body["error"] = "Twitter OAuth token exchange failed (HTTP " +
                std::to_string(exchange.status) + ")";
        }
        out.body["status_code"] = static_cast<Json::Int64>(exchange.status);
        return out;
    }

    const std::string accessToken = exchangeJson.get("access_token", "").asString();
    const std::string refreshToken = exchangeJson.get("refresh_token", "").asString();
    const Json::Int64 expiresIn = exchangeJson.get("expires_in", 7200).asInt64();  // Twitter default 2h

    if (accessToken.empty()) {
        out.httpStatusCode = 502;
        out.body["error"] = "Twitter OAuth response missing access_token";
        out.body["body"] = exchange.body;
        return out;
    }

    // Compute expires_at timestamp (milliseconds)
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    const Json::Int64 expiresAtMs = nowMs + std::max<Json::Int64>(1, expiresIn) * 1000;

    Json::Value tokens(Json::objectValue);
    tokens["access_token"] = accessToken;
    tokens["refresh_token"] = refreshToken;
    tokens["token_type"] = exchangeJson.get("token_type", "bearer").asString();
    tokens["scope"] = exchangeJson.get("scope", "").asString();
    tokens["expires_in"] = expiresIn;
    tokens["expires_at_ms"] = expiresAtMs;

    out.httpStatusCode = 200;
    out.body["status"] = "authorized";
    out.body["tokens"] = tokens;
    return out;
}

} // namespace animus::kernel