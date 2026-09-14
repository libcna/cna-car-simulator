// Street furniture and roadside objects: bus stop shelter, benches, lamp posts, fences,
// walls, gates, timber stacks, hydrants, bins and Z 11 delineator posts.
#pragma once

#include "CarSim/Map/ObjectPlacement.hpp"
#include "CarSim/Render/MeshData.hpp"

namespace CarSim::Render
{
    struct PropMeshes
    {
        MeshData metal;      // galvanised grey: posts, shelters, lamp poles
        MeshData wood;       // benches, fences, timber, gate
        MeshData concrete;   // walls, kerbstones
        MeshData white;      // delineator posts (white plastic), bins
        MeshData black;      // delineator bands, bin lids, rubber
        MeshData reflectorOrange;
        MeshData reflectorWhite;
        MeshData glass;      // shelter panels (dark tinted)
        MeshData red;        // hydrant
        MeshData hedge;      // clipped hedges (leafy green)
    };

    class PropGenerator
    {
    public:
        /// Appends one prop (world space).
        static void Generate(const Map::PlacedProp& prop, PropMeshes& out);
    };
}
