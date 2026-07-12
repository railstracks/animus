#include "animus_kernel/AdminServer.h"
#include "animus_kernel/AgentStore.h"
#include "animus_kernel/ChainRunner.h"
#include "animus_kernel/ProviderThrottle.h"
#include "animus_kernel/SessionManager.h"
#include "kernel/admin/internal/AdminServerInternals.h"

#include <drogon/drogon.h>
#include <drogon/WebSocketController.h>

namespace animus::kernel {
using namespace adminserver_internal;

namespace {
#include "kernel/admin/internal/AdminServerWebSocketControllers.inc"
#include "kernel/admin/internal/AdminServerNodeWebSocket.inc"
}

void AdminServer::RegisterWebSocketControllers() {
    drogon::app().registerController(std::make_shared<AdminChatWebSocketController>());
    drogon::app().registerController(std::make_shared<AdminObservationWebSocketController>());
    drogon::app().registerController(std::make_shared<NodeWebSocketController>());
}

} // namespace animus::kernel
