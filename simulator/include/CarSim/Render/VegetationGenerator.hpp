// Trees as crossed alpha-tested cards: one silhouette texture per species, three quads per
// tree with a per-tree shade in the vertex colour. Cheap enough for the forests of the sample
// map; drawn with AlphaTestEffect (no sorting).
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

        /// Silhouette texture (RGBA, premultiplied not required: alpha test) for a species.
        [[nodiscard]] static Image CardTexture(Map::TreeSpecies species, int width, int height, unsigned seed);

        /// Appends the crossed cards of one tree (world space) to `mesh`; vertex colour = shade.
        static void AppendTree(const Map::PlacedTree& tree, MeshData& mesh);

        /// Appends a simple trunk cylinder for close-up views (optional, bark textured).
        static void AppendTrunk(const Map::PlacedTree& tree, MeshData& mesh);
    };
}
