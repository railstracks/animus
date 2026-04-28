#include "animus_kernel/llm/OpenAIProvider.h"
#include "animus_kernel/llm/OpenAICompat.h"

using namespace animus::kernel::llm::openai_compat;

namespace animus::kernel::llm {

std::string OpenAIProvider::BuildRequestBody(const LLMRequest& request) const {
  return BuildOpenAIRequestBody(request);
}

LLMMessage OpenAIProvider::ParseResponse(const std::string& body,
                                          std::string* error) const {
  auto choicesPos = body.find("\"choices\"");
  if (choicesPos == std::string::npos) {
    auto errMsg = ExtractJsonString(body, "message");
    if (error) *error = errMsg.empty() ? "No choices in response" : errMsg;
    return {};
  }

  LLMMessage msg;
  msg.role = ExtractJsonString(body.substr(choicesPos), "role");
  if (msg.role.empty()) msg.role = "assistant";
  msg.content = ExtractJsonString(body.substr(choicesPos), "content");
  return msg;
}

std::optional<LLMToken> OpenAIProvider::ParseSSELine(
    const std::string& line) const {
  auto frPos = line.find("\"finish_reason\":\"stop\"");
  bool isFinal = (frPos != std::string::npos);

  auto content = ExtractJsonString(line, "content");

  LLMToken token;
  token.content = content;
  token.is_final = isFinal;
  if (isFinal) token.finish_reason = "stop";

  if (content.empty() && !isFinal) {
    auto ptStr = ExtractJsonNumber(line, "prompt_tokens");
    auto ctStr = ExtractJsonNumber(line, "completion_tokens");
    if (!ptStr.empty()) token.prompt_tokens = std::stoi(ptStr);
    if (!ctStr.empty()) token.completion_tokens = std::stoi(ctStr);
    if (ptStr.empty() && ctStr.empty()) return std::nullopt;
  }

  return token;
}

} // namespace animus::kernel::llm
