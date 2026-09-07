#include "animus_kernel/api/ApiConnectionManager.h"

#include "animus_kernel/ApiPackageStore.h"
#include "animus_kernel/Log.h"
#include "animus_kernel/tools/HttpClient.h"

#include <json/json.h>

namespace animus::kernel {

namespace {

// Interval between poll ticks (scans connections; per-connection cadence is
// enforced via lastPollMs vs poll.interval_s).
constexpr int kTickMs = 5000;

std::string JsonWriteCompact(const Json::Value& v) {
    Json::StreamWriterBuilder b;
    b["indentation"] = "";
    b["commentStyle"] = "None";
    return Json::writeString(b, v);
}

Json::Value ParseJsonOr(const std::string& text, const Json::Value& fallback) {
    if (text.empty()) return fallback;
    Json::CharReaderBuilder rb;
    Json::Value out;
    std::istringstream s(text);
    std::string e;
    if (Json::parseFromStream(rb, s, &out, &e)) return out;
    return fallback;
}

// Dot-path resolver: cursor_path ("a.b.c") into a JSON body.
const Json::Value* ResolvePath(const Json::Value& root, const std::string& path) {
    const Json::Value* v = &root;
    std::string rest = path;
    while (!rest.empty()) {
        size_t dot = rest.find('.');
        std::string seg = (dot == std::string::npos) ? rest : rest.substr(0, dot);
        if (seg.empty() || !v->isObject() || !v->isMember(seg)) return nullptr;
        v = &(*v)[seg];
        if (dot == std::string::npos) break;
        rest.erase(0, dot + 1);
    }
    return v;
}

// Top-level state keys referenced by a {{state.<key>...}} template.
std::vector<std::string> StateKeysReferenced(const std::string& tmpl) {
    std::vector<std::string> out;
    size_t pos = 0;
    while ((pos = tmpl.find("{{state.", pos)) != std::string::npos) {
        size_t end = tmpl.find("}}", pos);
        if (end == std::string::npos) break;
        std::string path = tmpl.substr(pos + 8, end - pos - 8);
        while (!path.empty() && path.front() == ' ') path.erase(0, 1);
        size_t dot = path.find('.');
        std::string key = (dot == std::string::npos) ? path : path.substr(0, dot);
        if (!key.empty()) out.push_back(key);
        pos = end + 2;
    }
    return out;
}

}  // namespace

ApiConnectionManager::ApiConnectionManager(ApiPackageStore* store, ApiRuntime* runtime,
                                           HttpClient* http)
    : m_store(store), m_runtime(runtime), m_http(http) {}

ApiConnectionManager::~ApiConnectionManager() { Stop(); }

void ApiConnectionManager::SetDispatchCallback(DispatchCallback cb) { m_dispatchCb = std::move(cb); }

void ApiConnectionManager::Start() {
    if (m_running.exchange(true)) return;
    m_stop = false;
    m_thread = std::thread([this] { Run(); });
    ALOG_INFO("api-conn", "connection manager started");
}

void ApiConnectionManager::Stop() {
    if (!m_running.exchange(false)) return;
    m_stop = true;
    if (m_thread.joinable()) m_thread.join();
    ALOG_INFO("api-conn", "connection manager stopped");
}

int ApiConnectionManager::PollOnce() {
    // One synchronous pass (admin/debug surface). Claims each connection
    // under the mutex (in-flight guard), polls with the mutex free.
    int n = 0;
    for (const auto& pkg : m_store->ListPackages()) {
        if (!pkg.enabled) continue;
        for (const auto& conn : m_store->ListConnections(pkg.id)) {
            if (!conn.enabled) continue;
            std::string key = pkg.id + ":" + conn.name;
            std::string lastCursor;
            int prevErrors = 0;
            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                auto& cs = m_connStates[key];
                if (cs.polling) continue;  // poll thread owns it right now
                cs.polling = true;
                cs.lastPollMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
                lastCursor = cs.lastCursor;
                prevErrors = cs.consecutiveErrors;
            }
            PollOutcome o = PollConnection(pkg, conn, lastCursor, prevErrors);
            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                auto& cs = m_connStates[key];
                cs.polling = false;
                cs.consecutiveErrors = o.consecutiveErrors;
                cs.lastCursor = o.newCursor;
            }
            ++n;
        }
    }
    return n;
}

