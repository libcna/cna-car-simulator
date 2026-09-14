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

TEST(SampleMap, PlotsAndUtilityPolesAreGeneratedClearOfBuildingsAndRoads)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(LipovaDirectory(), errors);
    ASSERT_TRUE(world);
    int fences = 0, hedges = 0, wire = 0, sheds = 0, poles = 0;
    for (const auto& p : world->Objects().Props()) {
        switch (p.type) {
            case Map::PropType::Fence: ++fences; break;
            case Map::PropType::Hedge: ++hedges; break;
            case Map::PropType::WireFence: ++wire; break;
            case Map::PropType::Shed: ++sheds; break;
            case Map::PropType::UtilityPole: ++poles; break;
            default: break;
        }
        if (p.type == Map::PropType::Shed || p.type == Map::PropType::UtilityPole) {
            const Microsoft::Xna::Framework::Vector2 at(p.position.X, p.position.Z);
            EXPECT_FALSE(world->Objects().InsideBuilding(at, 0.5f));
            float height = 0.0f;
            Sim::SurfaceType surface = Sim::SurfaceType::Asphalt;
            float edge = 0.0f;
            const bool onRoad = world->Roads().RoadSurfaceAt(at, height, surface, edge) && edge < 0.0f;
            EXPECT_FALSE(onRoad) << "prop on the paved road at " << at.X << ", " << at.Y;
        }
    }
    EXPECT_GT(fences + hedges + wire, 60) << "houses should have street-side fences";
    EXPECT_GT(hedges, 5);
    EXPECT_GT(wire, 5);
    EXPECT_GT(sheds, 10);
    EXPECT_GT(poles, 40);
}

TEST(SampleMap, BushesLineRuralVergesAndForestEdges)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(LipovaDirectory(), errors);
    ASSERT_TRUE(world);
    int bushes = 0;
    for (const auto& t : world->Objects().Trees()) {
        if (t.species != Map::TreeSpecies::Bush) continue;
        ++bushes;
        EXPECT_LT(t.Height(), 4.0f);
        const Microsoft::Xna::Framework::Vector2 at(t.position.X, t.position.Z);
        EXPECT_FALSE(world->Objects().InsideBuilding(at, 1.0f));
        float height = 0.0f;
        Sim::SurfaceType surface = Sim::SurfaceType::Asphalt;
        float edge = 0.0f;
        const bool onPaved = world->Roads().RoadSurfaceAt(at, height, surface, edge) && edge < 0.0f;
        EXPECT_FALSE(onPaved);
    }
    EXPECT_GT(bushes, 300);
}

TEST(SampleMap, TheSquareIsPavedAndLinedWithTownHouses)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(Map::MapDirectory(CARSIM_TEST_CONTENT_DIR, "lipova"), errors);
    ASSERT_TRUE(world);
    // The naměstí is a paved region: the ground there drives and sounds like cobbles.
    const Map::RegionSpec* square = nullptr;
    for (const auto& region : world->Data().terrain.regions) {
        if (region.type == Map::RegionType::Square) square = &region;
    }
    ASSERT_NE(square, nullptr) << "the sample map has a paved square";
    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (const auto& p : square->polygon) {
        minX = std::min(minX, p.X); maxX = std::max(maxX, p.X);
        minZ = std::min(minZ, p.Y); maxZ = std::max(maxZ, p.Y);
    }
    EXPECT_GT((maxX - minX) * (maxZ - minZ), 2000.0f) << "a square worth the name";
    const float centreX = 0.5f * (minX + maxX);
    const float centreZ = 0.5f * (minZ + maxZ);
    EXPECT_EQ(world->Terrain().RegionAt(centreX, centreZ), Map::RegionType::Square);
    EXPECT_EQ(world->Ground().Sample(centreX, centreZ).surface, Sim::SurfaceType::Cobbles);
    EXPECT_EQ(world->Ground().Sample(minX - 40.0f, centreZ).surface, Sim::SurfaceType::Grass) << "outside the square it is grass again";

    // Buildings line at least three sides, and one of them is the church.
    int west = 0, east = 0, north = 0, church = 0;
    for (const auto& b : world->Objects().Buildings()) {
        const float x = b.position.X;
        const float z = b.position.Z;
        if (z < minZ - 14.0f || z > maxZ + 14.0f) continue;
        if (x > minX - 12.0f && x < minX + 2.0f) ++west;
        if (x > maxX - 2.0f && x < maxX + 12.0f) ++east;
        if (z < minZ + 2.0f && x > minX - 6.0f && x < maxX + 6.0f) ++north;
        if (b.spec && b.spec->type == "church" && x > minX && x < maxX && z > minZ && z < maxZ) ++church;
    }
    EXPECT_GE(west, 3) << "town houses along the west side";
    EXPECT_GE(east, 3) << "town houses along the east side";
    EXPECT_GE(north, 2) << "town houses along the north side";
    EXPECT_EQ(church, 1) << "the church stands on the square";
}
