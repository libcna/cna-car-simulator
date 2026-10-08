// Uniform draws from std::mt19937 that come out the same on every standard library.
//
// The engine's sequence is fixed by the C++ standard, but std::uniform_int_distribution and
// std::uniform_real_distribution are not: libc++, libstdc++ and MSVC each map the engine's output
// to a range differently. Seeded traffic -- which car spawns where, its colour, plate and lane
// choices -- therefore differed between macOS and Linux, and a test that drives a seeded scenario
// passed on one and not the other. Every draw in the simulator goes through these instead.
#pragma once

#include <random>

namespace CarSim::Core::Random
{
    /// Uniform integer in [low, high], unbiased (rejection sampling on 32-bit draws).
    [[nodiscard]] int Int(std::mt19937& rng, int low, int high);
    /// Uniform float in [low, high), from the top 24 bits of one draw.
    [[nodiscard]] float Float(std::mt19937& rng, float low, float high);
}