void ApiConnectionManager::Run() {
    while (!m_stop.load()) {
        Tick();
        for (int i = 0; i < kTickMs / 100 && !m_stop.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void ApiConnectionManager::Tick() {
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();

    struct DuePoll {
        ApiPackage pkg;
        ApiPackageConnection conn;
        std::string lastCursor;
        int prevErrors;
    };
    std::vector<DuePoll> due;

    for (const auto& pkg : m_store->ListPackages()) {
        if (!pkg.enabled) continue;
        for (const auto& conn : m_store->ListConnections(pkg.id)) {
            if (!conn.enabled) continue;

            Json::Value poll = ParseJsonOr(conn.poll, Json::Value(Json::objectValue));
            int intervalS = poll.get("interval_s", 60).asInt();
            if (intervalS <= 0) intervalS = 60;

            // Claim under the mutex; poll with it FREE.
            std::string key = pkg.id + ":" + conn.name;
            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                auto& cs = m_connStates[key];
                if (now - cs.lastPollMs < static_cast<int64_t>(intervalS) * 1000) continue;
                if (cs.polling) continue;  // manual PollOnce owns it
                cs.lastPollMs = now;
                cs.polling = true;
                due.push_back({pkg, conn, cs.lastCursor, cs.consecutiveErrors});
            }
        }
    }

    for (const auto& d : due) {
        PollOutcome o = PollConnection(d.pkg, d.conn, d.lastCursor, d.prevErrors);
        std::string key = d.pkg.id + ":" + d.conn.name;
        std::lock_guard<std::mutex> lock(m_stateMutex);
        auto& cs = m_connStates[key];
        cs.polling = false;
        cs.consecutiveErrors = o.consecutiveErrors;
        cs.lastCursor = o.newCursor;
    }
}

Json::Value ApiConnectionManager::BuildPollContext(const ApiPackage& pkg,
                                                   const ApiPackageConnection& conn,
                                                   const std::string& lastCursor) {
    (void)conn;
    Json::Value state = ParseJsonOr(pkg.state, Json::Value(Json::objectValue));
    Json::Value schema = ParseJsonOr(pkg.state_schema, Json::Value(Json::objectValue));
    if (schema.isObject()) {
        for (const std::string& k : schema.getMemberNames()) {
            if (schema[k].isMember("default") && !state.isMember(k))
                state[k] = schema[k]["default"];
        }
    }
    // Request-side cursor: {{state._cursor.value}} — first tick empty.
    Json::Value cursor(Json::objectValue);
    cursor["value"] = lastCursor;
    state["_cursor"] = cursor;
    return state;
}

ApiConnectionManager::PollOutcome ApiConnectionManager::PollConnection(
    const ApiPackage& pkg, const ApiPackageConnection& conn,
    const std::string& lastCursor, int prevErrors) {
    // longpoll only in this build
    if (conn.type != "longpoll") {
        ALOG_INFO("api-conn", "[" << pkg.name << ":" << conn.name << "] type '" << conn.type
                                  << "' not driven yet (longpoll only)");
        return PollOutcome{};  // no-op outcome: state unchanged
    }

    PollOutcome o;
    o.consecutiveErrors = prevErrors;  // carry through unless changed below
    o.newCursor = lastCursor;

    Json::Value poll = ParseJsonOr(conn.poll, Json::Value(Json::objectValue));
    Json::Value stateCtx = BuildPollContext(pkg, conn, lastCursor);
    Json::Value schema = ParseJsonOr(pkg.state_schema, Json::Value(Json::objectValue));

    // URL interpolation (secrets flow into the request; masking at logs).
    std::string ierr;
    Json::Value emptyArgs(Json::objectValue);
    std::string url = ApiRuntime::Interpolate(conn.url_template, stateCtx, emptyArgs, {}, ierr);
    if (!ierr.empty()) {
        ALOG_WARNING("api-conn", "[" << pkg.name << ":" << conn.name << "] url interpolation: "
                                     << ierr);
        return o;
    }

    HttpClient::Request req;
    req.method = "GET";
    req.url = url;
    req.timeout_seconds = 30;

    // Collect secret values referenced by templates for log masking.
    std::vector<std::string> secretValues;
    auto collectSecret = [&](const std::string& tmpl) {
        for (const std::string& k : StateKeysReferenced(tmpl)) {
            if (schema[k].get("secret", Json::Value(false)).asBool() &&
                stateCtx.isMember(k) && stateCtx[k].isString())
                secretValues.push_back(stateCtx[k].asString());
        }
    };
    collectSecret(conn.url_template);

    // Headers
    Json::Value headers = ParseJsonOr(conn.headers_template, Json::Value(Json::objectValue));
    if (headers.isObject()) {
        for (const std::string& h : headers.getMemberNames()) {
            std::string hv = ApiRuntime::Interpolate(headers[h].asString(), stateCtx, emptyArgs,
                                                     {}, ierr);
            if (!ierr.empty()) {
                ALOG_WARNING("api-conn", "[" << pkg.name << ":" << conn.name << "] header '"
                                             << h << "' interpolation: " << ierr);
                return o;
            }
            req.headers[h] = hv;
            collectSecret(headers[h].asString());
        }
    }

    // Query params from poll.params_template (interpolated, appended)
    Json::Value params = poll.get("params_template", Json::Value(Json::objectValue));
    if (params.isObject()) {
        std::string qs;
        for (const std::string& p : params.getMemberNames()) {
            std::string rawVal = params[p].isString() ? params[p].asString()
                                                     : JsonWriteCompact(params[p]);
            std::string pv = ApiRuntime::Interpolate(rawVal, stateCtx, emptyArgs, {}, ierr);
            if (!ierr.empty()) pv = rawVal;  // literal, not a template
            if (!qs.empty()) qs += "&";
            qs += p + "=" + pv;
        }
        if (!qs.empty())
            req.url += (req.url.find('?') == std::string::npos ? "?" : "&") + qs;
    }

    ALOG_INFO("api-conn", "[" << pkg.name << ":" << conn.name << "] poll GET "
                              << ApiRuntime::MaskSecrets(req.url, secretValues));

    HttpClient::Response resp = m_http->Execute(req);
    if (resp.status_code != 200) {
        o.consecutiveErrors = prevErrors + 1;
        ALOG_WARNING("api-conn", "[" << pkg.name << ":" << conn.name << "] poll status "
                                      << resp.status_code << " (consecutive: "
                                      << o.consecutiveErrors << ")");
        return o;
    }
    o.consecutiveErrors = 0;

    Json::Value body = ParseJsonOr(resp.body, Json::Value(Json::nullValue));
    if (body.isNull()) {
        ALOG_WARNING("api-conn", "[" << pkg.name << ":" << conn.name
                                     << "] poll body is not JSON ("
                                     << resp.body.size() << " B)");
        return o;
    }

    // Cursor extraction (dot path into the response body)
    const std::string cursorPath = poll.get("cursor_path", "").asString();
    if (!cursorPath.empty()) {
        const Json::Value* v = ResolvePath(body, cursorPath);
        if (v && !v->isNull()) {
            std::string newCursor = v->isString() ? v->asString() : JsonWriteCompact(*v);
            if (newCursor != lastCursor) {
                o.newCursor = newCursor;
                o.cursorChanged = true;
                ALOG_INFO("api-conn", "[" << pkg.name << ":" << conn.name << "] cursor -> "
                                          << newCursor.substr(0, 40));
            }
        }
    }

    // on_message hook
    Json::Value hooks = ParseJsonOr(conn.hooks, Json::Value(Json::objectValue));
    std::string onMessage = hooks.get("on_message", "").asString();
    if (onMessage.empty()) return o;

    ALOG_INFO("api-conn", "[" << pkg.name << ":" << conn.name << "] on_message -> '"
                              << onMessage << "'");
    Json::Value result = m_runtime->RunHook(pkg.name, onMessage, "", body);
    HandleHookResult(pkg, conn, result);
    return o;
}

void ApiConnectionManager::HandleHookResult(const ApiPackage& pkg,
                                            const ApiPackageConnection& conn,
                                            const Json::Value& result) {
    if (!result.get("success", Json::Value(false)).asBool()) {
        std::string err = result.get("error", "").asString();
        ALOG_WARNING("api-conn", "[" << pkg.name << ":" << conn.name
                                     << "] hook failed: " << (err.empty() ? "?" : err));
        return;
    }
    const Json::Value& dispatches = result["dispatches"];
    if (!dispatches.isArray()) return;
    for (const auto& d : dispatches) {
        if (!d.isObject() || !d.isMember("prompt")) continue;
        Dispatch out;
        out.packageId = pkg.id;
        out.packageName = pkg.name;
        out.connectionName = conn.name;
        out.reason = d.get("reason", "").asString();
        out.prompt = d["prompt"].asString();
        out.payloadJson = JsonWriteCompact(d);
        ALOG_INFO("api-conn", "[" << pkg.name << ":" << conn.name << "] dispatch reason="
                                  << out.reason);
        if (m_dispatchCb) m_dispatchCb(out);
    }
}

}  // namespace animus::kernel
