#include "MapTestUtil.hpp"

#include "CarSim/Map/MapWorld.hpp"
#include "CarSim/Map/TerrainField.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace CarSim;
using namespace CarSim::Test;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;

TEST(TerrainField, FeaturesAndNoiseAreDeterministic)
{
    Map::TerrainSpec spec;
    spec.sizeX = spec.sizeZ = 1000.0f;
    spec.cellSize = 5.0f;
    spec.noiseAmplitude = 0.0f;
    Map::TerrainFeatureSpec hill;
    hill.center = Vector2(100.0f, -50.0f);
    hill.radius = 100.0f;
    hill.height = 20.0f;
    spec.features.push_back(hill);
    Map::TerrainField a;
    a.Build(spec);
    EXPECT_NEAR(a.RawHeight(100.0f, -50.0f), 20.0f, 1e-4f);
    EXPECT_NEAR(a.RawHeight(100.0f, 50.0f), 20.0f * std::exp(-2.0f), 1e-3f);
    EXPECT_NEAR(a.Height(100.0f, -50.0f), 20.0f, 1e-3f);
    spec.noiseAmplitude = 4.0f;
    Map::TerrainField b;
    b.Build(spec);
    Map::TerrainField c;
    c.Build(spec);
    EXPECT_FLOAT_EQ(b.Height(123.0f, 77.0f), c.Height(123.0f, 77.0f));
    EXPECT_NE(b.Height(123.0f, 77.0f), a.Height(123.0f, 77.0f));
    EXPECT_GE(b.MaxHeight(), b.MinHeight());
}

TEST(TerrainField, BilinearHeightIsContinuous)
{
    Map::TerrainSpec spec;
    spec.sizeX = spec.sizeZ = 500.0f;
    spec.cellSize = 5.0f;
    spec.noiseAmplitude = 6.0f;
    Map::TerrainField t;
    t.Build(spec);
    float previous = t.Height(-100.0f, 20.0f);
    for (float x = -100.0f; x < 100.0f; x += 0.25f) {
        const float h = t.Height(x, 20.0f);
        EXPECT_LT(std::fabs(h - previous), 0.6f);
        previous = h;
    }
    const Vector3 n = t.Normal(10.0f, 10.0f);
    EXPECT_NEAR(n.Length(), 1.0f, 1e-4f);
    EXPECT_GT(n.Y, 0.9f);
}

TEST(TerrainField, ConformsToRoads)
{
    auto data = FlatMap(1000.0f);
    data.terrain.cellSize = 4.0f;
    data.terrain.noiseAmplitude = 0.0f;
    Map::TerrainFeatureSpec hill;
    hill.center = Vector2(0.0f, 0.0f);
    hill.radius = 150.0f;
    hill.height = 12.0f;
    data.terrain.features.push_back(hill);
    data.nodes = {Node("a", -400, 0), Node("b", 400, 0)};
    data.roads = {Road("r", {"a", "b"})};
    auto world = BuildWorld(data);
    ASSERT_TRUE(world);
    const auto& terrain = world->Terrain();
    const auto& roads = world->Roads();
    // Under the road centre the terrain sits just below the road surface.
    Map::RoadHit hit;
    ASSERT_TRUE(roads.NearestRoad(Vector2(0.0f, 0.0f), 5.0f, hit));
    const float roadY = roads.SurfaceHeight(hit);
    EXPECT_LT(terrain.Height(0.0f, 0.0f), roadY);
    EXPECT_GT(terrain.Height(0.0f, 0.0f), roadY - 0.3f);
    // Far from the road the terrain is untouched.
    EXPECT_NEAR(terrain.Height(0.0f, 120.0f), terrain.RawHeight(0.0f, 120.0f), 1e-3f);
    // The blend zone is monotonic between the edge and the raw terrain.
    const float edge = terrain.Height(0.0f, 4.0f);
    const float mid = terrain.Height(0.0f, 10.0f);
    const float far = terrain.Height(0.0f, 20.0f);
    EXPECT_TRUE((edge <= mid && mid <= far) || (edge >= mid && mid >= far));
    EXPECT_LT(terrain.RoadDistanceAtVertex(terrain.Columns() / 2, terrain.Rows() / 2), 0.5f);
}

TEST(MapGround, RaycastsHitRoadAndTerrain)
{
    auto data = FlatMap(1000.0f);
    data.terrain.baseHeight = 3.0f;
    data.nodes = {Node("a", 0, 200), Node("b", 0, -200)};
    data.roads = {Road("r", {"a", "b"})};
    auto world = BuildWorld(data);
    ASSERT_TRUE(world);
    const auto& ground = world->Ground();
    Sim::GroundHit hit;
    ASSERT_TRUE(ground.Raycast(Vector3(1.0f, 10.0f, 0.0f), Vector3(0.0f, -1.0f, 0.0f), 20.0f, hit));
    EXPECT_EQ(hit.surface, Sim::SurfaceType::Asphalt);
    EXPECT_NEAR(hit.point.Y, 3.0f - 0.02f, 0.05f);   // crown at 1 m lateral
    EXPECT_NEAR(hit.distance, 7.0f, 0.1f);
    EXPECT_GT(hit.normal.Y, 0.99f);
    ASSERT_TRUE(ground.Raycast(Vector3(60.0f, 10.0f, 0.0f), Vector3(0.0f, -1.0f, 0.0f), 20.0f, hit));
    EXPECT_EQ(hit.surface, Sim::SurfaceType::Grass);
    EXPECT_NEAR(hit.point.Y, 3.0f, 1e-3f);
    EXPECT_FALSE(ground.Raycast(Vector3(60.0f, 10.0f, 0.0f), Vector3(0.0f, -1.0f, 0.0f), 5.0f, hit));
    const auto sample = ground.Sample(0.0f, 0.0f);
    EXPECT_TRUE(sample.onRoad);
    EXPECT_LT(sample.distanceToPavedEdge, 0.0f);
    const auto spawn = world->PlayerSpawn();
    const Vector3 p = world->SpawnPosition(spawn);
    EXPECT_NEAR(p.Y, 3.0f, 0.2f);
}
