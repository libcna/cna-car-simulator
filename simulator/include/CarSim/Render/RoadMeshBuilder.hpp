// Generates road strips (paved surface, shoulders, kerbs, sidewalks), markings and intersection
// patches as MeshData from the RoadNetwork. Pure geometry: no graphics device involved.
#pragma once

#include "CarSim/Map/RoadNetwork.hpp"
#include "CarSim/Render/MeshData.hpp"

#include <functional>

namespace CarSim::Render
{
    /// Vertex colours carry surface wear (rgb, 1.0 = clean); the renderer multiplies the baked
    /// lighting and ground shadows into them. Verge vertices use alpha as the terrain-tint blend
    /// (0 at the road edge, 255 where the strip meets the terrain).
    struct RoadPieceMeshes
    {
        MeshData paved;      // asphalt/gravel surface including edge strips, lateral columns with wheel-track wear
        MeshData snowBase;  // coarse unpatched asphalt for snow; empty when no cuts were added
        MeshData shoulder;   // unpaved shoulders (gravel texture)
        MeshData sidewalk;   // paving slabs (urban stretches with sidewalks)
        MeshData kerb;       // concrete kerb faces
        MeshData markings;   // white lines, slightly above the surface
        MeshData verge;      // grass strip from the shoulder down to the terrain (rural stretches)
    };

    class RoadMeshBuilder
    {
    public:
        explicit RoadMeshBuilder(const Map::RoadNetwork& network) : network_(network) {}

        /// Terrain height query (world x, z); when set, rural pieces get a verge strip that
        /// drapes from the shoulder edge down to the terrain so the road no longer floats.
        void SetTerrainHeight(std::function<float(float, float)> terrainHeight) { terrainHeight_ = std::move(terrainHeight); }

        /// Width of the verge strip outside the shoulder (metres).
        static constexpr float kVergeWidthM = 2.0f;

        /// Strip meshes for one road piece.
        [[nodiscard]] RoadPieceMeshes BuildPiece(const Map::RoadPiece& piece) const;

        /// Flat fan mesh of an intersection patch plus its give-way/stop lines.
        void BuildIntersection(const Map::Intersection& intersection, MeshData& paved, MeshData& markings) const;

        /// Pedestrian crossing (V 7): 0.5 m bars across the paved width at the road point nearest
        /// to `position` (typically an IP 6 sign). Returns false when no road is near.
        [[nodiscard]] bool BuildCrossing(const Microsoft::Xna::Framework::Vector2& position, MeshData& markings) const;

        /// Lift of the marking geometry above the road surface (metres).
        static constexpr float kMarkingLift = 0.008f;

    private:
        const Map::RoadNetwork& network_;
        std::function<float(float, float)> terrainHeight_;
    };
}
