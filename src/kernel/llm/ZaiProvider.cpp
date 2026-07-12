#include "animus_kernel/llm/ZaiProvider.h"
#include "animus_kernel/llm/OpenAICompat.h"

#include <algorithm>
#include <cctype>
#include <iostream>

using namespace animus::kernel::llm::openai_compat;

namespace animus::kernel::llm {

namespace {

// Z.ai vision-capable model families: GLM-4V, GLM-4.5V, GLM-4.6V,
// GLM-5V-Turbo, GLM-5.1V, GLM-5.2V, etc.
bool IsZaiVisionModel(const std::string& modelId) {
  std::string lower = modelId;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                  [](unsigned char c) { return std::tolower(c); });
  // Any GLM model with 'v' suffix/version indicating vision
  if (lower.find("glm-") != std::string::npos &&
      lower.find('v') != std::string::npos) return true;
  // GLM-5.x and later all support vision natively
  if (lower.rfind("glm-5", 0) == 0) return true;
  if (lower.rfind("glm-4.6", 0) == 0) return true;
  if (lower.rfind("glm-4.5", 0) == 0) return true;
  return false;
}

} // namespace

std::string ZaiProvider::BuildRequestBody(const LLMRequest& request) const {
  std::string body = BuildOpenAIRequestBody(request);

  // When reasoning effort is set, add the "thinking" parameter.
  // Z.ai supports: "thinking": {"type": "enabled"}
  // Z.ai does not support effort levels — all non-empty values enable thinking.
  if (IsReasoningEnabled(request.reasoning_effort)) {
    body.insert(body.size() - 1, ",\"thinking\":{\"type\":\"enabled\"}");
  }

  return body;
}

LLMMessage ZaiProvider::ParseResponse(const std::string& body,
                                       std::string* error) const {
  return openai_compat::ParseResponseWithReasoning(body, error);
}

std::optional<LLMToken> ZaiProvider::ParseSSELine(
    const std::string& line) const {
  return openai_compat::ParseSSELineWithReasoning(line);
}

std::vector<std::pair<std::string, std::string>>
ZaiProvider::GetHeaders() const {
  auto headers = LLMProviderBase::GetHeaders();
  headers.emplace_back("Accept-Language", "en");
  return headers;
}

// ---------------------------------------------------------------------------
// FetchCapabilities — Z.ai model capability detection
// ---------------------------------------------------------------------------

bool ZaiProvider::FetchCapabilities(const std::string& modelId) {
  bool ok = LLMProviderBase::FetchCapabilities(modelId);

  m_capabilities.supports_vision = IsZaiVisionModel(modelId);

  // GLM-5.x supports reasoning/thinking
  std::string lower = modelId;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                  [](unsigned char c) { return std::tolower(c); });
  m_capabilities.supports_reasoning =
      (lower.rfind("glm-5", 0) == 0 ||
       lower.rfind("glm-4.6", 0) == 0);

  std::cerr << "[zai] Capabilities for " << modelId << ": vision="
            << m_capabilities.supports_vision
            << " reasoning=" << m_capabilities.supports_reasoning
            << std::endl;
  return ok;
}

} // namespace animus::kernel::llm
