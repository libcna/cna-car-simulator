// Lit windows after dark and the lamp factor that fades them in.
#include "CarSim/Map/MapData.hpp"
#include "CarSim/Map/ObjectPlacement.hpp"
#include "CarSim/Render/BuildingGenerator.hpp"
#include "CarSim/Render/LightingRig.hpp"

#include <gtest/gtest.h>

using namespace CarSim;
using Microsoft::Xna::Framework::Vector3;

namespace
{
    Map::PlacedBuilding MakeHouse(const Map::BuildingSpec& spec, const float x, const float z)
    {
        Map::PlacedBuilding b;
        b.spec = &spec;
        b.position = Vector3(x, 0.0f, z);
        b.headingRad = 0.0f;
        b.halfWidth = spec.width * 0.5f;
        b.halfDepth = spec.depth * 0.5f;
        b.height = spec.eavesHeight;
        b.foundationDrop = 0.0f;
        return b;
    }

    std::size_t Quads(const Render::MeshData& m) { return m.vertices.size() / 4; }
}

TEST(NightLighting, SomeWindowsAreLitAndTheSplitIsStable)
{
    Map::BuildingSpec spec;
    spec.type = "house";
    spec.width = 12.0f;
    spec.depth = 10.0f;
    spec.eavesHeight = 6.5f;
    spec.floors = 2;
    spec.seed = 7u;

    // Twenty houses down a street: some windows are lit, most are not, and every window ends up
    // in exactly one of the two batches.
    std::size_t lit = 0;
    std::size_t total = 0;
    for (int i = 0; i < 20; ++i) {
        Render::BuildingMeshes meshes;
        Render::BuildingGenerator::Generate(MakeHouse(spec, static_cast<float>(i) * 18.0f, 0.0f), meshes);
        lit += Quads(meshes.windowsLit) + Quads(meshes.glassLit);
        total += Quads(meshes.windows) + Quads(meshes.windowsLit) + Quads(meshes.glassDark) + Quads(meshes.glassLit);
    }
    ASSERT_GT(total, 100u);
    EXPECT_GT(lit, total / 10);        // the street is not dark
    EXPECT_LT(lit, total * 3 / 4);     // and it is not a wall of light either

    // The same building generated twice gives the same split.
    Render::BuildingMeshes a, b;
    Render::BuildingGenerator::Generate(MakeHouse(spec, 42.0f, -13.0f), a);
    Render::BuildingGenerator::Generate(MakeHouse(spec, 42.0f, -13.0f), b);
    EXPECT_EQ(Quads(a.windowsLit), Quads(b.windowsLit));
    EXPECT_EQ(Quads(a.windows), Quads(b.windows));
    EXPECT_GT(Quads(a.windows) + Quads(a.windowsLit), 0u);
}

TEST(NightLighting, TheLampFactorFollowsTheSun)
{
    const auto at = [](const float hours) {
        Render::LightingRig rig;
        rig.SetTimeOfDay(hours);
        return rig.LampFactor();
    };
    EXPECT_FLOAT_EQ(at(13.0f), 0.0f);          // midday: lamps off
    EXPECT_FLOAT_EQ(at(1.0f), 1.0f);           // deep night: full
    EXPECT_GT(at(21.0f), 0.5f);                // after sunset
    EXPECT_LT(at(18.0f), 0.2f);                // still broad daylight in summer
    EXPECT_GT(at(21.0f), at(20.0f));           // and it comes on, never off, as the sun drops
    EXPECT_GT(at(20.0f), at(19.0f));
}
