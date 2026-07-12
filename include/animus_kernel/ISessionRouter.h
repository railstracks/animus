#pragma once

#include <vector>

#include "animus_kernel/IncomingEvent.h"
#include "animus_kernel/Session.h"

namespace animus::kernel {

struct SessionRoutingResult {
    SessionKey primary;
    std::vector<SessionKey> context;
};

class ISessionRouter {
public:
    virtual ~ISessionRouter() = default;
    virtual SessionRoutingResult Route(const IncomingEvent& event) = 0;
};

} // namespace animus::kernel
