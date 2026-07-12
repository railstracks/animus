#include "animus_kernel/llm/OpenAICodexProvider.h"
#include "animus_kernel/llm/OpenAICompat.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include <curl/curl.h>
#include <json/reader.h>
#include <json/value.h>
#include <json/writer.h>

using namespace animus::kernel::llm::openai_compat;

namespace animus::kernel::llm {

namespace {

size_t WriteToString(void* ptr, size_t size, size_t nmemb, void* data) {
  size_t total = size * nmemb;
  static_cast<std::string*>(data)->append(static_cast<char*>(ptr), total);
  return total;
}

int64_t ExtractJsonInt64(const std::string& json, const std::string& key) {
  auto num = ExtractJsonNumber(json, key);
  return num.empty() ? 0 : std::stoll(num);
}

bool ParseJson(const std::string& text, Json::Value* out, std::string* error) {
  if (!out) return false;
  Json::CharReaderBuilder builder;
  std::istringstream stream(text);
  std::string parseErrors;
  if (!Json::parseFromStream(builder, stream, out, &parseErrors)) {
    if (error) *error = parseErrors;
    return false;
  }
  return true;
}

std::string JsonToCompactString(const Json::Value& value) {
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "";
  return Json::writeString(writer, value);
}

std::string TrimTrailingSlash(std::string value) {
  while (!value.empty() && value.back() == '/') {
    value.pop_back();
  }
  return value;
}

std::string CanonicalizeBaseUrl(const std::string& configuredBaseUrl) {
  constexpr const char* kCodexBase = "https://chatgpt.com/backend-api/codex";
  if (configuredBaseUrl.empty()) {
    return kCodexBase;
  }
  std::string base = TrimTrailingSlash(configuredBaseUrl);
  if (base == "https://chatgpt.com/backend-api" ||
      base == "https://chatgpt.com/backend-api/v1" ||
      base == "https://chatgpt.com/backend-api/codex/v1") {
    return kCodexBase;
  }
  return base;
}

std::string MergeInstructions(const std::string& existing, const std::string& next) {
  if (next.empty()) return existing;
  if (existing.empty()) return next;
  return existing + "\n\n" + next;
}

Json::Value BuildTextMessageItem(const std::string& role, const std::string& text) {
  Json::Value item(Json::objectValue);
  item["type"] = "message";
  if (role == "assistant" || role == "user" || role == "developer") {
    item["role"] = role;
  } else {
    item["role"] = "user";
  }

  Json::Value content(Json::arrayValue);
  Json::Value textPart(Json::objectValue);
  textPart["type"] = "input_text";
  textPart["text"] = text;
  content.append(textPart);
  item["content"] = content;
  return item;
}

Json::Value BuildFunctionToolDef(const LLMToolDef& tool) {
  Json::Value out(Json::objectValue);
  out["type"] = "function";
  out["name"] = tool.name;
  if (!tool.description.empty()) {
    out["description"] = tool.description;
  }

  Json::Value parameters(Json::objectValue);
  if (!tool.parameters.empty()) {
    Json::Value parsed;
    if (ParseJson(tool.parameters, &parsed, nullptr) && parsed.isObject()) {
      parameters = parsed;
    }
  }
  out["parameters"] = parameters;
  return out;
}

std::string ResolveErrorMessage(const Json::Value& root) {
  if (root.isMember("error") && root["error"].isObject()) {
    const Json::Value& err = root["error"];
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

LLMToolCall BuildToolCallFromOutputItem(const Json::Value& item) {
  LLMToolCall call;
  call.id = item.get("call_id", item.get("id", "")).asString();
  call.name = item.get("name", "").asString();

  if (item.isMember("arguments")) {
    if (item["arguments"].isString()) {
      call.arguments = item["arguments"].asString();
    } else {
      call.arguments = JsonToCompactString(item["arguments"]);
    }
  }

  return call;
}

} // anonymous namespace

OpenAICodexProvider::OpenAICodexProvider(const LLMProviderConfig& config)
    : LLMProviderBase(config),
      m_authFilePath(Config().extra.count("auth_file")
                         ? Config().extra.at("auth_file")
                         : "config/auth.json") {
  std::string err;
  LoadTokens(&err);
}

bool OpenAICodexProvider::LoadTokens(std::string* error) {
  std::ifstream file(m_authFilePath);
  if (!file.is_open()) return false;

  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());

  auto sectionPos = content.find("\"openai-codex\"");
  if (sectionPos == std::string::npos) {
    m_accessToken = ExtractJsonString(content, "access");
    m_refreshToken = ExtractJsonString(content, "refresh");
    m_expiresMs = ExtractJsonInt64(content, "expires");
  } else {
    std::string section = content.substr(sectionPos);
    m_accessToken = ExtractJsonString(section, "access");
    m_refreshToken = ExtractJsonString(section, "refresh");
    m_expiresMs = ExtractJsonInt64(section, "expires");
  }

  if (m_expiresMs <= 0 && !m_accessToken.empty()) {
    if (error) *error = "Loaded token but missing expires timestamp";
  }

  return !m_accessToken.empty();
}

void OpenAICodexProvider::PersistTokens() const {
  std::ifstream inFile(m_authFilePath);
  std::string content;
  if (inFile.is_open()) {
    content.assign((std::istreambuf_iterator<char>(inFile)),
                   std::istreambuf_iterator<char>());
  }

  std::ostringstream section;
  section << "\"openai-codex\":{"
          << "\"type\":\"oauth\"," 
          << "\"access\":\"" << EscapeJson(m_accessToken) << "\"," 
          << "\"refresh\":\"" << EscapeJson(m_refreshToken) << "\"," 
          << "\"expires\":" << m_expiresMs
          << "}";

  auto pos = content.find("\"openai-codex\"");
  if (pos != std::string::npos) {
    auto bracePos = content.find('{', pos);
    if (bracePos != std::string::npos) {
      int depth = 0;
      size_t i = bracePos;
      for (; i < content.size(); ++i) {
        if (content[i] == '{') depth++;
        if (content[i] == '}') {
          depth--;
          if (depth == 0) break;
        }
      }
      content.replace(pos, i - pos + 1, section.str());
    }
  } else {
    if (content.empty()) {
      content = "{" + section.str() + "}";
    } else {
      auto lastBrace = content.rfind('}');
      if (lastBrace != std::string::npos) {
        content.insert(lastBrace, "," + section.str());
      }
    }
  }

  std::ofstream outFile(m_authFilePath);
  if (outFile.is_open()) outFile << content;
}

bool OpenAICodexProvider::EnsureTokenValid(std::string* error) {
  auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count();
  if (!m_accessToken.empty() && m_expiresMs > now + REFRESH_THRESHOLD_MS) {
    return true;
  }
  return RefreshToken(error);
}

bool OpenAICodexProvider::RefreshToken(std::string* error) {
  if (m_refreshToken.empty()) {
    if (error) *error = "No refresh token - OAuth login required";
    return false;
  }

  CURL* curl = curl_easy_init();
  if (!curl) {
    if (error) *error = "curl init failed for token refresh";
    return false;
  }

  char* encodedRefresh = curl_easy_escape(curl, m_refreshToken.c_str(),
                                          static_cast<int>(m_refreshToken.size()));
  if (!encodedRefresh) {
    curl_easy_cleanup(curl);
    if (error) *error = "failed to URL-encode refresh token";
    return false;
  }

  std::string postFields =
      "grant_type=refresh_token&refresh_token=" + std::string(encodedRefresh) +
      "&client_id=" + CLIENT_ID;
  curl_free(encodedRefresh);

  std::string responseBody;
  char errbuf[CURL_ERROR_SIZE] = {};

  curl_easy_setopt(curl, CURLOPT_URL, TOKEN_ENDPOINT);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postFields.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToString);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L);

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers,
                              "Content-Type: application/x-www-form-urlencoded");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  CURLcode res = curl_easy_perform(curl);
  curl_slist_free_all(headers);

  if (res != CURLE_OK) {
    curl_easy_cleanup(curl);
    if (error) *error = errbuf[0] ? errbuf : curl_easy_strerror(res);
    return false;
  }

  long httpCode = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
  curl_easy_cleanup(curl);

  if (httpCode != 200) {
    Json::Value errRoot;
    std::string detail;
    if (ParseJson(responseBody, &errRoot, nullptr)) {
      detail = ResolveErrorMessage(errRoot);
    }
    if (error) {
      *error = "Refresh HTTP " + std::to_string(httpCode) +
               (detail.empty() ? "" : ": " + detail);
    }
    return false;
  }

  std::string newAccess = ExtractJsonString(responseBody, "access_token");
  std::string newRefresh = ExtractJsonString(responseBody, "refresh_token");
  int64_t expiresIn = ExtractJsonInt64(responseBody, "expires_in");

  if (newAccess.empty()) {
    if (error) *error = "No access_token in refresh response";
    return false;
  }

  m_accessToken = newAccess;
  if (!newRefresh.empty()) m_refreshToken = newRefresh;

  auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count();
  if (expiresIn <= 0) {
    expiresIn = 3600;
  }
  m_expiresMs = now + expiresIn * 1000;

  PersistTokens();
  return true;
}

