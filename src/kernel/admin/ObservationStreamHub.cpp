#include "animus_kernel/admin/ObservationStreamHub.h"

#include <vector>

namespace animus::kernel {

std::uint64_t ObservationStreamHub::AddSubscriber(
    const drogon::WebSocketConnectionPtr& connection,
    const adminserver_internal::ObservationStreamFilter& filter) {
    if (!connection) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::uint64_t id = m_nextSubscriberId++;
    m_subscribers[id] = SubscriberState{id, connection, filter};
    return id;
}

void ObservationStreamHub::UpdateFilter(
    std::uint64_t subscriberId,
    const adminserver_internal::ObservationStreamFilter& filter) {
    if (subscriberId == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_subscribers.find(subscriberId);
    if (it != m_subscribers.end()) {
        it->second.filter = filter;
    }
}

void ObservationStreamHub::RemoveSubscriber(std::uint64_t subscriberId) {
    if (subscriberId == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_subscribers.erase(subscriberId);
}

void ObservationStreamHub::Broadcast(const KernelConfig::MemoryObservation& observation) {
    const std::string encoded = adminserver_internal::JsonToCompactString(
        adminserver_internal::BuildObservationStreamEvent(observation));

    std::vector<std::uint64_t> staleSubscriberIds;
    std::vector<drogon::WebSocketConnectionPtr> recipients;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& pair : m_subscribers) {
            auto& subscriber = pair.second;
            if (!subscriber.connection || !subscriber.connection->connected()) {
                staleSubscriberIds.push_back(pair.first);
                continue;
            }
            if (!adminserver_internal::ObservationMatchesFilter(observation, subscriber.filter)) {
                continue;
            }
            recipients.push_back(subscriber.connection);
        }
        for (const std::uint64_t id : staleSubscriberIds) {
            m_subscribers.erase(id);
        }
    }
    for (const auto& recipient : recipients) {
        if (recipient && recipient->connected()) {
            recipient->send(encoded);
        }
    }
}

} // namespace animus::kernel
