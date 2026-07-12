#pragma once

#include <cstdint>
#include <string>

namespace animus::kernel {

struct PendingOAuthCodeFlowState {
    std::string state;
    std::string codeVerifier;
    std::string redirectUri;
    std::string clientId;      // OAuth client_id (needed for token exchange)
    std::string clientSecret;  // OAuth client_secret (empty for public clients)
    std::uint64_t createdUnixMs{0};
    std::uint64_t expiresUnixMs{0};
};

} // namespace animus::kernel
