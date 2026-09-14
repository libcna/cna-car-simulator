#include "CarSim/Map/MapDocument.hpp"
#include "CarSim/Map/MapWorld.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

using namespace CarSim;

namespace
{
    std::string LipovaDirectory() { return Map::MapDirectory(CARSIM_TEST_CONTENT_DIR, "lipova"); }
}

TEST(SampleMap, LipovaLoadsAndIsFullyConnected)
{
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    auto world = Map::MapWorld::Load(LipovaDirectory(), errors, &warnings);
    for (const auto& e : errors) {
        ADD_FAILURE() << e;
    }
    ASSERT_TRUE(world);
    EXPECT_EQ(world->Data().info.id, "lipova");
    const auto& roads = world->Roads();
    const auto& lanes = world->Lanes();
    EXPECT_GT(roads.TotalLength(), 14000.0f);
    EXPECT_LT(roads.TotalLength(), 24000.0f);
    EXPECT_GE(roads.Intersections().size(), 10u);
    for (const auto& inter : roads.Intersections()) {
        for (const auto& a : inter.approaches) {
            EXPECT_GE(a.piece, 0) << "approach without piece at node " << world->Data().nodes[static_cast<std::size_t>(inter.node)].id;
        }
    }
    ASSERT_FALSE(lanes.Lanes().empty());
    EXPECT_EQ(lanes.Reachable(0).size(), lanes.Lanes().size());
    for (const auto& lane : lanes.Lanes()) {
        EXPECT_FALSE(lane.outgoingLinks.empty()) << "lane " << lane.id << " dead-ends";
    }
    // Every spawn sits on a lane.
    ASSERT_FALSE(world->Data().traffic.playerSpawns.empty());
    for (const auto& spawn : world->Data().traffic.playerSpawns) {
        const int lane = lanes.NearestLane(spawn.position, spawn.headingDeg * 3.14159265f / 180.0f, 4.0f);
        EXPECT_GE(lane, 0) << "spawn " << spawn.name;
    }
    // Build time budget for the sample map (plan section 17).
    EXPECT_LT(world->Stats().loadSeconds, 6.0);
}

TEST(SampleMap, TownIsUrbanAndCountrysideIsNot)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(LipovaDirectory(), errors);
    ASSERT_TRUE(world);
    const auto* main = world->Data().FindRoad("main");
    ASSERT_NE(main, nullptr);
    const auto& roads = world->Roads();
    const Map::Road* road = nullptr;
    for (const auto& r : roads.Roads()) {
        if (r.spec == main) road = &r;
    }
    ASSERT_NE(road, nullptr);
    // The square is inside the built-up area; the eastern approach is not.
    Map::RoadHit hit;
    ASSERT_TRUE(roads.NearestRoad(Microsoft::Xna::Framework::Vector2(-100.0f, 3.0f), 10.0f, hit));
    EXPECT_FLOAT_EQ(road->SpeedLimitAt(hit.s), 50.0f);
    ASSERT_TRUE(roads.NearestRoad(Microsoft::Xna::Framework::Vector2(1000.0f, -210.0f), 40.0f, hit));
    EXPECT_FLOAT_EQ(road->SpeedLimitAt(hit.s), 90.0f);
}

TEST(SampleMap, RightOfWayIsAntisymmetric)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(LipovaDirectory(), errors);
    ASSERT_TRUE(world);
    const auto& lanes = world->Lanes();
    int yields = 0;
    for (const auto& link : lanes.Links()) {
        for (const int other : link.yieldTo) {
            ++yields;
            const auto& m = lanes.LinkAt(other);
            EXPECT_TRUE(std::find(m.yieldTo.begin(), m.yieldTo.end(), link.id) == m.yieldTo.end())
                << "links " << link.id << " and " << other << " yield to each other";
            EXPECT_TRUE(std::find(link.conflicts.begin(), link.conflicts.end(), other) != link.conflicts.end());
        }
    }
    EXPECT_GT(yields, 20);
}
