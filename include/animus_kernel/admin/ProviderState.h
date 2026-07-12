#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "animus_kernel/llm/LLMTypes.h"

namespace animus::kernel {

struct ProviderState {
    std::string providerId;     // unique instance name (user-defined)
    std::string providerType;   // "openai", "zai", "ollama", etc.
    std::string baseUrl;
    std::string apiKey;
    std::string defaultModel;
    std::uint32_t defaultContextWindow{128000};
    std::unordered_map<std::string, std::uint32_t> modelContextWindows;
    std::string authType{"api_key"};  // "api_key", "oauth", "none"
    std::string authFile;             // for oauth providers (e.g. config/auth.json)
    std::unordered_map<std::string, std::string> extra;
    std::string status{"untested"};   // "untested", "available", "error"
    std::string lastError;
    std::uint64_t lastTestedUnixMs{0};
    int concurrency{1};

    // Cached capabilities from provider API
    llm::ProviderCapabilities capabilities;
};

} // namespace animus::kernel
