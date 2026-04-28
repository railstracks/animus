#include "animus_kernel/llm/OllamaProvider.h"
#include "animus_kernel/llm/OpenAICompat.h"

#include <sstream>

using namespace animus::kernel::llm::openai_compat;

namespace animus::kernel::llm {

std::string OllamaProvider::BuildRequestBody(
    const LLMRequest& request) const {
  std::ostringstream ss;
  ss << "{\"model\":\"" << EscapeJson(request.model) << "\"";

  ss << ",\"messages\":[";
  for (size_t i = 0; i < request.messages.size(); ++i) {
    if (i > 0) ss << ",";
    ss << "{\"role\":\"" << EscapeJson(request.messages[i].role)
       << "\",\"content\":\"" << EscapeJson(request.messages[i].content)
       << "\"}";
  }
  ss << "]";

  ss << ",\"temperature\":" << request.temperature;
  if (request.max_tokens > 0) {
    ss << ",\"max_tokens\":" << request.max_tokens;
  }
  ss << ",\"stream\":" << (request.stream ? "true" : "false");

  auto numCtx = Config().extra.find("num_ctx");
  auto numPredict = Config().extra.find("num_predict");
  if (numCtx != Config().extra.end() || numPredict != Config().extra.end()) {
    ss << ",\"options\":{";
    bool first = true;
    if (numCtx != Config().extra.end()) {
      ss << "\"num_ctx\":" << numCtx->second;
      first = false;
    }
    if (numPredict != Config().extra.end()) {
      if (!first) ss << ",";
      ss << "\"num_predict\":" << numPredict->second;
    }
    ss << "}";
  }

  ss << "}";
  return ss.str();
}

LLMMessage OllamaProvider::ParseResponse(const std::string& body,
                                          std::string* error) const {
  auto choicesPos = body.find("\"choices\"");
  if (choicesPos == std::string::npos) {
    auto errMsg = ExtractJsonString(body, "message");
    if (error) *error = errMsg.empty() ? "No choices in response" : errMsg;
    return {};
  }

  LLMMessage msg;
  msg.role = "assistant";
  msg.content = ExtractJsonString(body.substr(choicesPos), "content");
  return msg;
}

std::optional<LLMToken> OllamaProvider::ParseSSELine(
    const std::string& line) const {
  auto frPos = line.find("\"finish_reason\":\"stop\"");
  bool isFinal = (frPos != std::string::npos);

  auto content = ExtractJsonString(line, "content");

  LLMToken token;
  token.content = content;
  token.is_final = isFinal;
  if (isFinal) token.finish_reason = "stop";

  if (content.empty() && !isFinal) return std::nullopt;
  return token;
}

std::vector<std::pair<std::string, std::string>>
OllamaProvider::GetHeaders() const {
  std::vector<std::pair<std::string, std::string>> headers;
  headers.emplace_back("Content-Type", "application/json");
  return headers;
}

} // namespace animus::kernel::llm