void OpenAICodexProvider::ResetStreamingState() const {
  m_streamToolCallsByItemId.clear();
  m_streamItemIdByCallId.clear();
  m_streamSawToolCall = false;
}

std::string OpenAICodexProvider::BuildRequestBody(const LLMRequest& request) const {
  Json::Value root(Json::objectValue);
  root["model"] = request.model;
  root["stream"] = request.stream;
  root["store"] = false;

  std::string instructions;
  Json::Value input(Json::arrayValue);

  for (const auto& msg : request.messages) {
    if (msg.role == "system") {
      instructions = MergeInstructions(instructions, msg.content);
      continue;
    }

    if (msg.role == "assistant" && !msg.tool_calls.empty()) {
      for (const auto& tc : msg.tool_calls) {
        Json::Value call(Json::objectValue);
        call["type"] = "function_call";
        if (!tc.id.empty()) {
          call["call_id"] = tc.id;
          call["id"] = tc.id;
        }
        call["name"] = tc.name;
        call["arguments"] = tc.arguments;
        input.append(call);
      }
      if (!msg.content.empty()) {
        input.append(BuildTextMessageItem("assistant", msg.content));
      }
      continue;
    }

    if (msg.role == "tool") {
      if (!msg.tool_call_id.empty()) {
        Json::Value toolOut(Json::objectValue);
        toolOut["type"] = "function_call_output";
        toolOut["call_id"] = msg.tool_call_id;
        toolOut["output"] = msg.content;
        input.append(toolOut);
      } else {
        input.append(BuildTextMessageItem("user", msg.content));
      }
      continue;
    }

    input.append(BuildTextMessageItem(msg.role, msg.content));
  }

  if (!instructions.empty()) {
    root["instructions"] = instructions;
  }

  // Codex Responses requires non-empty input when only instructions are present.
  if (input.empty()) {
    input.append(BuildTextMessageItem("user", " "));
  }
  root["input"] = input;

  if (!request.tools.empty()) {
    Json::Value tools(Json::arrayValue);
    for (const auto& tool : request.tools) {
      tools.append(BuildFunctionToolDef(tool));
    }
    root["tools"] = tools;

    if (!request.tool_choice.empty()) {
      if (request.tool_choice == "auto" || request.tool_choice == "none" ||
          request.tool_choice == "required") {
        root["tool_choice"] = request.tool_choice;
      } else {
        Json::Value choice(Json::objectValue);
        choice["type"] = "function";
        choice["name"] = request.tool_choice;
        root["tool_choice"] = choice;
      }
    }
  }

  if (IsReasoningEnabled(request.reasoning_effort)) {
    Json::Value reasoning(Json::objectValue);
    reasoning["effort"] = request.reasoning_effort;
    root["reasoning"] = reasoning;
  }

  // The native Codex backend may reject temperature/max_output_tokens.
  // Keep them opt-in for compatibility until proven safe in this runtime.
  auto includeMax = request.extra.find("codex_include_max_output_tokens");
  if (request.max_tokens > 0 && includeMax != request.extra.end() &&
      includeMax->second == "true") {
    root["max_output_tokens"] = request.max_tokens;
  }
  auto includeTemp = request.extra.find("codex_include_temperature");
  if (request.temperature > 0.0f && includeTemp != request.extra.end() &&
      includeTemp->second == "true") {
    root["temperature"] = request.temperature;
  }

  return JsonToCompactString(root);
}

