// Project version information.
#pragma once

#include <string>

namespace CarSim::Core
{
    /// Semantic version of the simulator, e.g. "0.1.0".
    [[nodiscard]] std::string VersionString();

    /// Human-readable product name used in window titles and logs.
    [[nodiscard]] std::string ProductName();
}
