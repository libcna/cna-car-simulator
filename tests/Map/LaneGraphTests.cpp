#include "MapTestUtil.hpp"

#include "CarSim/Map/LaneGraph.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>

using namespace CarSim;
using namespace CarSim::Test;
using Microsoft::Xna::Framework::Vector2;

namespace
{
    bool Contains(const std::vector<int>& v, const int id) { return std::find(v.begin(), v.end(), id) != v.end(); }

    Map::MapData CrossRoads(const bool mainPriority)
    {
        auto data = FlatMap();
        data.nodes = {Node("w", -300, 0), Node("c", 0, 0), Node("e", 300, 0), Node("n", 0, -300), Node("s", 0, 300)};
        if (mainPriority) {
            data.nodes[1].mainRoads = {"main"};
        }
        data.roads = {Road("main", {"w", "c", "e"}), Road("minor", {"n", "c", "s"})};
        return data;
    }
}

TEST(LaneGraph, FourWayIntersectionHasAllTurns)
{
    auto world = BuildWorld(CrossRoads(true));
    ASSERT_TRUE(world);
    const auto& lanes = world->Lanes();
    const auto& roads = world->Roads();
    ASSERT_EQ(roads.Pieces().size(), 4u);
    EXPECT_EQ(lanes.Lanes().size(), 8u);
    // 4 incoming lanes x 3 turns + 4 dead-end U-turns.
    EXPECT_EQ(lanes.Links().size(), 16u);
    // Eastbound main lane on the west piece ends at the intersection and has L/S/R.
    const int eastIn = FindLane(lanes, roads, "main", true, 0);
    ASSERT_GE(eastIn, 0);
    EXPECT_EQ(lanes.LaneAt(eastIn).toIntersection, 0);
    EXPECT_NE(FindLink(lanes, eastIn, Map::TurnType::Straight), nullptr);
    EXPECT_NE(FindLink(lanes, eastIn, Map::TurnType::Left), nullptr);
    EXPECT_NE(FindLink(lanes, eastIn, Map::TurnType::Right), nullptr);
    EXPECT_EQ(FindLink(lanes, eastIn, Map::TurnType::UTurn), nullptr);
    // Lanes keep the road's speed limit and are on the right-hand side of the centreline.
    const auto& lane = lanes.LaneAt(eastIn);
    EXPECT_FLOAT_EQ(lane.points.front().speedLimitKmh, 90.0f);
    EXPECT_GT(lane.lateralOffset, 0.0f);
    EXPECT_NEAR(lane.points.front().position.Z, 1.5f, 1e-3f);   // +s is east; right is south (+z)
}

TEST(LaneGraph, MinorRoadYieldsToMainRoad)
{
    auto world = BuildWorld(CrossRoads(true));
    ASSERT_TRUE(world);
    const auto& lanes = world->Lanes();
    const auto& roads = world->Roads();
    const int eastIn = FindLane(lanes, roads, "main", true, 0);
    const int southIn = FindLane(lanes, roads, "minor", true, 0);   // north piece, travelling south towards the centre
    ASSERT_GE(eastIn, 0);
    ASSERT_GE(southIn, 0);
    ASSERT_EQ(lanes.LaneAt(southIn).toIntersection, 0);
    const auto* mainStraight = FindLink(lanes, eastIn, Map::TurnType::Straight);
    const auto* minorStraight = FindLink(lanes, southIn, Map::TurnType::Straight);
    ASSERT_NE(mainStraight, nullptr);
    ASSERT_NE(minorStraight, nullptr);
    EXPECT_TRUE(mainStraight->priority);
    EXPECT_FALSE(minorStraight->priority);
    EXPECT_EQ(minorStraight->control, Map::ApproachControl::Yield);
    EXPECT_TRUE(Contains(minorStraight->conflicts, mainStraight->id));
    EXPECT_TRUE(Contains(minorStraight->yieldTo, mainStraight->id));
    EXPECT_FALSE(Contains(mainStraight->yieldTo, minorStraight->id));
    // Left turn from the main road gives way to oncoming main traffic.
    const int westIn = FindLane(lanes, roads, "main", false, 1);   // east piece travelling west
    ASSERT_GE(westIn, 0);
    const auto* mainLeft = FindLink(lanes, eastIn, Map::TurnType::Left);
    const auto* oncomingStraight = FindLink(lanes, westIn, Map::TurnType::Straight);
    ASSERT_NE(mainLeft, nullptr);
    ASSERT_NE(oncomingStraight, nullptr);
    EXPECT_TRUE(Contains(mainLeft->yieldTo, oncomingStraight->id));
    EXPECT_FALSE(Contains(oncomingStraight->yieldTo, mainLeft->id));
}