LLMMessage OpenAICodexProvider::ParseResponse(const std::string& body,
                                              std::string* error) const {
  Json::Value root;
  std::string parseError;
  if (!ParseJson(body, &root, &parseError)) {
    if (error) *error = "Failed to parse Codex response JSON: " + parseError;
    return {};
  }

  std::string serviceError = ResolveErrorMessage(root);
  if (!serviceError.empty()) {
    if (error) *error = serviceError;
    return {};
  }

  LLMMessage msg;
  msg.role = "assistant";

  const Json::Value output = root["output"];
  if (output.isArray()) {
    for (Json::ArrayIndex i = 0; i < output.size(); ++i) {
      const Json::Value& item = output[i];
      const std::string type = item.get("type", "").asString();

      if (type == "message") {
        const Json::Value content = item["content"];
        if (!content.isArray()) continue;
        for (Json::ArrayIndex j = 0; j < content.size(); ++j) {
          const Json::Value& part = content[j];
          const std::string partType = part.get("type", "").asString();
          if (partType == "output_text") {
            msg.content += part.get("text", "").asString();
          } else if (partType == "refusal") {
            msg.content += part.get("refusal", "").asString();
          }
        }
      } else if (type == "reasoning") {
        const Json::Value summary = item["summary"];
        if (summary.isArray()) {
          for (Json::ArrayIndex j = 0; j < summary.size(); ++j) {
            const Json::Value& part = summary[j];
            if (!msg.thinking_content.empty()) msg.thinking_content += "\n\n";
            msg.thinking_content += part.get("text", "").asString();
          }
        }
      } else if (type == "function_call") {
        LLMToolCall call = BuildToolCallFromOutputItem(item);
        if (!call.id.empty() && !call.name.empty()) {
          msg.tool_calls.push_back(std::move(call));
        }
      }
    }
  }

  return msg;
}

