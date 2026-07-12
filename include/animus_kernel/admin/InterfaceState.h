#pragma once

#include <cstdint>
#include <string>

#include <json/value.h>

namespace animus::kernel {

struct InterfaceState {
    std::string moduleId;
    std::string name;
    std::string type;
    bool enabled{true};
    bool connected{true};
    std::uint64_t lastEventUnixMs{0};
    Json::Value config{Json::objectValue};
};

} // namespace animus::kernel

