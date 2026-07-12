#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include <drogon/WebSocketConnection.h>
#include <json/json.h>

#include "animus_kernel/KernelConfig.h"
#include "animus_kernel/admin/ObservationStreamTypes.h"

namespace animus::kernel {
namespace adminserver_internal {
bool ObservationMatchesFilter(
    const KernelConfig::MemoryObservation& observation,
    const ObservationStreamFilter& filter);
std::string JsonToCompactString(const Json::Value& value);
Json::Value BuildObservationStreamEvent(const KernelConfig::MemoryObservation& observation);
} // namespace adminserver_internal

class ObservationStreamHub {
public:
    std::uint64_t AddSubscriber(
        const drogon::WebSocketConnectionPtr& connection,
        const adminserver_internal::ObservationStreamFilter& filter);

    void UpdateFilter(
        std::uint64_t subscriberId,
        const adminserver_internal::ObservationStreamFilter& filter);

    void RemoveSubscriber(std::uint64_t subscriberId);

    void Broadcast(const KernelConfig::MemoryObservation& observation);

private:
    struct SubscriberState {
        std::uint64_t id{0};
        drogon::WebSocketConnectionPtr connection;
        adminserver_internal::ObservationStreamFilter filter;
    };

    std::mutex m_mutex;
    std::uint64_t m_nextSubscriberId{1};
    std::unordered_map<std::uint64_t, SubscriberState> m_subscribers;
};

} // namespace animus::kernel
