#include "animus_kernel/llm/OpenAICodexProvider.h"
#include "animus_kernel/llm/OpenAICompat.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>

#include <curl/curl.h>

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

} // anonymous namespace

OpenAICodexProvider::OpenAICodexProvider(const LLMProviderConfig& config)
    : OpenAIProvider(config),
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
        if (content[i] == '}') { depth--; if (depth == 0) break; }
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
    if (error) *error = "No refresh token — OAuth login required";
    return false;
  }

  std::string postFields =
      "grant_type=refresh_token&refresh_token=" + m_refreshToken +
      "&client_id=" + CLIENT_ID;

  CURL* curl = curl_easy_init();
  if (!curl) {
    if (error) *error = "curl init failed for token refresh";
    return false;
  }

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
    if (error) *error = "Refresh HTTP " + std::to_string(httpCode);
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
  m_expiresMs = now + expiresIn * 1000;

  PersistTokens();
  return true;
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
  return OpenAIProvider::Complete(request, error);
}

LLMMessage OpenAICodexProvider::StreamComplete(const LLMRequest& request,
                                                LLMTokenCallback callback,
                                                std::string* error) {
  if (!EnsureTokenValid(error)) return {};
  return OpenAIProvider::StreamComplete(request, std::move(callback), error);
}

} // namespace animus::kernel::llm
