#include "MapTestUtil.hpp"

#include "CarSim/Map/RoadNetwork.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace CarSim;
using namespace CarSim::Test;
using Microsoft::Xna::Framework::Vector2;

namespace
{
    Map::RoadNetwork BuildNetwork(const Map::MapData& data, const Map::HeightSampler& height = [](float, float) { return 0.0f; })
    {
        Map::RoadNetwork network;
        std::vector<std::string> errors;
        EXPECT_TRUE(network.Build(data, height, errors));
        for (const auto& e : errors) {
            ADD_FAILURE() << e;
        }
        return network;
    }
}

TEST(RoadNetwork, StraightRoadLengthAndProjection)
{
    auto data = FlatMap();
    data.nodes = {Node("a", 0, 0), Node("b", 0, -500)};
    data.roads = {Road("r", {"a", "b"})};
    const auto network = BuildNetwork(data);
    ASSERT_EQ(network.Roads().size(), 1u);
    const auto& road = network.Roads()[0];
    EXPECT_NEAR(road.curve.Length(), 500.0f, 0.01f);
    float s = 0.0f;
    float lateral = 0.0f;
    const float d = road.curve.Project(Vector2(2.0f, -250.0f), s, lateral);
    EXPECT_NEAR(d, 2.0f, 1e-3f);
    EXPECT_NEAR(s, 250.0f, 0.5f);
    EXPECT_NEAR(lateral, 2.0f, 1e-3f);   // travelling north, +x is on the right
    EXPECT_TRUE(network.Intersections().empty());
    ASSERT_EQ(network.Pieces().size(), 1u);
    EXPECT_NEAR(network.Pieces()[0].s1 - network.Pieces()[0].s0, 500.0f, 0.01f);
}

TEST(RoadNetwork, TJunctionCreatesIntersectionAndPieces)
{
    auto data = FlatMap();
    data.nodes = {Node("a", -300, 0), Node("b", 0, 0), Node("c", 300, 0), Node("d", 0, 300)};
    data.nodes[1].mainRoads = {"main"};
    data.roads = {Road("main", {"a", "b", "c"}), Road("minor", {"b", "d"}, 2.75f)};
    const auto network = BuildNetwork(data);
    ASSERT_EQ(network.Intersections().size(), 1u);
    const auto& inter = network.Intersections()[0];
    EXPECT_EQ(inter.approaches.size(), 3u);
    for (const auto& a : inter.approaches) {
        EXPECT_GE(a.setback, 3.25f + 1.0f);
        EXPECT_LE(a.setback, 30.0f);
        EXPECT_GE(a.piece, 0);
        const bool isMain = network.Roads()[static_cast<std::size_t>(a.road)].spec->id == "main";
        EXPECT_EQ(a.control, isMain ? Map::ApproachControl::Priority : Map::ApproachControl::Yield);
    }
    EXPECT_EQ(network.Pieces().size(), 3u);
    EXPECT_TRUE(Map::PointInPolygon(Vector2(0.0f, 0.0f), inter.patch));
    EXPECT_GT(Map::PolygonArea(inter.patch), 50.0f);
    float height = 1.0f;
    Sim::SurfaceType surface = Sim::SurfaceType::Grass;
    float edge = 0.0f;
    EXPECT_TRUE(network.RoadSurfaceAt(Vector2(0.0f, 0.0f), height, surface, edge));
    EXPECT_NEAR(height, 0.0f, 1e-3f);
    EXPECT_EQ(surface, Sim::SurfaceType::Asphalt);
    EXPECT_LT(edge, 0.0f);
    // A point well beside the roads is off the road system.
    EXPECT_FALSE(network.RoadSurfaceAt(Vector2(100.0f, 100.0f), height, surface, edge));
}

