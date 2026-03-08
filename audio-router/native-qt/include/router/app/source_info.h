#pragma once

#include <cstdint>
#include <string>

namespace router::app {

struct SourceInfo {
    uint32_t processId = 0;
    std::string executableName;
    std::string displayName;
    bool active = false;
};

}  // namespace router::app
