#include "CarSim/Map/MapDocument.hpp"
#include "CarSim/Map/MapWorld.hpp"
#include "CarSim/Traffic/Pedestrians.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <iostream>

using namespace CarSim;
using Microsoft::Xna::Framework::Vector3;

namespace
{
    std::unique_ptr<Map::MapWorld> Lipova()
    {
        std::vector<std::string> errors;
        return Map::MapWorld::Load(Map::MapDirectory(CARSIM_TEST_CONTENT_DIR, "lipova"), errors);
    }

    /// A walkway with a zebra crossing on it, and that crossing.
    bool WalkwayWithCrossing(const Traffic::Pedestrians& people, int& walkway, int& crossing)
    {
        for (std::size_t c = 0; c < people.Crossings().size(); ++c) {
            const auto& x = people.Crossings()[c];
            for (std::size_t w = 0; w < people.Walkways().size(); ++w) {
                const auto& way = people.Walkways()[w];
                if (way.road == x.road && x.s >= way.s0 && x.s <= way.s1) {
                    walkway = static_cast<int>(w);
                    crossing = static_cast<int>(c);
                    return true;
                }
            }
        }
        return false;
    }
}

TEST(Pedestrians, TheTownHasPavementsAndCrossingsAndPeopleOnThem)
{
    auto world = Lipova();
    ASSERT_TRUE(world);
    Traffic::Pedestrians people(*world, 3);
    EXPECT_GT(people.Walkways().size(), 5u);
    EXPECT_GT(people.Crossings().size(), 0u);
    const auto& way = people.Walkways().front();
    const Vector3 focus = world->Roads().Roads()[static_cast<std::size_t>(way.road)].curve.Evaluate(way.s0).position;
    for (int i = 0; i < 300; ++i) people.Update(1.0f / 10.0f, focus, {}, {}, 30);
    EXPECT_GE(people.People().size(), 10u);
    for (const auto& p : people.People()) {
        if (p.crossing == -1) {
            EXPECT_FALSE(p.onRoad) << "people walk on the pavement";
        }
    }
}

TEST(Pedestrians, APersonCrossesAtTheZebraWhenTheRoadIsClear)
{
    auto world = Lipova();
    ASSERT_TRUE(world);
    Traffic::Pedestrians people(*world, 5);
    int walkway = -1, crossing = -1;
    ASSERT_TRUE(WalkwayWithCrossing(people, walkway, crossing));
    const auto& x = people.Crossings()[static_cast<std::size_t>(crossing)];
    const int id = people.Spawn(walkway, x.s - 3.0f, 1.0f, true);
    ASSERT_GE(id, 0);
    const float startSide = people.Walkways()[static_cast<std::size_t>(walkway)].lateral;
    bool wasOnRoad = false, reachedOtherSide = false;
    for (int i = 0; i < 300; ++i) {
        people.Update(1.0f / 10.0f, people.People().front().position, {}, {}, 1);
        for (const auto& p : people.People()) {
            if (p.id != id) continue;
            wasOnRoad = wasOnRoad || p.onRoad;
            // Back on a pavement, on the far side.
            reachedOtherSide = reachedOtherSide || (p.crossing == -1 && (p.lateral > 0.0f) != (startSide > 0.0f));
        }
    }
    EXPECT_TRUE(wasOnRoad);
    EXPECT_TRUE(reachedOtherSide);
}

TEST(Pedestrians, APersonWaitsAtTheKerbForAnApproachingCar)
{
    auto world = Lipova();
    ASSERT_TRUE(world);
    Traffic::Pedestrians people(*world, 5);
    int walkway = -1, crossing = -1;
    ASSERT_TRUE(WalkwayWithCrossing(people, walkway, crossing));
    const auto& x = people.Crossings()[static_cast<std::size_t>(crossing)];
    const int id = people.Spawn(walkway, x.s - 1.0f, 1.0f, true);
    ASSERT_GE(id, 0);
    // A car bearing down at 50 km/h from 40 m away, kept there.
    const Vector3 at = world->Roads().Roads()[static_cast<std::size_t>(x.road)].curve.Evaluate(x.s).position;
    Traffic::TrafficVehicle car;
    car.position = at + Vector3(40.0f, 0.0f, 0.0f);
    car.speed = 14.0f;
    for (int i = 0; i < 50; ++i) people.Update(1.0f / 10.0f, at, {car}, {}, 1);
    const auto& p = people.People().front();
    EXPECT_EQ(p.crossing, crossing) << "at the crossing";
    EXPECT_FALSE(p.onRoad) << "still waiting at the kerb";
    EXPECT_FALSE(people.RoadProbes().empty()) << "traffic sees the person at the kerb";
}
