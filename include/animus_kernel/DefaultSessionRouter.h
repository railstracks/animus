#pragma once

#include "animus_kernel/ISessionRouter.h"

namespace animus::kernel {

// Default router: derive SessionKey from connector metadata.
class DefaultSessionRouter final : public ISessionRouter {
public:
    SessionRoutingResult Route(const IncomingEvent& event) override;
};

} // namespace animus::kernel