void OpenAICodexProvider::ParseToolCallsFromResponse(
    const std::string& body,
    std::vector<LLMToolCall>& tool_calls,
    std::string& finish_reason) const {
  Json::Value root;
  if (!ParseJson(body, &root, nullptr)) {
    finish_reason = "stop";
    return;
  }

  tool_calls.clear();
  const Json::Value output = root["output"];
  if (output.isArray()) {
    for (Json::ArrayIndex i = 0; i < output.size(); ++i) {
      const Json::Value& item = output[i];
      if (item.get("type", "").asString() == "function_call") {
        LLMToolCall call = BuildToolCallFromOutputItem(item);
        if (!call.id.empty() && !call.name.empty()) {
          tool_calls.push_back(std::move(call));
        }
      }
    }
  }

  if (!tool_calls.empty()) {
    finish_reason = "tool_calls";
  } else {
    finish_reason = "stop";
  }
}

std::optional<LLMToken> OpenAICodexProvider::ParseSSELine(
    const std::string& line) const {
  Json::Value event;
  if (!ParseJson(line, &event, nullptr)) {
    return std::nullopt;
  }

  const std::string type = event.get("type", "").asString();

  if (type == "response.created") {
    ResetStreamingState();
    return std::nullopt;
  }

  if (type == "response.output_item.added") {
    const Json::Value item = event["item"];
    if (item.isObject() && item.get("type", "").asString() == "function_call") {
      const std::string itemId = item.get("id", event.get("item_id", "")).asString();
      if (!itemId.empty()) {
        StreamingToolCall state;
        state.call_id = item.get("call_id", item.get("id", "")).asString();
        state.name = item.get("name", "").asString();
        state.arguments = item.get("arguments", "").asString();
        m_streamToolCallsByItemId[itemId] = state;
        if (!state.call_id.empty()) {
          m_streamItemIdByCallId[state.call_id] = itemId;
        }
      }
    }
    return std::nullopt;
  }

  if (type == "response.function_call_arguments.delta") {
    std::string itemId = event.get("item_id", "").asString();
    if (itemId.empty()) {
      const std::string callId = event.get("call_id", "").asString();
      auto mapped = m_streamItemIdByCallId.find(callId);
      if (mapped != m_streamItemIdByCallId.end()) {
        itemId = mapped->second;
      }
    }

    auto it = m_streamToolCallsByItemId.find(itemId);
    if (it != m_streamToolCallsByItemId.end()) {
      const std::string delta = event.get("delta", "").asString();
      it->second.arguments += delta;
    }
    return std::nullopt;
  }

  if (type == "response.output_item.done") {
    const Json::Value item = event["item"];
    if (!item.isObject()) return std::nullopt;

    if (item.get("type", "").asString() == "function_call") {
      const std::string itemId = item.get("id", event.get("item_id", "")).asString();
      StreamingToolCall state;

      auto it = m_streamToolCallsByItemId.find(itemId);
      if (it != m_streamToolCallsByItemId.end()) {
        state = it->second;
      }

      if (state.call_id.empty()) {
        state.call_id = item.get("call_id", item.get("id", "")).asString();
      }
      if (state.name.empty()) {
        state.name = item.get("name", "").asString();
      }

      if (state.arguments.empty()) {
        if (item.isMember("arguments")) {
          if (item["arguments"].isString()) {
            state.arguments = item["arguments"].asString();
          } else {
            state.arguments = JsonToCompactString(item["arguments"]);
          }
        }
      }

      if (!state.call_id.empty() && !state.name.empty()) {
        LLMToken token;
        token.finish_reason = "tool_calls";
        token.is_final = false;
        token.tool_calls.push_back({state.call_id, state.name, state.arguments});
        m_streamSawToolCall = true;
        m_streamToolCallsByItemId.erase(itemId);
        if (!state.call_id.empty()) {
          m_streamItemIdByCallId.erase(state.call_id);
        }
        return token;
      }
    }
    return std::nullopt;
  }

  if (type == "response.output_text.delta" || type == "response.refusal.delta") {
    LLMToken token;
    token.content = event.get("delta", "").asString();
    if (token.content.empty()) {
      return std::nullopt;
    }
    return token;
  }

  if (type == "response.completed") {
    LLMToken token;
    token.is_final = true;
    token.finish_reason = m_streamSawToolCall ? "tool_calls" : "stop";

    const Json::Value response = event["response"];
    const Json::Value usage = response["usage"];
    if (usage.isObject()) {
      const int inputTokens = usage.get("input_tokens", 0).asInt();
      const int outputTokens = usage.get("output_tokens", 0).asInt();
      const int cachedTokens = usage["input_tokens_details"].get("cached_tokens", 0).asInt();
      token.prompt_tokens = std::max(0, inputTokens - cachedTokens);
      token.completion_tokens = outputTokens;
    }

    return token;
  }

  if (type == "error") {
    LLMToken token;
    token.is_final = true;
    token.finish_reason = "stop";
    return token;
  }

  return std::nullopt;
}

