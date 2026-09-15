// Fixed-time signals: the phase sequence, and traffic stopping for red and going on green.
#include "CarSim/Map/MapData.hpp"
#include "CarSim/Map/MapWorld.hpp"
#include "CarSim/Traffic/SignalController.hpp"
#include "CarSim/Traffic/TrafficSystem.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using namespace CarSim;
using Microsoft::Xna::Framework::Vector2;

namespace
{
    Map::SignalPlan TwoGroupPlan()
    {
        Map::SignalPlan plan;
        plan.enabled = true;
        plan.greenSeconds = 20.0f;
        plan.amberSeconds = 3.0f;
        plan.allRedSeconds = 2.0f;
        plan.groups = {{"main"}, {"minor"}};
        return plan;
    }

    Map::RoadNodeSpec Node(const std::string& id, const float x, const float z)
    {
        Map::RoadNodeSpec n;
        n.id = id;
        n.position = Vector2(x, z);
        return n;
    }

    Map::RoadSpec Road(const std::string& id, std::vector<std::string> nodes)
    {
        Map::RoadSpec r;
        r.id = id;
        r.nodes = std::move(nodes);
        r.laneWidth = 3.0f;
        r.speedLimitKmh = 90.0f;
        return r;
    }

    /// A crossroads where the lights hold the minor road while the main road runs.
    std::unique_ptr<Map::MapWorld> SignalledCross()
    {
        Map::MapData data;
        data.info.id = "signals";
        data.terrain.sizeX = data.terrain.sizeZ = 2000.0f;
        data.terrain.cellSize = 10.0f;
        data.terrain.noiseAmplitude = 0.0f;
        data.nodes = {Node("w", -400, 0), Node("c", 0, 0), Node("e", 400, 0), Node("n", 0, -400), Node("s", 0, 400)};
        data.nodes[1].mainRoads = {"main"};
        data.nodes[1].signals = TwoGroupPlan();
        data.roads = {Road("main", {"w", "c", "e"}), Road("minor", {"n", "c", "s"})};
        data.traffic.maxVehicles = 0;
        std::vector<std::string> errors;
        auto world = Map::MapWorld::Build(std::move(data), errors);
        EXPECT_TRUE(errors.empty());
        return world;
    }

    int LaneOf(const Map::MapWorld& w, const std::string& road, const bool forward, const int ordinal)
    {
        int n = 0;
        for (const auto& lane : w.Lanes().Lanes()) {
            if (w.Roads().Roads()[static_cast<std::size_t>(lane.road)].spec->id == road && lane.forward == forward) {
                if (n++ == ordinal) return lane.id;
            }
        }
        return -1;
    }
}

TEST(Signals, OneGroupIsGreenAtATimeAndTheCycleRepeats)
{
    const Map::SignalPlan plan = TwoGroupPlan();
    EXPECT_FLOAT_EQ(Traffic::CycleSeconds(plan), 50.0f);   // (20 + 3 + 2) x 2

    // Group 0: green, amber, then red for the rest of the cycle.
    EXPECT_EQ(Traffic::AspectAt(plan, 0, 0.0f), Traffic::SignalAspect::Green);
    EXPECT_EQ(Traffic::AspectAt(plan, 0, 19.9f), Traffic::SignalAspect::Green);
    EXPECT_EQ(Traffic::AspectAt(plan, 0, 21.0f), Traffic::SignalAspect::Amber);
    EXPECT_EQ(Traffic::AspectAt(plan, 0, 24.0f), Traffic::SignalAspect::Red);
    EXPECT_EQ(Traffic::AspectAt(plan, 0, 40.0f), Traffic::SignalAspect::Red);
    // Group 1 takes over after the all-red.
    EXPECT_EQ(Traffic::AspectAt(plan, 1, 0.0f), Traffic::SignalAspect::Red);
    EXPECT_EQ(Traffic::AspectAt(plan, 1, 26.0f), Traffic::SignalAspect::Green);
    EXPECT_EQ(Traffic::AspectAt(plan, 1, 46.0f), Traffic::SignalAspect::Amber);
    // Red and amber together just before a group's green.
    EXPECT_EQ(Traffic::AspectAt(plan, 0, 49.5f), Traffic::SignalAspect::RedAmber);
    // Never two greens at once, anywhere in the cycle.
    for (float t = 0.0f; t < 200.0f; t += 0.25f) {
        int greens = 0;
        for (int g = 0; g < 2; ++g) {
            const auto a = Traffic::AspectAt(plan, g, t);
            if (a == Traffic::SignalAspect::Green || a == Traffic::SignalAspect::Amber) ++greens;
        }
        ASSERT_LE(greens, 1) << "two groups may cross at t = " << t;
    }
    // The cycle repeats exactly.
    for (float t = 0.0f; t < 50.0f; t += 0.5f) {
        EXPECT_EQ(Traffic::AspectAt(plan, 0, t), Traffic::AspectAt(plan, 0, t + 50.0f));
    }
}

