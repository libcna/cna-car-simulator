// Trees as crossed alpha-tested cards: two silhouettes in one atlas per species, three quads
// per tree with a per-tree shade in the vertex colour. Drawn with AlphaTestEffect (no sorting).
#pragma once

#include "CarSim/Map/ObjectPlacement.hpp"
#include "CarSim/Render/Image.hpp"
#include "CarSim/Render/MeshData.hpp"

namespace CarSim::Render
{
    class VegetationGenerator
    {
    public:
        static constexpr int kSpeciesCount = 8;
        static constexpr int kCardWidth = 256;
        static constexpr int kCardHeight = 512;
        static constexpr int kAtlasPadding = 16;
        static constexpr int kAtlasWidth = 2 * kCardWidth + 3 * kAtlasPadding;

        /// Silhouette texture (RGBA, premultiplied not required: alpha test) for a species.
        [[nodiscard]] static Image CardTexture(Map::TreeSpecies species, int width, int height, unsigned seed);
        /// Two seeded silhouettes separated by transparent gutters in one species texture.
        [[nodiscard]] static Image CardAtlasTexture(Map::TreeSpecies species, unsigned seed);
        /// A frosted palette of the same atlas: identical alpha and silhouette, with snow
        /// settling on foliage while the trunk remains exposed.
        [[nodiscard]] static Image WinterAtlasTexture(const Image& summer, Map::TreeSpecies species, unsigned seed);
        /// Blend matching seasonal atlases without changing their alpha-tested silhouette.
        [[nodiscard]] static Image BlendSeasonalAtlases(const Image& summer, const Image& winter, float snowCover);

        /// Appends the crossed cards of one tree (world space) to `mesh`; vertex colour = shade.
        static void AppendTree(const Map::PlacedTree& tree, MeshData& mesh);

        /// Appends a simple trunk cylinder for close-up views (optional, bark textured).
        static void AppendTrunk(const Map::PlacedTree& tree, MeshData& mesh);
    };
}
