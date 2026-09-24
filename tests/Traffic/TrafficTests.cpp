#include "CarSim/Collision/Shapes.hpp"
#include "CarSim/Map/MapData.hpp"
#include "CarSim/Map/MapDocument.hpp"
#include "CarSim/Map/MapWorld.hpp"
#include "CarSim/Traffic/TrafficSystem.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>

using namespace CarSim;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;

namespace
{
    Map::RoadNodeSpec Node(const std::string& id, float x, float z)
    {
        Map::RoadNodeSpec n;
        n.id = id;
        n.position = Vector2(x, z);
        return n;
    }

    Map::RoadSpec Road(const std::string& id, std::vector<std::string> nodes, float speed = 90.0f)
    {
        Map::RoadSpec r;
        r.id = id;
        r.nodes = std::move(nodes);
        r.laneWidth = 3.0f;
        r.speedLimitKmh = speed;
        return r;
    }

    std::unique_ptr<Map::MapWorld> CrossWorld(bool priority, const std::vector<Map::PropSpec>& props = {})
    {
        Map::MapData data;
        data.info.id = "cross";
        data.terrain.sizeX = data.terrain.sizeZ = 2000.0f;
        data.terrain.cellSize = 10.0f;
        data.terrain.noiseAmplitude = 0.0f;
        data.nodes = {Node("w", -400, 0), Node("c", 0, 0), Node("e", 400, 0), Node("n", 0, -400), Node("s", 0, 400)};
        if (priority) data.nodes[1].mainRoads = {"main"};
        data.roads = {Road("main", {"w", "c", "e"}), Road("minor", {"n", "c", "s"})};
        data.traffic.maxVehicles = 0;
        data.objects.props = props;
        std::vector<std::string> errors;
        auto world = Map::MapWorld::Build(std::move(data), errors);
        EXPECT_TRUE(errors.empty());
        return world;
    }

    int LaneOf(const Map::MapWorld& w, const std::string& road, bool forward, int ordinal)
    {
        int n = 0;
        for (const auto& lane : w.Lanes().Lanes()) {
            if (w.Roads().Roads()[static_cast<std::size_t>(lane.road)].spec->id == road && lane.forward == forward) {
                if (n++ == ordinal) return lane.id;
            }
        }
        return -1;
    }

    Traffic::PlayerProbe NoPlayer() { return {}; }
}

TEST(TrafficIdm, FreeRoadAcceleratesAndFollowerKeepsDistance)
{
    Traffic::TrafficParams p;
    EXPECT_GT(Traffic::TrafficSystem::IdmAcceleration(0.0f, 20.0f, 1e9f, 0.0f, p), 1.0f);
    EXPECT_NEAR(Traffic::TrafficSystem::IdmAcceleration(20.0f, 20.0f, 1e9f, 0.0f, p), 0.0f, 1e-3f);
    // Close behind a slower leader: strong braking.
    EXPECT_LT(Traffic::TrafficSystem::IdmAcceleration(20.0f, 25.0f, 8.0f, 10.0f, p), -3.0f);
}

TEST(TrafficSystem, FollowerNeverHitsLeader)
{
    auto world = CrossWorld(true);
    ASSERT_TRUE(world);
    Traffic::TrafficSystem traffic(*world, 1);
    const int lane = LaneOf(*world, "main", true, 0);
    ASSERT_GE(lane, 0);
    const int leader = traffic.SpawnOn(lane, 120.0f, 8.0f);
    const int follower = traffic.SpawnOn(lane, 60.0f, 25.0f);
    ASSERT_GE(leader, 0);
    ASSERT_GE(follower, 0);
    float minGap = 1e9f;
    bool matchedSpeed = false;
    for (int i = 0; i < 60 * 20; ++i) {
        traffic.Update(1.0f / 60.0f, NoPlayer());
        const auto& vs = traffic.Vehicles();
        const auto* l = &vs[0];
        const auto* f = &vs[1];
        if (l->lane == f->lane && l->link < 0 && f->link < 0) {
            const float gap = l->s - f->s - 4.2f;
            minGap = std::min(minGap, gap);
            // Once caught up, the follower holds a time gap and matches the leader's speed.
            if (gap < 45.0f && std::fabs(f->speed - l->speed) < 3.0f && f->speed > 3.0f) {
                matchedSpeed = true;
            }
        }
    }
    EXPECT_GT(minGap, 1.0f);
    EXPECT_TRUE(matchedSpeed);
}