TEST(Signals, APlanThatIsOffOrHasOneGroupShowsGreen)
{
    Map::SignalPlan off = TwoGroupPlan();
    off.enabled = false;
    EXPECT_EQ(Traffic::AspectAt(off, 0, 30.0f), Traffic::SignalAspect::Green);
    EXPECT_FLOAT_EQ(Traffic::CycleSeconds(off), 0.0f);

    Map::SignalPlan single = TwoGroupPlan();
    single.groups = {{"main"}};
    EXPECT_EQ(Traffic::AspectAt(single, 0, 30.0f), Traffic::SignalAspect::Green);
    // An approach with no group at all is not signalised.
    EXPECT_EQ(Traffic::AspectAt(TwoGroupPlan(), -1, 5.0f), Traffic::SignalAspect::Green);
}

TEST(Signals, TheJunctionIsSignalisedAndEveryApproachHasAGroup)
{
    auto world = SignalledCross();
    ASSERT_TRUE(world);
    const auto& intersections = world->Roads().Intersections();
    ASSERT_EQ(intersections.size(), 1u);
    const auto& inter = intersections.front();
    EXPECT_TRUE(inter.signals.enabled);
    ASSERT_EQ(inter.signals.groups.size(), 2u);
    for (const auto& approach : inter.approaches) {
        EXPECT_EQ(approach.control, Map::ApproachControl::Signal);
        EXPECT_GE(approach.signalGroup, 0);
    }
    // One mast per approach, each facing the traffic coming towards the junction.
    EXPECT_EQ(world->Objects().Signals().size(), inter.approaches.size());
    // Every connector through the junction inherits the control and the group of its approach.
    int signalledLinks = 0;
    for (const auto& link : world->Lanes().Links()) {
        if (link.intersection < 0) continue;
        EXPECT_EQ(link.control, Map::ApproachControl::Signal);
        EXPECT_GE(link.signalGroup, 0);
        ++signalledLinks;
    }
    EXPECT_GT(signalledLinks, 0);
}

TEST(Signals, TrafficStopsForRedAndGoesOnGreen)
{
    auto world = SignalledCross();
    ASSERT_TRUE(world);
    Traffic::TrafficSystem traffic(*world, 11);
    const int minorLane = LaneOf(*world, "minor", true, 0);   // southbound towards the junction
    ASSERT_GE(minorLane, 0);
    const auto& lanes = world->Lanes();
    // The minor road is group 1, so it starts on red: a car arriving now must stop and wait.
    traffic.SpawnOn(minorLane, lanes.LaneAt(minorLane).length - 70.0f, 13.0f);
    ASSERT_EQ(traffic.Vehicles().size(), 1u);

    bool waited = false;
    bool crossedWhileRed = false;
    float t = 0.0f;
    for (int i = 0; i < 60 * 45; ++i) {
        traffic.Update(1.0f / 60.0f, Traffic::PlayerProbe{});
        t += 1.0f / 60.0f;
        if (traffic.Vehicles().empty()) break;
        const auto& v = traffic.Vehicles().front();
        const auto aspect = traffic.AspectOf(0, 1);
        if (v.waiting && v.speed < 0.5f) waited = true;
        if (v.link >= 0 && aspect == Traffic::SignalAspect::Red && !waited) crossedWhileRed = true;
        if (waited && v.link >= 0) break;
    }
    EXPECT_TRUE(waited) << "the car should stop at the red light";
    EXPECT_FALSE(crossedWhileRed);
    ASSERT_FALSE(traffic.Vehicles().empty());
    // It is released once its group gets its green, which is 25 s into the cycle at the latest.
    EXPECT_GE(traffic.Signals().Seconds(), 25.0f);   // its green starts 25 s into the cycle
    EXPECT_TRUE(traffic.Vehicles().front().link >= 0 || traffic.Vehicles().front().lane != minorLane)
        << "the car should be through once the light turns green";
}
