#pragma once

#include <vector>

#include "animus_kernel/ISessionRouter.h"
#include "animus_kernel/SessionRoutingRule.h"

namespace animus::kernel {

// Default router: derive SessionKey from connector metadata.
class DefaultSessionRouter final : public ISessionRouter {
public:
    DefaultSessionRouter() = default;
    explicit DefaultSessionRouter(std::vector<SessionRoutingRule> rules);

    SessionRoutingResult Route(const IncomingEvent& event) override;

private:
    std::vector<SessionRoutingRule> m_rules;
};

} // namespace animus::kernel
