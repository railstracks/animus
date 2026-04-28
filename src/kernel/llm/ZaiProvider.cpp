#include "animus_kernel/llm/ZaiProvider.h"
#include "animus_kernel/llm/OpenAICompat.h"

using namespace animus::kernel::llm::openai_compat;

namespace animus::kernel::llm {

std::string ZaiProvider::BuildRequestBody(const LLMRequest& request) const {
  std::string body = BuildOpenAIRequestBody(request);

  auto it = Config().extra.find("thinking");
  if (it != Config().extra.end()) {
    auto pos = body.rfind('}');
    if (pos != std::string::npos) {
      body.insert(pos, ",\"thinking\":\"" + it->second + "\"");
    }
  }

  return body;
}

LLMMessage ZaiProvider::ParseResponse(const std::string& body,
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

  auto thinkingContent = ExtractJsonString(body.substr(choicesPos), "thinking_content");
  if (!thinkingContent.empty()) {
    msg.content = "[thinking] " + thinkingContent + "\n[/thinking]\n" + msg.content;
  }

  return msg;
}

std::optional<LLMToken> ZaiProvider::ParseSSELine(
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
ZaiProvider::GetHeaders() const {
  auto headers = LLMProviderBase::GetHeaders();
  headers.emplace_back("Accept-Language", "en");
  return headers;
}

} // namespace animus::kernel::llm
