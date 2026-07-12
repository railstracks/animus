#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace animus::kernel::adminserver_internal {

struct ObservationStreamFilter {
    std::vector<std::string> layers;
    std::vector<std::string> tags;
    std::uint64_t lastSeenTimestampUnixMs{0};
};

} // namespace animus::kernel::adminserver_internal
