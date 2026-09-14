// Generates road strips (paved surface, shoulders, kerbs, sidewalks), markings and intersection
// patches as MeshData from the RoadNetwork. Pure geometry: no graphics device involved.
#pragma once

#include "CarSim/Map/RoadNetwork.hpp"
#include "CarSim/Render/MeshData.hpp"

namespace CarSim::Render
{
    struct RoadPieceMeshes
    {
        MeshData paved;      // asphalt/gravel surface including edge strips
        MeshData shoulder;   // unpaved shoulders (gravel texture)
        MeshData sidewalk;   // paving slabs (urban stretches with sidewalks)
        MeshData kerb;       // concrete kerb faces
        MeshData markings;   // white lines, slightly above the surface
    };

    class RoadMeshBuilder
    {
    public:
        explicit RoadMeshBuilder(const Map::RoadNetwork& network) : network_(network) {}

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
    };
}
