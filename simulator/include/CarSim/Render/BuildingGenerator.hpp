// Procedural buildings for the Czech town: houses, cottages, prefab blocks, church, barn,
// shop, hall and chapel, generated into material batches (wall palette, roof palette,
// windows, trim) from the map's placed buildings.
#pragma once

#include "CarSim/Map/ObjectPlacement.hpp"
#include "CarSim/Render/Image.hpp"
#include "CarSim/Render/MeshData.hpp"

#include <array>
#include <vector>

namespace CarSim::Render
{
    struct BuildingPalette
    {
        static constexpr int kWallColours = 8;
        static constexpr int kRoofColours = 4;
        [[nodiscard]] static Rgb Wall(int index);
        [[nodiscard]] static Rgb Roof(int index);
        [[nodiscard]] static int WallIndex(const Map::PlacedBuilding& b);
        [[nodiscard]] static int RoofIndex(const Map::PlacedBuilding& b);
    };

    struct BuildingMeshes
    {
        std::array<MeshData, BuildingPalette::kWallColours> walls;
        std::array<MeshData, BuildingPalette::kRoofColours> roofs;
        MeshData windows;
        MeshData trim;      // doors, plinths, chimneys (dark brown)
        MeshData glassDark; // shop fronts and block windows (darker, no frame)
        MeshData frames;    // window frames, fascia boards, cornices, canopies, chimney caps (off-white)
        MeshData metal;     // gutters, downpipes, railings (galvanised grey)
        MeshData dark;      // reveal shadow lines, chimney pots (near black)
        MeshData concrete;  // doorsteps, block canopies
    };

    class BuildingGenerator
    {
    public:
        /// Appends the building's geometry (world space) to the batches.
        static void Generate(const Map::PlacedBuilding& building, BuildingMeshes& out);

        /// Window texture (frame, glass, curtains) and dark glass used by the batches.
        [[nodiscard]] static Image WindowTexture(int size, unsigned seed);
    };
}
