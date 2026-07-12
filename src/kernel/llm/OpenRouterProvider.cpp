#include "animus_kernel/llm/OpenRouterProvider.h"
#include "animus_kernel/llm/OpenAICompat.h"

#include <algorithm>
#include <cctype>
#include <iostream>

using namespace animus::kernel::llm::openai_compat;

namespace animus::kernel::llm {

// ---------------------------------------------------------------------------
// GetHeaders — standard OpenAI + OpenRouter app identification
// ---------------------------------------------------------------------------

std::vector<std::pair<std::string, std::string>>
OpenRouterProvider::GetHeaders() const {
  auto headers = LLMProviderBase::GetHeaders();
  // OpenRouter uses these for app ranking in their dashboard.
  // Optional but recommended for visibility.
  headers.emplace_back("HTTP-Referer", "https://animus.steadyfort.com");
  headers.emplace_back("X-Title", "Animus");
  return headers;
}

// ---------------------------------------------------------------------------
// BuildRequestBody — standard OpenAI format
// ---------------------------------------------------------------------------

std::string OpenRouterProvider::BuildRequestBody(
    const LLMRequest& request) const {
  // OpenRouter accepts standard OpenAI /v1/chat/completions requests.
  // No custom parameters needed for v1.
  return BuildOpenAIRequestBody(request);
}

// ---------------------------------------------------------------------------
// ParseResponse / ParseSSELine — standard OpenAI parsing
// ---------------------------------------------------------------------------

LLMMessage OpenRouterProvider::ParseResponse(const std::string& body,
                                              std::string* error) const {
  return ParseResponseWithReasoning(body, error);
}

std::optional<LLMToken> OpenRouterProvider::ParseSSELine(
    const std::string& line) const {
  return ParseSSELineWithReasoning(line);
}

// ---------------------------------------------------------------------------
// ParseToolCallsFromResponse
// ---------------------------------------------------------------------------

void OpenRouterProvider::ParseToolCallsFromResponse(
    const std::string& body,
    std::vector<LLMToolCall>& tool_calls,
    std::string& finish_reason) const {
  tool_calls = ParseToolCalls(body);
  finish_reason = ExtractFinishReason(body);
}

// ---------------------------------------------------------------------------
// FetchCapabilities — conservative defaults + model-name heuristics
// ---------------------------------------------------------------------------

bool OpenRouterProvider::FetchCapabilities(const std::string& modelId) {
  bool ok = LLMProviderBase::FetchCapabilities(modelId);

  // OpenRouter normalizes tool calling across providers, so most models
  // support it. Conservative default: true.
  m_capabilities.supports_tools = true;
  m_capabilities.supports_streaming = true;

  // Vision detection via model-name heuristics.
  // OpenRouter model names use "provider/model" format.
  std::string lowerId = modelId;
  std::transform(lowerId.begin(), lowerId.end(), lowerId.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  static const std::vector<std::string> kVisionIndicators = {
      "vision", "-vl", "multimodal", "4o",           // OpenAI GPT-4o
      "gemini",                                        // Google Gemini
      "claude",                                        // Most Claude models
      "llava", "pixtral", "moondream",                // Open vision models
      "qwen2.5-vl", "qwen2-vl", "qwen-vl",           // Qwen VL
      "internvl",                                      // InternVL
      "minicpm-v",                                     // MiniCPM-V
      "gemma3", "gemma4",                             // Google Gemma
      "llama3.2-vision", "llama3-vision",             // Meta Llama Vision
  };

  m_capabilities.supports_vision = false;
  for (const auto& indicator : kVisionIndicators) {
    if (lowerId.find(indicator) != std::string::npos) {
      m_capabilities.supports_vision = true;
      break;
    }
  }

  // Reasoning/thinking detection
  static const std::vector<std::string> kReasoningIndicators = {
      "reasoning", "thinking", "r1", "o1", "o3", "o4",
      "deepseek-r", "deepseek-v4-pro",
  };

  m_capabilities.supports_reasoning = false;
  for (const auto& indicator : kReasoningIndicators) {
    if (lowerId.find(indicator) != std::string::npos) {
      m_capabilities.supports_reasoning = true;
      break;
    }
  }

  std::cerr << "[openrouter] Capabilities for " << modelId << ": vision="
            << m_capabilities.supports_vision
            << " reasoning=" << m_capabilities.supports_reasoning
            << " tools=" << m_capabilities.supports_tools
            << std::endl;
  return ok;
}

// ---------------------------------------------------------------------------
// GetVisionModelIds — filter raw_features for vision-capable models
// ---------------------------------------------------------------------------

std::vector<std::string> OpenRouterProvider::GetVisionModelIds() const {
  static const std::vector<std::string> kVisionIndicators = {
      "vision", "-vl", "multimodal", "4o",
      "gemini", "claude",
      "llava", "pixtral", "moondream",
      "qwen2.5-vl", "qwen2-vl", "qwen-vl",
      "internvl", "minicpm-v",
      "gemma3", "gemma4",
      "llama3.2-vision", "llama3-vision",
  };

  std::vector<std::string> result;
  for (const auto& model : m_capabilities.raw_features) {
    std::string lowerId = model;
    std::transform(lowerId.begin(), lowerId.end(), lowerId.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    for (const auto& indicator : kVisionIndicators) {
      if (lowerId.find(indicator) != std::string::npos) {
        result.push_back(model);
        break;
      }
    }
  }
  return result;
}

} // namespace animus::kernel::llm
