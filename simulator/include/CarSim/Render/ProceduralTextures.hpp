// Procedurally generated, tileable material textures (no external assets).
#pragma once

#include "CarSim/Render/Image.hpp"

#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cstdint>
#include <vector>

namespace CarSim::Render::Textures
{
    [[nodiscard]] Image Solid(int size, const Color& color);
    [[nodiscard]] Image Checker(int size, int cells, const Color& a, const Color& b);

    /// Worn asphalt: grey aggregate with fine speckle and faint patches. Tile ~ 4 m.
    [[nodiscard]] Image Asphalt(int size, std::uint32_t seed);
    /// Compacted gravel/forest road surface. Tile ~ 3 m.
    [[nodiscard]] Image Gravel(int size, std::uint32_t seed);
    /// Meadow grass. Tile ~ 6 m.
    [[nodiscard]] Image Grass(int size, std::uint32_t seed);
    /// Ploughed/dry field soil. Tile ~ 6 m.
    [[nodiscard]] Image Soil(int size, std::uint32_t seed);
    /// Concrete paving slabs (sidewalks). Tile ~ 2 m (4 x 4 slabs of 0.5 m).
    [[nodiscard]] Image PavingSlabs(int size, std::uint32_t seed);
    /// Rendered plaster facade in a base colour with weathering. Tile ~ 3 m.
    [[nodiscard]] Image Plaster(int size, const Rgb& base, std::uint32_t seed);
    /// Clay roof tiles. Tile ~ 2 m.
    [[nodiscard]] Image RoofTiles(int size, const Rgb& base, std::uint32_t seed);
    /// Tree bark. Tile ~ 1 m around the trunk.
    [[nodiscard]] Image Bark(int size, std::uint32_t seed);
    /// Leaf/needle cluster with alpha for cross-quad vegetation.
    [[nodiscard]] Image LeafCluster(int size, const Rgb& leaf, bool conifer, std::uint32_t seed);
    /// Sky gradient cube-map faces (+X, -X, +Y, -Y, +Z, -Z) for environment mapping.
    /// Six cube-map faces (+X, -X, +Y, -Y, +Z, -Z) of a simple sky gradient. The alpha channel
    /// carries a highlight mask around the sun direction (`toSun` must be normalised), which
    /// EnvironmentMapEffect multiplies by EnvironmentMapSpecular: alpha is zero everywhere else,
    /// so the specular term only adds the sun glint. `sunSharpness` is the exponent of the lobe.
    [[nodiscard]] std::vector<Image> SkyCubeFaces(int size, const Rgb& zenith, const Rgb& horizon, const Rgb& ground,
                                                  const Microsoft::Xna::Framework::Vector3& toSun, float sunSharpness = 400.0f);
    /// Cumulus cloud layer with alpha, tileable.
    [[nodiscard]] Image CloudLayer(int size, std::uint32_t seed);
    /// Car interior fabric/plastic grain.
    [[nodiscard]] Image InteriorGrain(int size, const Rgb& base, std::uint32_t seed);
}