TEST(TrafficSystem, CarsBrakeForWalkerAndContinueAfterTheRoadClears)
{
    auto world = CrossWorld(true);
    ASSERT_TRUE(world);
    Traffic::TrafficSystem traffic(*world, 42);
    traffic.SetDensity(0);
    const int lane = LaneOf(*world, "main", true, 0);
    ASSERT_GE(lane, 0);
    ASSERT_GE(traffic.SpawnOn(lane, 60.0f, 20.0f), 0);

    Traffic::PlayerProbe walker;
    walker.valid = true;
    walker.lengthM = 0.5f;
    walker.position = world->Lanes().LaneAt(lane).Evaluate(120.0f).position;
    bool stopped = false;
    float nearestGap = 1e9f;
    for (int i = 0; i < 60 * 12; ++i) {
        traffic.Update(1.0f / 60.0f, NoPlayer(), walker);
        const auto& car = traffic.Vehicles().front();
        nearestGap = std::min(nearestGap, 120.0f - car.s - car.lengthM * 0.5f - walker.lengthM * 0.5f);
        stopped = stopped || (car.speed < 0.2f && car.s > 100.0f);
    }
    EXPECT_TRUE(stopped);
    EXPECT_GT(nearestGap, 0.1f) << "traffic must not pass through the pedestrian";
    const float stoppedAt = traffic.Vehicles().front().s;

    const auto direction = world->Lanes().LaneAt(lane).Evaluate(120.0f).tangent;
    walker.position += Vector3(-direction.Z, 0.0f, direction.X) * 8.0f;
    for (int i = 0; i < 60 * 5; ++i) traffic.Update(1.0f / 60.0f, NoPlayer(), walker);
    EXPECT_GT(traffic.Vehicles().front().s, stoppedAt + 10.0f);
    EXPECT_GT(traffic.Vehicles().front().speed, 2.0f);
}

TEST(TrafficSystem, CarStopsForWalkerWhoStepsIntoItsImmediatePath)
{
    auto world = CrossWorld(true);
    ASSERT_TRUE(world);
    Traffic::TrafficSystem traffic(*world, 43);
    traffic.SetDensity(0);
    const int lane = LaneOf(*world, "main", true, 0);
    ASSERT_GE(lane, 0);
    ASSERT_GE(traffic.SpawnOn(lane, 60.0f, 20.0f), 0);

    Traffic::PlayerProbe walker;
    walker.valid = true;
    walker.lengthM = 0.5f;
    walker.position = world->Lanes().LaneAt(lane).Evaluate(67.0f).position;
    for (int i = 0; i < 60; ++i) {
        traffic.Update(1.0f / 60.0f, NoPlayer(), walker);
        const auto& car = traffic.Vehicles().front();
        const float gap = 67.0f - car.s - car.lengthM * 0.5f - walker.lengthM * 0.5f;
        EXPECT_GE(gap, 0.0f);
    }
    EXPECT_LT(traffic.Vehicles().front().speed, 0.2f);
}

TEST(TrafficSystem, CarsKeepDrivingBeneathAnAirborneHelicopter)
{
    auto world = CrossWorld(true);
    ASSERT_TRUE(world);
    Traffic::TrafficSystem traffic(*world, 44);
    traffic.SetDensity(0);
    const int lane = LaneOf(*world, "main", true, 0);
    ASSERT_GE(lane, 0);
    ASSERT_GE(traffic.SpawnOn(lane, 60.0f, 20.0f), 0);

    Traffic::PlayerProbe helicopter;
    helicopter.valid = true;
    helicopter.blocksTraffic = false;
    helicopter.position = world->Lanes().LaneAt(lane).Evaluate(120.0f).position + Vector3(0.0f, 3.0f, 0.0f);
    for (int i = 0; i < 60 * 8; ++i) traffic.Update(1.0f / 60.0f, helicopter);
    EXPECT_GT(traffic.Vehicles().front().s, 150.0f);
    EXPECT_GT(traffic.Vehicles().front().speed, 2.0f);
}

