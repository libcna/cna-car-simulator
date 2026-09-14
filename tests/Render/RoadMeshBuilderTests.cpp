// Road strips from the sample map: wear colours stay in a plausible band, verges drape from
// the shoulder down to the terrain, and markings keep their lift.
#include "CarSim/Map/MapDocument.hpp"
#include "CarSim/Map/MapWorld.hpp"
#include "CarSim/Render/RoadMeshBuilder.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace CarSim;
using namespace CarSim::Render;
using Microsoft::Xna::Framework::Vector3;

namespace
{
    std::unique_ptr<Map::MapWorld> LoadLipova()
    {
        std::vector<std::string> errors;
        auto world = Map::MapWorld::Load(Map::MapDirectory(CARSIM_TEST_CONTENT_DIR, "lipova"), errors);
        EXPECT_TRUE(errors.empty());
        return world;
    }

    float Grey(const MeshVertex& v)
    {
        return (static_cast<float>(v.color.getRProperty()) + static_cast<float>(v.color.getGProperty()) + static_cast<float>(v.color.getBProperty())) /
               (3.0f * 255.0f);
    }
}

TEST(RoadMeshBuilder, PavedSurfaceCarriesWheelTrackWearInItsVertexColours)
{
    auto world = LoadLipova();
    ASSERT_TRUE(world);
    RoadMeshBuilder builder(world->Roads());
    int checked = 0;
    for (const auto& piece : world->Roads().Pieces()) {
        const auto& road = world->Roads().Roads()[static_cast<std::size_t>(piece.road)];
        if (road.profile.surface != Sim::SurfaceType::Asphalt || piece.s1 - piece.s0 < 30.0f) continue;
        const RoadPieceMeshes meshes = builder.BuildPiece(piece);
        ASSERT_GT(meshes.paved.vertices.size(), 4u);
        float lo = 1.0f, hi = 0.0f, sum = 0.0f;
        for (const auto& v : meshes.paved.vertices) {
            const float g = Grey(v);
            lo = std::min(lo, g);
            hi = std::max(hi, g);
            sum += g;
        }
        EXPECT_GT(lo, 0.75f);
        EXPECT_LT(hi, 1.0f + 1e-3f);
        EXPECT_GT(hi - lo, 0.08f) << "tracks, drips and edges must differ";
        EXPECT_NEAR(sum / static_cast<float>(meshes.paved.vertices.size()), 0.97f, 0.06f);
        // Lateral columns: more than the two edge columns per row.
        EXPECT_GT(meshes.paved.vertices.size(), 6u * 2u);
        for (const auto& v : meshes.markings.vertices) EXPECT_GT(Grey(v), 0.75f);
        if (++checked >= 6) break;
    }
    EXPECT_GE(checked, 3);
}

TEST(RoadMeshBuilder, RuralVergesDrapeFromTheShoulderToTheTerrain)
{
    auto world = LoadLipova();
    ASSERT_TRUE(world);
    RoadMeshBuilder builder(world->Roads());
    // Without a terrain query there is no verge; with one, rural pieces get a strip whose
    // outer vertices sit just above the terrain and whose colour alpha runs from 0 to 255.
    int rural = 0;
    for (const auto& piece : world->Roads().Pieces()) {
        const auto& road = world->Roads().Roads()[static_cast<std::size_t>(piece.road)];
        const auto& samples = road.curve.Samples();
        bool urban = false;
        for (std::size_t i = piece.sampleBegin; i < piece.sampleEnd && i < samples.size(); ++i) urban = urban || samples[i].urban;
        if (urban || piece.s1 - piece.s0 < 30.0f) continue;
        EXPECT_EQ(builder.BuildPiece(piece).verge.TriangleCount(), 0u);
        builder.SetTerrainHeight([&](const float x, const float z) { return world->Terrain().Height(x, z); });
        const RoadPieceMeshes meshes = builder.BuildPiece(piece);
        builder.SetTerrainHeight({});
        ASSERT_GT(meshes.verge.TriangleCount(), 0u);
        bool sawInner = false, sawOuter = false;
        for (const auto& v : meshes.verge.vertices) {
            const int a = static_cast<int>(v.color.getAProperty());
            if (a == 0) sawInner = true;
            if (a == 255) {
                sawOuter = true;
                const float terrain = world->Terrain().Height(v.position.X, v.position.Z);
                EXPECT_NEAR(v.position.Y, terrain + 0.05f, 0.02f);
            }
        }
        EXPECT_TRUE(sawInner);
        EXPECT_TRUE(sawOuter);
        if (++rural >= 4) break;
    }
    EXPECT_GE(rural, 2);
}
