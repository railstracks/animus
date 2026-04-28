#include "animus_kernel/llm/CohereProvider.h"
#include "animus_kernel/llm/OpenAICompat.h"

#include <sstream>

using namespace animus::kernel::llm::openai_compat;

namespace animus::kernel::llm {

std::string CohereProvider::GetEndpointURL() const {
  return Config().base_url + "/chat";
}

std::string CohereProvider::BuildRequestBody(
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

  auto safetyMode = Config().extra.find("safety_mode");
  if (safetyMode != Config().extra.end()) {
    ss << ",\"safety_mode\":\"" << safetyMode->second << "\"";
  }

  auto k = Config().extra.find("k");
  if (k != Config().extra.end()) {
    ss << ",\"k\":" << k->second;
  }

  auto p = Config().extra.find("p");
  if (p != Config().extra.end()) {
    ss << ",\"p\":" << p->second;
  }

  ss << "}";
  return ss.str();
}

LLMMessage CohereProvider::ParseResponse(const std::string& body,
                                          std::string* error) const {
  auto msgPos = body.find("\"message\"");
  if (msgPos == std::string::npos) {
    if (error) *error = "No message in Cohere response";
    return {};
  }

  LLMMessage msg;
  msg.role = "assistant";

  auto contentStr = ExtractJsonString(body.substr(msgPos), "content");
  if (!contentStr.empty()) {
    msg.content = contentStr;
  } else {
    auto textPos = body.find("\"text\"", msgPos);
    if (textPos != std::string::npos) {
      msg.content = ExtractJsonString(body.substr(textPos), "text");
    }
  }

  return msg;
}

std::optional<LLMToken> CohereProvider::ParseSSELine(
    const std::string& line) const {
  auto type = ExtractJsonString(line, "type");

  if (type == "message-end") {
    LLMToken token;
    token.is_final = true;
    token.finish_reason = "stop";
    return token;
  }

  if (type == "content-delta") {
    auto text = ExtractJsonString(line, "text");
    if (text.empty()) return std::nullopt;

    LLMToken token;
    token.content = text;
    return token;
  }

  return std::nullopt;
}

bool CohereProvider::IsSSEDone(const std::string& line) const {
  auto type = ExtractJsonString(line, "type");
  return type == "message-end";
}

} // namespace animus::kernel::llm