TEST(TrafficSystem, CarsProgressThroughIntersections)
{
    auto world = CrossWorld(true);
    ASSERT_TRUE(world);
    Traffic::TrafficSystem traffic(*world, 2);
    const int lane = LaneOf(*world, "main", true, 0);
    traffic.SpawnOn(lane, 300.0f, 15.0f);
    bool enteredLink = false;
    bool leftLane = false;
    for (int i = 0; i < 60 * 30; ++i) {
        traffic.Update(1.0f / 60.0f, NoPlayer());
        const auto& v = traffic.Vehicles()[0];
        enteredLink = enteredLink || v.link >= 0;
        leftLane = leftLane || (v.link < 0 && v.lane != lane);
    }
    EXPECT_TRUE(enteredLink);
    EXPECT_TRUE(leftLane);
    EXPECT_GT(traffic.Vehicles()[0].speed, 1.0f);
}

TEST(TrafficSystem, MinorRoadWaitsForMainRoadTraffic)
{
    auto world = CrossWorld(true);
    ASSERT_TRUE(world);
    Traffic::TrafficSystem traffic(*world, 3);
    const int mainLane = LaneOf(*world, "main", true, 0);      // eastbound towards the junction
    const int minorLane = LaneOf(*world, "minor", true, 0);    // southbound towards the junction
    const auto& lanes = world->Lanes();
    // Minor car close to the junction, main car arriving in ~3 s at 20 m/s.
    const int minor = traffic.SpawnOn(minorLane, lanes.LaneAt(minorLane).length - 15.0f, 5.0f);
    (void)minor;
    traffic.SpawnOn(mainLane, lanes.LaneAt(mainLane).length - 60.0f, 20.0f);
    bool minorWaited = false;
    bool minorCrossedBeforeMain = false;
    bool mainPassed = false;
    for (int i = 0; i < 60 * 15; ++i) {
        traffic.Update(1.0f / 60.0f, NoPlayer());
        const auto& vs = traffic.Vehicles();
        const auto& m = vs[0];
        const auto& main = vs[1];
        if (m.waiting) minorWaited = true;
        if (main.link >= 0 || (main.link < 0 && main.lane != mainLane)) mainPassed = true;
        if (m.link >= 0 && !mainPassed) minorCrossedBeforeMain = true;
    }
    EXPECT_TRUE(minorWaited);
    EXPECT_FALSE(minorCrossedBeforeMain);
    // Eventually the minor car crosses too.
    EXPECT_TRUE(traffic.Vehicles()[0].link >= 0 || traffic.Vehicles()[0].lane != minorLane);
}

TEST(TrafficSystem, SpawnsAroundThePlayerAndDespawnsFarAway)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(Map::MapDirectory(CARSIM_TEST_CONTENT_DIR, "lipova"), errors);
    ASSERT_TRUE(world);
    Traffic::TrafficSystem traffic(*world, 4);
    traffic.SetDensity(12);
    Traffic::PlayerProbe player;
    player.valid = true;
    const auto spawn = world->PlayerSpawn();
    player.position = world->SpawnPosition(spawn);
    player.forward = Vector3(1.0f, 0.0f, 0.0f);
    for (int i = 0; i < 60 * 30; ++i) {
        traffic.Update(1.0f / 60.0f, player);
    }
    EXPECT_GE(static_cast<int>(traffic.Vehicles().size()), 8);
    EXPECT_LE(static_cast<int>(traffic.Vehicles().size()), 12);
    for (const auto& v : traffic.Vehicles()) {
        // Cars may queue behind the parked player, but never overlap it.
        const float d = Vector3::Distance(v.position, player.position);
        EXPECT_GT(d, 5.5f);
        EXPECT_LT(d, world->Data().traffic.despawnDistance + 50.0f);
        EXPECT_TRUE(Traffic::PlateGenerator::IsValidStandard(v.plate) || Traffic::PlateGenerator::IsValidElectric(v.plate)) << v.plate;
    }
    // Teleport the player far away: cars despawn over time.
    player.position = Vector3(1800.0f, 0.0f, -2500.0f);
    for (int i = 0; i < 60 * 8; ++i) {
        traffic.Update(1.0f / 60.0f, player);
    }
    for (const auto& v : traffic.Vehicles()) {
        EXPECT_LT(Vector3::Distance(v.position, player.position), world->Data().traffic.despawnDistance + 50.0f);
    }
}

