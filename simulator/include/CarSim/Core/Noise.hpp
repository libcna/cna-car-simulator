// Deterministic hash and value-noise helpers shared by terrain generation and textures.
#pragma once

#include <cstdint>

namespace CarSim::Core::Noise
{
    /// Deterministic hash in [0, 1).
    [[nodiscard]] float Hash(int x, int y, std::uint32_t seed);
    /// Tileable value noise over a `period` x `period` lattice, output [0, 1].
    [[nodiscard]] float Value(float x, float y, int period, std::uint32_t seed);
    /// Fractal Brownian motion of tileable value noise, output roughly [0, 1].
    [[nodiscard]] float Fbm(float x, float y, int period, int octaves, float persistence, std::uint32_t seed);
    /// Tileable cellular (Worley) distance to the nearest feature point, output [0, 1].
    [[nodiscard]] float Cellular(float x, float y, int period, std::uint32_t seed);
    /// Non-tileable value noise in world units (`x`, `y` already divided by the wavelength).
    [[nodiscard]] float ValueOpen(float x, float y, std::uint32_t seed);
    /// Non-tileable fBm, output roughly [-1, 1].
    [[nodiscard]] float FbmSigned(float x, float y, int octaves, float persistence, std::uint32_t seed);
}