TEST(LaneGraph, RightHandRuleWithoutPriority)
{
    auto world = BuildWorld(CrossRoads(false));
    ASSERT_TRUE(world);
    const auto& lanes = world->Lanes();
    const auto& roads = world->Roads();
    const int eastIn = FindLane(lanes, roads, "main", true, 0);     // travelling east
    const int northIn = FindLane(lanes, roads, "minor", false, 1);  // south piece travelling north
    ASSERT_GE(eastIn, 0);
    ASSERT_GE(northIn, 0);
    const auto* eastStraight = FindLink(lanes, eastIn, Map::TurnType::Straight);
    const auto* northStraight = FindLink(lanes, northIn, Map::TurnType::Straight);
    ASSERT_NE(eastStraight, nullptr);
    ASSERT_NE(northStraight, nullptr);
    EXPECT_EQ(eastStraight->control, Map::ApproachControl::RightHandRule);
    // Northbound traffic comes from the eastbound driver's right.
    EXPECT_TRUE(Contains(eastStraight->yieldTo, northStraight->id));
    EXPECT_FALSE(Contains(northStraight->yieldTo, eastStraight->id));
}

TEST(LaneGraph, RoutesAroundALoop)
{
    auto data = FlatMap();
    data.nodes = {Node("a", -400, 0), Node("b", 400, 0), Node("c", 0, -500)};
    data.roads = {Road("ab", {"a", "b"}), Road("bc", {"b", "c"}), Road("ca", {"c", "a"})};
    auto world = BuildWorld(data);
    ASSERT_TRUE(world);
    const auto& lanes = world->Lanes();
    EXPECT_EQ(world->Roads().Intersections().size(), 3u);
    EXPECT_EQ(lanes.Lanes().size(), 6u);
    // Without junctions there is no legal way to reverse: each direction forms its own cycle.
    const auto reachable = lanes.Reachable(0);
    EXPECT_EQ(reachable.size(), 3u);
    for (const int id : reachable) {
        EXPECT_EQ(lanes.LaneAt(id).forward, lanes.LaneAt(0).forward);
    }
    const int from = FindLane(lanes, world->Roads(), "ab", true);
    const int to = FindLane(lanes, world->Roads(), "ca", true);
    const auto route = lanes.FindRoute(from, to);
    ASSERT_EQ(route.size(), 3u);   // ab -> bc -> ca
    EXPECT_EQ(route.front().lane, from);
    EXPECT_EQ(route.back().lane, to);
    EXPECT_GE(route[0].link, 0);
    EXPECT_EQ(route.back().link, -1);
    std::mt19937 rng(1);
    EXPECT_GE(lanes.RandomLink(from, rng), 0);
}

TEST(LaneGraph, NearestLanePrefersMatchingHeading)
{
    auto data = FlatMap();
    data.nodes = {Node("a", -300, 0), Node("b", 300, 0)};
    data.roads = {Road("r", {"a", "b"})};
    auto world = BuildWorld(data);
    ASSERT_TRUE(world);
    const auto& lanes = world->Lanes();
    float s = 0.0f;
    float lateral = 0.0f;
    const float east = 3.14159265f * 0.5f;
    const int eastLane = lanes.NearestLane(Vector2(0.0f, 0.0f), east, 6.0f, &s, &lateral);
    const int westLane = lanes.NearestLane(Vector2(0.0f, 0.0f), -east, 6.0f);
    ASSERT_GE(eastLane, 0);
    ASSERT_GE(westLane, 0);
    EXPECT_NE(eastLane, westLane);
    EXPECT_TRUE(lanes.LaneAt(eastLane).forward);
    EXPECT_FALSE(lanes.LaneAt(westLane).forward);
    EXPECT_NEAR(s, 300.0f, 1.0f);
    EXPECT_NEAR(lateral, -1.5f, 1e-2f);   // the centreline point is left of the eastbound lane centre
    EXPECT_EQ(lanes.LaneAt(eastLane).oppositeLane, westLane);
}