TEST(TrafficSystem, NewCarsAppearOutsideThePlayersViewConeAndWheelsSpinWithSpeed)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(Map::MapDirectory(CARSIM_TEST_CONTENT_DIR, "lipova"), errors);
    ASSERT_TRUE(world);
    Traffic::TrafficSystem traffic(*world, 11);
    traffic.SetDensity(16);
    Traffic::PlayerProbe player;
    player.valid = true;
    player.position = world->SpawnPosition(world->PlayerSpawn());
    player.forward = Vector3(1.0f, 0.0f, 0.0f);
    const float cosCone = std::cos(traffic.params.spawnViewAngleDeg * 3.14159265f / 180.0f);
    std::vector<int> seen;
    std::vector<std::pair<int, float>> previousSpin;
    int spawnsChecked = 0;
    int spinChecked = 0;
    for (int i = 0; i < 60 * 40; ++i) {
        traffic.Update(1.0f / 60.0f, player);
        for (const auto& v : traffic.Vehicles()) {
            const bool isNew = std::find(seen.begin(), seen.end(), v.id) == seen.end();
            if (isNew) {
                seen.push_back(v.id);
                Vector3 to = v.position - player.position;
                to.Y = 0.0f;
                const float d = to.Length();
                if (d > 1e-3f) {
                    to /= d;
                    const bool inCone = Vector3::Dot(to, player.forward) > cosCone;
                    EXPECT_FALSE(d < traffic.params.spawnViewDistance && inCone)
                        << "car " << v.id << " appeared " << d << " m ahead inside the view cone";
                }
                EXPECT_GE(d, world->Data().traffic.spawnMinDistance - 1.0f);
                ++spawnsChecked;
            }
            // Wheel spin integrates the travelled distance at the 0.31 m reference radius.
            auto it = std::find_if(previousSpin.begin(), previousSpin.end(), [&](const auto& p) { return p.first == v.id; });
            if (it != previousSpin.end()) {
                if (!v.backingOff) {   // reversing out of a blocked junction spins the wheels backwards
                    const float expected = v.speed * (1.0f / 60.0f) / 0.31f;
                    EXPECT_NEAR(v.wheelSpin - it->second, expected, 0.25f * expected + 1e-3f);
                    ++spinChecked;
                }
                it->second = v.wheelSpin;
            } else {
                previousSpin.emplace_back(v.id, v.wheelSpin);
            }
        }
    }
    EXPECT_GE(spawnsChecked, 12);
    EXPECT_GT(spinChecked, 1000);
}