bool OpenAICodexProvider::ProcessSSELine(const std::string& line,
                                         std::string& accumulated,
                                         LLMTokenCallback& callback) {
  if (line.empty() || line == "\r") return false;

  std::string data = line;
  if (data.size() > 6 && data.substr(0, 6) == "data: ") {
    data = data.substr(6);
  } else if (data.size() > 5 && data.substr(0, 5) == "data:") {
    data = data.substr(5);
  }

  while (!data.empty() && (data.back() == '\r' || data.back() == ' ')) {
    data.pop_back();
  }

  if (data.empty()) return false;
  if (data == "[DONE]") return true;

  auto token = ParseSSELine(data);
  if (token.has_value()) {
    if (!token->is_thinking) {
      accumulated += token->content;
    }
    if (callback) {
      callback(token.value());
    }
    if (token->is_final) {
      return true;
    }
  }

  return false;
}

std::string OpenAICodexProvider::GetEndpointURL() const {
  const std::string base = CanonicalizeBaseUrl(Config().base_url);
  if (base.size() >= 10 && base.substr(base.size() - 10) == "/responses") {
    return base;
  }
  return base + "/responses";
}

std::vector<std::pair<std::string, std::string>>
OpenAICodexProvider::GetHeaders() const {
  auto headers = LLMProviderBase::GetHeaders();
  headers.erase(
      std::remove_if(headers.begin(), headers.end(),
                     [](const auto& h) { return h.first == "Authorization"; }),
      headers.end());
  headers.emplace_back("Authorization", "Bearer " + m_accessToken);
  return headers;
}