TEST(RoadNetwork, FilletSmoothsInteriorCorner)
{
    auto data = FlatMap();
    data.nodes = {Node("a", 0, 0), Node("b", 200, 0), Node("c", 200, 200)};
    data.roads = {Road("r", {"a", "b", "c"})};
    data.roads[0].cornerRadius = 40.0f;
    const auto network = BuildNetwork(data);
    const auto& road = network.Roads()[0];
    float maxCurv = 0.0f;
    float minDistToCorner = 1e9f;
    for (const auto& s : road.curve.Samples()) {
        maxCurv = std::max(maxCurv, std::fabs(s.curvature));
        minDistToCorner = std::min(minDistToCorner, Vector2::Distance(Vector2(s.position.X, s.position.Z), Vector2(200.0f, 0.0f)));
    }
    EXPECT_NEAR(maxCurv, 1.0f / 40.0f, 1e-3f);
    EXPECT_GT(minDistToCorner, 5.0f);      // the corner is cut
    EXPECT_LT(minDistToCorner, 20.0f);
    EXPECT_LT(road.curve.Length(), 400.0f);
    EXPECT_GT(road.curve.Length(), 360.0f);
    // The node's s lies on the arc, roughly in the middle of the road.
    EXPECT_NEAR(road.nodeS[1], road.curve.Length() * 0.5f, 5.0f);
    // Turning right (east then south): positive curvature on the arc.
    float signedSum = 0.0f;
    for (const auto& s : road.curve.Samples()) signedSum += s.curvature;
    EXPECT_GT(signedSum, 0.0f);
}

TEST(RoadNetwork, HeightsFollowTerrainAndPinNodes)
{
    auto data = FlatMap();
    data.nodes = {Node("a", -400, 0), Node("b", 400, 0)};
    data.roads = {Road("r", {"a", "b"})};
    const auto slope = [](float x, float) { return x * 0.03f + 2.0f * std::sin(x * 0.05f); };
    const auto network = BuildNetwork(data, slope);
    const auto& road = network.Roads()[0];
    const auto& samples = road.curve.Samples();
    EXPECT_NEAR(samples.front().position.Y, network.NodePositions()[0].Y, 1e-3f);
    EXPECT_NEAR(samples.back().position.Y, network.NodePositions()[1].Y, 1e-3f);
    EXPECT_NEAR(network.NodePositions()[1].Y - network.NodePositions()[0].Y, 24.0f, 5.0f);   // slope plus the sampled ripple
    float maxGrade = 0.0f;
    for (std::size_t i = 1; i < samples.size(); ++i) {
        maxGrade = std::max(maxGrade, std::fabs(samples[i].position.Y - samples[i - 1].position.Y) / (samples[i].s - samples[i - 1].s));
    }
    EXPECT_LT(maxGrade, 0.06f);   // the 10 % ripples of the sine are filtered away
    EXPECT_GT(maxGrade, 0.02f);
}

TEST(RoadNetwork, CrownLowersTheEdges)
{
    auto data = FlatMap();
    data.nodes = {Node("a", 0, 0), Node("b", 0, -300)};
    data.roads = {Road("r", {"a", "b"})};
    const auto network = BuildNetwork(data);
    Map::RoadHit centre;
    ASSERT_TRUE(network.NearestRoad(Vector2(0.0f, -150.0f), 5.0f, centre));
    Map::RoadHit edge;
    ASSERT_TRUE(network.NearestRoad(Vector2(3.0f, -150.0f), 5.0f, edge));
    EXPECT_NEAR(centre.lateral, 0.0f, 1e-3f);
    EXPECT_NEAR(edge.lateral, 3.0f, 1e-3f);
    EXPECT_NEAR(network.SurfaceHeight(centre) - network.SurfaceHeight(edge), 0.06f, 1e-3f);
}

TEST(RoadNetwork, SpeedLimitDropsInsideTown)
{
    auto data = FlatMap();
    data.nodes = {Node("a", 0, 0), Node("b", 0, -300, true), Node("c", 0, -600, true), Node("d", 0, -900)};
    data.roads = {Road("r", {"a", "b", "c", "d"})};
    const auto network = BuildNetwork(data);
    const auto& road = network.Roads()[0];
    EXPECT_FLOAT_EQ(road.SpeedLimitAt(100.0f), 90.0f);
    EXPECT_FLOAT_EQ(road.SpeedLimitAt(450.0f), 50.0f);
    EXPECT_FLOAT_EQ(road.SpeedLimitAt(800.0f), 90.0f);
}