TEST(TrafficSystem, BusesAndLorriesShareAJunctionWithCarsWithoutOverlap)
{
    // A bus and a lorry on the main road, cars from both sides of the minor road: the long
    // bodies sweep outside the car-sized conflict map, so the junction has to be kept clear
    // around them. Nobody may touch anybody, and everybody gets through.
    auto world = CrossWorld(true);
    ASSERT_TRUE(world);
    Traffic::TrafficSystem traffic(*world, 7);
    traffic.SetDensity(0);
    const int mainEast = LaneOf(*world, "main", true, 0);
    const int mainWest = LaneOf(*world, "main", false, 0);
    const int minorSouth = LaneOf(*world, "minor", true, 0);
    const int minorNorth = LaneOf(*world, "minor", false, 0);
    ASSERT_GE(mainEast, 0);
    ASSERT_GE(minorNorth, 0);
    const int bus = traffic.SpawnOn(mainEast, 330.0f, 12.0f, Sim::CarStyle::Body::Bus);
    const int lorry = traffic.SpawnOn(mainWest, 320.0f, 12.0f, Sim::CarStyle::Body::Truck);
    const int carA = traffic.SpawnOn(minorSouth, 350.0f, 8.0f, Sim::CarStyle::Body::Hatchback);
    const int carB = traffic.SpawnOn(minorNorth, 345.0f, 8.0f, Sim::CarStyle::Body::Sedan);
    ASSERT_GE(bus, 0);
    ASSERT_GE(lorry, 0);
    ASSERT_GE(carA, 0);
    ASSERT_GE(carB, 0);
    std::map<int, int> phase;   // 0 approaching, 1 inside the junction, 2 through

    const auto box = [](const Traffic::TrafficVehicle& c) {
        return Collision::Obb::FromHeading(c.position + Vector3(0.0f, 0.5f * c.heightM, 0.0f),
                                           Vector3(0.5f * c.widthM, 0.5f * c.heightM, 0.5f * c.lengthM), c.headingRad);
    };
    int overlaps = 0;
    for (int i = 0; i < 30 * 90; ++i) {
        traffic.Update(1.0f / 30.0f, NoPlayer());
        const auto& cars = traffic.Vehicles();
        for (const auto& c : cars) {
            int& p = phase[c.id];
            if (p == 0 && c.link >= 0) p = 1;
            if (p == 1 && c.link < 0) p = 2;
        }
        for (std::size_t a = 0; a < cars.size(); ++a) {
            for (std::size_t b = a + 1; b < cars.size(); ++b) {
                Collision::Contact contact;
                if (Collision::IntersectObbObb(box(cars[a]), box(cars[b]), contact)) ++overlaps;
            }
        }
    }
    EXPECT_EQ(overlaps, 0);
    for (const auto& v : traffic.Vehicles()) {
        EXPECT_EQ(phase[v.id], 2) << "vehicle " << v.id << " (" << Sim::CarStyle::ToString(v.body) << ") did not get through the junction";
    }
}

TEST(TrafficSystem, LongVehiclesAreSlowerAndCarryTheirBodyClass)
{
    auto world = CrossWorld(true);
    ASSERT_TRUE(world);
    Traffic::TrafficSystem traffic(*world, 3);
    traffic.SetDensity(0);
    const int lane = LaneOf(*world, "main", true, 0);
    const int bus = traffic.SpawnOn(lane, 10.0f, 0.0f, Sim::CarStyle::Body::Bus);
    ASSERT_GE(bus, 0);
    const auto& v = traffic.Vehicles().front();
    EXPECT_TRUE(v.Heavy());
    EXPECT_GT(v.lengthM, 11.0f);
    EXPECT_GT(v.massKg, 10000.0f);
    for (int i = 0; i < 30 * 60; ++i) traffic.Update(1.0f / 30.0f, NoPlayer());
    EXPECT_LE(traffic.Vehicles().front().speed, 80.0f / 3.6f + 0.1f) << "heavy vehicles keep to 80 km/h";
}

TEST(TrafficSystem, BusesCallAtTheStopsOnTheirLaneAndMoveOn)
{
    // A shelter on the right of the eastbound main road, 200 m before the junction.
    Map::PropSpec shelter;
    shelter.type = "bus_stop";
    shelter.position = Vector2(-200.0f, 5.5f);
    auto world = CrossWorld(true, {shelter});
    ASSERT_TRUE(world);
    Traffic::TrafficSystem traffic(*world, 5);
    traffic.SetDensity(0);
    int lane = -1;
    for (const auto& l : world->Lanes().Lanes()) {
        if (!traffic.BusStopsOn(l.id).empty()) lane = l.id;
    }
    ASSERT_GE(lane, 0) << "the shelter is assigned to a lane";
    const float stop = traffic.BusStopsOn(lane).front();
    ASSERT_GE(traffic.SpawnOn(lane, std::max(0.0f, stop - 120.0f), 12.0f, Sim::CarStyle::Body::Bus), 0);
    ASSERT_GE(traffic.SpawnOn(lane, std::max(0.0f, stop - 160.0f), 12.0f, Sim::CarStyle::Body::Hatchback), 0);
    bool dwelled = false, indicated = false;
    float stoppedAt = -1.0f;
    for (int i = 0; i < 30 * 60; ++i) {
        traffic.Update(1.0f / 30.0f, NoPlayer());
        for (const auto& v : traffic.Vehicles()) {
            if (v.body != Sim::CarStyle::Body::Bus) continue;
            indicated = indicated || v.indicatorRight;
            if (v.dwell > 0.0f && !dwelled) {
                dwelled = true;
                stoppedAt = v.s;
            }
        }
    }
    EXPECT_TRUE(indicated);
    ASSERT_TRUE(dwelled);
    EXPECT_NEAR(stoppedAt, stop, 1.5f) << "stops with its middle at the shelter";
    for (const auto& v : traffic.Vehicles()) {
        if (v.body == Sim::CarStyle::Body::Bus) EXPECT_TRUE(v.lane != lane || v.s > stop + 20.0f) << "and moves on";
    }
}