LLMMessage OpenAICodexProvider::Complete(const LLMRequest& request,
                                         std::string* error) {
  if (!EnsureTokenValid(error)) return {};
  // Codex Responses currently requires stream=true. Some kernel call paths
  // still invoke non-streaming Complete(), so route them through the streaming
  // transport and aggregate text server-side.
  return StreamComplete(request, nullptr, error);
}

LLMMessage OpenAICodexProvider::StreamComplete(const LLMRequest& request,
                                               LLMTokenCallback callback,
                                               std::string* error) {
  if (!EnsureTokenValid(error)) return {};
  ResetStreamingState();
  return LLMProviderBase::StreamComplete(request, std::move(callback), error);
}

bool OpenAICodexProvider::FetchCapabilities(const std::string& modelId) {
  m_capabilities.model_id = modelId;
  m_capabilities.context_length = 0;
  m_capabilities.supports_tools = true;
  m_capabilities.supports_tool_choice = true;
  m_capabilities.supports_reasoning = true;
  m_capabilities.supports_streaming = true;
  m_capabilities.supports_json_mode = false;
  m_capabilities.supports_vision = false;
  m_capabilities.raw_features.clear();

  std::string tokenErr;
  if (!EnsureTokenValid(&tokenErr)) {
    if (!modelId.empty()) {
      m_capabilities.raw_features.push_back(modelId);
    }
    return false;
  }

  const std::string base = CanonicalizeBaseUrl(Config().base_url);
  // The Codex models endpoint currently requires a client_version query param.
  // Keep this stable/defaulted to avoid 400 errors during provider model discovery.
  const std::string modelsUrl = base + "/models?client_version=1.0.0";
  auto headers = GetHeaders();
  headers.emplace_back("Accept", "application/json");

  std::string body;
  std::string getErr;
  const int status = DoHTTPGet(modelsUrl, headers, &body, &getErr);
  if (status != 200) {
    if (!modelId.empty()) {
      m_capabilities.raw_features.push_back(modelId);
    }
    return false;
  }

  Json::Value root;
  std::string parseErr;
  if (!ParseJson(body, &root, &parseErr)) {
    if (!modelId.empty()) {
      m_capabilities.raw_features.push_back(modelId);
    }
    return false;
  }

  std::vector<std::string> discovered;
  std::unordered_set<std::string> seen;
  auto pushModel = [&](const std::string& name) {
    if (name.empty()) return;
    if (seen.insert(name).second) {
      discovered.push_back(name);
    }
  };

  auto pullModelName = [&](const Json::Value& item) -> std::string {
    if (!item.isObject()) return {};
    static const char* kKeys[] = {"id", "model", "slug", "name", "default_model"};
    for (const char* key : kKeys) {
      if (item.isMember(key) && item[key].isString()) {
        const std::string value = item[key].asString();
        if (!value.empty()) return value;
      }
    }
    return {};
  };

  auto parseModelArray = [&](const Json::Value& arr) {
    if (!arr.isArray()) return;
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i) {
      if (arr[i].isString()) {
        pushModel(arr[i].asString());
      } else if (arr[i].isObject()) {
        pushModel(pullModelName(arr[i]));
      }
    }
  };

  if (root.isArray()) {
    parseModelArray(root);
  } else if (root.isObject()) {
    if (root.isMember("models")) {
      const Json::Value& models = root["models"];
      if (models.isArray()) {
        parseModelArray(models);
      } else if (models.isObject()) {
        const auto keys = models.getMemberNames();
        for (const auto& key : keys) {
          if (models[key].isObject()) {
            std::string name = pullModelName(models[key]);
            if (name.empty()) name = key;
            pushModel(name);
          } else if (models[key].isString()) {
            pushModel(models[key].asString());
          } else {
            pushModel(key);
          }
        }
      }
    } else {
      const auto keys = root.getMemberNames();
      for (const auto& key : keys) {
        if (root[key].isObject()) {
          std::string name = pullModelName(root[key]);
          if (!name.empty()) {
            pushModel(name);
          }
        }
      }
    }
  }

  if (!modelId.empty()) {
    pushModel(modelId);
  }
  if (discovered.empty()) {
    if (!modelId.empty()) {
      discovered.push_back(modelId);
    }
  }
  m_capabilities.raw_features = std::move(discovered);
  return true;
}

} // namespace animus::kernel::llm
