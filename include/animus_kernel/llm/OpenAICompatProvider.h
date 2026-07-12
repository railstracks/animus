#pragma once

#include "animus_kernel/llm/OpenAIProvider.h"

#include <algorithm>
#include <cctype>

namespace animus::kernel::llm {

// ============================================================================
// OpenAICompatProvider — generic OpenAI-compatible endpoint
// ============================================================================
//
// For providers that expose a standard /v1/chat/completions endpoint but
// don't warrant a dedicated implementation. The user supplies base_url,
// api_key, and model name. Capability detection uses heuristic model-name
// pattern matching — no provider API calls, no hard-coded assumptions.
//
// This is the maintenance-free catch-all: if a provider changes their API,
// the user updates their config. No Animus code changes needed.
//
// Examples: Groq, Together AI, Fireworks, Novita, Hyperbolic, Lepton,
// Infermatic, AI21, Friendli, Replicate, and any future OpenAI-compatible
// endpoint.

class OpenAICompatProvider : public OpenAIProvider {
public:
  explicit OpenAICompatProvider(const LLMProviderConfig& config)
      : OpenAIProvider(config) {}

  std::string ProviderId() const override { return "openai-compatible"; }

  bool FetchCapabilities(const std::string& modelId) override {
    bool ok = OpenAIProvider::FetchCapabilities(modelId);

    std::string lower = modelId;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                    [](unsigned char c) { return std::tolower(c); });

    // Vision heuristics — common patterns across providers
    bool vision = false;
    if (lower.find("vision") != std::string::npos) vision = true;
    if (lower.find("vl") != std::string::npos) vision = true;
    if (lower.find("multimodal") != std::string::npos) vision = true;
    if (lower.find("4o") != std::string::npos) vision = true;  // gpt-4o
    if (lower.find("gemini") != std::string::npos) vision = true;
    if (lower.find("claude") != std::string::npos) vision = true;
    if (lower.find("pixtral") != std::string::npos) vision = true;

    // Reasoning heuristics
    bool reasoning = false;
    if (lower.find("reasoner") != std::string::npos) reasoning = true;
    if (lower.find("thinking") != std::string::npos) reasoning = true;
    if (lower.find("-r1") != std::string::npos) reasoning = true;
    if (lower.find("o1") == 0 || lower.find("o3") == 0 ||
        lower.find("o4") == 0) reasoning = true;

    m_capabilities.supports_vision = vision;
    m_capabilities.supports_reasoning = reasoning;

    std::cerr << "[openai-compatible] Capabilities for " << modelId
              << ": vision=" << m_capabilities.supports_vision
              << " reasoning=" << m_capabilities.supports_reasoning
              << std::endl;
    return ok;
  }
};

} // namespace animus::kernel::llm