namespace
{
    struct OvertakeRun
    {
        bool overtook = false;
        bool wentOut = false;
        int overlaps = 0;
        int closeCalls = 0;   // frames out in the other lane with a car coming within 45 m
    };

    OvertakeRun RunOvertake(const bool oncoming)
    {
        auto world = CrossWorld(true);
        EXPECT_TRUE(world);
        Traffic::TrafficSystem traffic(*world, 11);
        traffic.SetDensity(0);
        const int east = LaneOf(*world, "main", true, 0);
        const int west = LaneOf(*world, "main", false, 0);
        // A lorry crawling at 30 km/h with a car behind it, 350 m of road ahead.
        const int lorry = traffic.SpawnOn(east, 60.0f, 8.3f, Sim::CarStyle::Body::Truck);
        const int car = traffic.SpawnOn(east, 30.0f, 12.0f, Sim::CarStyle::Body::Hatchback);
        if (oncoming) {
            for (int i = 0; i < 6; ++i) traffic.SpawnOn(west, 20.0f + 55.0f * static_cast<float>(i), 14.0f, Sim::CarStyle::Body::Sedan);
        }
        const auto box = [](const Traffic::TrafficVehicle& c) {
            return Collision::Obb::FromHeading(c.position + Vector3(0.0f, 0.5f * c.heightM, 0.0f),
                                               Vector3(0.5f * c.widthM, 0.5f * c.heightM, 0.5f * c.lengthM), c.headingRad);
        };
        OvertakeRun run;
        for (int i = 0; i < 30 * 25; ++i) {
            traffic.Update(1.0f / 30.0f, NoPlayer());
            const auto& cars = traffic.Vehicles();
            const Traffic::TrafficVehicle* l = nullptr;
            const Traffic::TrafficVehicle* c = nullptr;
            for (const auto& v : cars) {
                if (v.id == lorry) l = &v;
                if (v.id == car) c = &v;
            }
            for (std::size_t a = 0; a < cars.size(); ++a) {
                for (std::size_t b = a + 1; b < cars.size(); ++b) {
                    Collision::Contact contact;
                    if (Collision::IntersectObbObb(box(cars[a]), box(cars[b]), contact)) ++run.overlaps;
                }
            }
            if (!l || !c) break;
            run.wentOut = run.wentOut || c->lateral > 1.0f;
            if (c->lateral > 0.5f) {
                for (const auto& o : cars) {
                    // The main road runs along +x eastbound; oncoming cars drive west.
                    if (o.forward.X < -0.5f && o.position.X > c->position.X && o.position.X - c->position.X < 45.0f) ++run.closeCalls;
                }
            }
            if (c->lane == east && l->lane == east && c->s > l->s + 5.0f && c->lateral < 0.1f) run.overtook = true;
        }
        return run;
    }
}

TEST(TrafficSystem, CarsOvertakeASlowLorryWhenTheOtherLaneIsClear)
{
    const OvertakeRun run = RunOvertake(false);
    EXPECT_TRUE(run.wentOut);
    EXPECT_TRUE(run.overtook);
    EXPECT_EQ(run.overlaps, 0);
    EXPECT_EQ(run.closeCalls, 0);
}

TEST(TrafficSystem, NobodyOvertakesIntoOncomingTraffic)
{
    // A stream of oncoming cars: the car waits behind the lorry until the road is clear, and is
    // never out in the other lane with one of them close.
    const OvertakeRun run = RunOvertake(true);
    EXPECT_EQ(run.closeCalls, 0);
    EXPECT_EQ(run.overlaps, 0);
}
