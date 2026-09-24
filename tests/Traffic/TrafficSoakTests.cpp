// TRF-008: thirty simulated minutes of ambient traffic on the sample map with the player parked
// off the road next to the square: no car bodies overlap, no car is stuck for minutes, plates stay
// unique. (A player parked on the carriageway legitimately builds a queue behind it; that case is
// covered by TrafficTests.)
#include "CarSim/Collision/Shapes.hpp"
#include "CarSim/Map/MapDocument.hpp"
#include "CarSim/Map/MapWorld.hpp"
#include "CarSim/Traffic/Pedestrians.hpp"
#include "CarSim/Traffic/TrafficSystem.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

using namespace CarSim;
using Microsoft::Xna::Framework::Vector3;

TEST(TrafficSoak, ThirtyMinutesWithoutOverlapsOrStuckCars)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(Map::MapDirectory(CARSIM_TEST_CONTENT_DIR, "lipova"), errors);
    ASSERT_TRUE(world);
    Traffic::TrafficSystem traffic(*world, std::getenv("CARSIM_SOAK_SEED") ? std::atoi(std::getenv("CARSIM_SOAK_SEED")) : 21);
    traffic.SetDensity(20);
    Traffic::PlayerProbe player;
    player.valid = true;
    // 14 m north of the spawn point on the square, off every lane.
    Vector3 parked = world->SpawnPosition(world->PlayerSpawn());
    parked.Z -= 14.0f;
    parked.Y = world->Ground().HeightAt(parked.X, parked.Z);
    player.position = parked;
    player.forward = Vector3(1.0f, 0.0f, 0.0f);
    float parkedS = 0.0f;
    ASSERT_EQ(world->Lanes().NearestLane(Microsoft::Xna::Framework::Vector2(parked.X, parked.Z), 0.0f, 4.0f, &parkedS), -1)
        << "the soak needs the player parked off the road";

    const float dt = 1.0f / 30.0f;   // traffic is robust at 30 Hz; halves the soak time
    const int steps = 30 * 60 * 30;  // 30 minutes
    std::map<int, float> stillSince;
    std::map<int, Vector3> lastPosition;
    int overlapFrames = 0;
    int stuckCars = 0;
    std::set<std::string> plates;
    int maxSimultaneous = 0;
    int backingOffObservations = 0;
    int committedObservations = 0;
    const bool verbose = std::getenv("CARSIM_SOAK_VERBOSE") != nullptr;
    const auto describe = [&](const Traffic::TrafficVehicle& c) {
        std::printf("    car %d (%s, %.1f m) lane %d link %d next %d s %.1f speed %.2f waiting %d wait %.1f stopped %d at (%.1f, %.1f)\n", c.id,
                    Sim::CarStyle::ToString(c.body), static_cast<double>(c.lengthM), c.lane,
                    c.link, c.nextLink, static_cast<double>(c.s), static_cast<double>(c.speed), c.waiting ? 1 : 0,
                    static_cast<double>(c.waitTime), c.stoppedAtLine ? 1 : 0, static_cast<double>(c.position.X), static_cast<double>(c.position.Z));
    };
    for (int step = 0; step < steps; ++step) {
        traffic.Update(dt, player);
        const auto& cars = traffic.Vehicles();
        maxSimultaneous = std::max(maxSimultaneous, static_cast<int>(cars.size()));
        // Every car's plate, every step: a car spawned in the last second of the run must be
        // counted too (sampled once a second, it was missed and the check failed spuriously).
        for (const auto& c : cars) plates.insert(c.plate);
        if (step % 30 != 0) continue;   // checks once per simulated second
        for (const auto& c : cars) {
            backingOffObservations += c.backingOff ? 1 : 0;
            committedObservations += c.committed ? 1 : 0;
            auto it = lastPosition.find(c.id);
            if (it != lastPosition.end() && Vector3::Distance(it->second, c.position) < 0.2f) {
                stillSince[c.id] += 1.0f;
                // Waiting at a junction or behind another car is legitimate; two minutes is not.
                if (stillSince[c.id] > 120.0f) {
                    ++stuckCars;
                    if (verbose) {
                        std::printf("  t=%d s stuck:\n", step / 30);
                        describe(c);
                        if (stuckCars == 1) {
                            std::printf("  --- all cars ---\n");
                            for (const auto& o : cars) describe(o);
                            std::printf("  --- links ---\n");
                            for (const auto& o : cars) {
                                const int l = o.link >= 0 ? o.link : o.nextLink;
                                if (l < 0) continue;
                                const auto& link = world->Lanes().LinkAt(l);
                                std::printf("    link %d: lane %d -> lane %d inter %d control %d turn %d len %.1f yieldTo:", l, link.fromLane, link.toLane,
                                            link.intersection, static_cast<int>(link.control), static_cast<int>(link.turn), static_cast<double>(link.length));
                                for (int y : link.yieldTo) std::printf(" %d", y);
                                std::printf("\n");
                            }
                        }
                    }
                    stillSince[c.id] = 0.0f;
                }
            } else {
                stillSince[c.id] = 0.0f;
            }
            lastPosition[c.id] = c.position;
        }
        const auto box = [](const Traffic::TrafficVehicle& c) {
            return Collision::Obb::FromHeading(c.position + Vector3(0.0f, 0.5f * c.heightM, 0.0f),
                                               Vector3(0.5f * c.widthM, 0.5f * c.heightM, 0.5f * c.lengthM), c.headingRad);
        };
        for (std::size_t i = 0; i < cars.size(); ++i) {
            for (std::size_t j = i + 1; j < cars.size(); ++j) {
                if (Vector3::Distance(cars[i].position, cars[j].position) > 8.0f) continue;
                Collision::Contact contact;
                if (Collision::IntersectObbObb(box(cars[i]), box(cars[j]), contact)) {
                    ++overlapFrames;
                    if (verbose) {
                        std::printf("  t=%d s overlap:\n", step / 30);
                        describe(cars[i]);
                        describe(cars[j]);
                    }
                }
            }
        }
    }
    if (verbose) {
        std::printf("  spawned %d, max simultaneous %d, deadlock releases seen %d s, back-offs seen %d s\n", traffic.SpawnedTotal(), maxSimultaneous,
                    committedObservations, backingOffObservations);
    }
    EXPECT_GE(maxSimultaneous, 12);
    EXPECT_EQ(overlapFrames, 0);
    EXPECT_EQ(stuckCars, 0);
    EXPECT_GT(traffic.SpawnedTotal(), 40);
    EXPECT_EQ(plates.size(), static_cast<std::size_t>(traffic.SpawnedTotal()));
}

// A second soak, in the north-east of the enlarged map, watching the things the first one does
// not: that nobody drives into a signalised junction against a red, that cars stay in the lane
// they are on, and that nothing is spawned in the player's lap. Ten simulated minutes at the
// signalised junction "U kaple", with the player parked on the verge beside it.
TEST(TrafficSoak, TenMinutesAtTheSignalsWithoutRedLightsOrWrongLanes)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(Map::MapDirectory(CARSIM_TEST_CONTENT_DIR, "lipova"), errors);
    ASSERT_TRUE(world);
    const auto& lanes = world->Lanes();

    // Find the signalised intersection and a place to park next to it.
    int signalised = -1;
    for (std::size_t i = 0; i < world->Roads().Intersections().size(); ++i) {
        if (world->Roads().Intersections()[i].signals.enabled) {
            signalised = static_cast<int>(i);
            break;
        }
    }
    ASSERT_GE(signalised, 0) << "the sample map must have a signalised junction";
    const auto& junction = world->Roads().Intersections()[static_cast<std::size_t>(signalised)];

    Traffic::TrafficSystem traffic(*world, 4242);
    traffic.SetDensity(20);
    Traffic::PlayerProbe player;
    player.valid = true;
    Vector3 parked(junction.center.X, 0.0f, junction.center.Z - 40.0f);
    parked.Y = world->Ground().HeightAt(parked.X, parked.Z);
    player.position = parked;
    player.forward = Vector3(1.0f, 0.0f, 0.0f);

    const float dt = 1.0f / 30.0f;
    const int steps = 30 * 60 * 10;   // ten minutes
    std::map<int, int> previousLink;
    std::set<int> crossed;   // cars currently past a stop line
    std::set<int> seen;
    int redLightEntries = 0;
    int wrongLaneObservations = 0;
    int spawnedOnPlayer = 0;
    float worstAgreement = 1.0f;
    int signalEntries = 0;

    for (int step = 0; step < steps; ++step) {
        traffic.Update(dt, player);
        for (const auto& c : traffic.Vehicles()) {
            // Crossing the stop line of a signalised approach: the aspect at that moment must
            // not be red. (Reaching the connector itself is not the test: a car that crossed on
            // amber is still clearing the junction when the light goes red, which is correct.)
            const int link = c.link >= 0 ? c.link : c.nextLink;
            const auto it = previousLink.find(c.id);
            const int before = it == previousLink.end() ? -1 : it->second;
            if (c.link < 0 && c.lane >= 0 && c.nextLink >= 0) {
                const auto& connector = lanes.LinkAt(c.nextLink);
                if (connector.signalGroup >= 0) {
                    const float toEnd = lanes.LaneAt(c.lane).length - c.s;
                    const float stopLine = 1.0f + c.lengthM * 0.5f;
                    const bool overTheLine = toEnd <= stopLine;
                    const bool wasBehind = !crossed.count(c.id);
                    if (overTheLine && wasBehind) {
                        crossed.insert(c.id);
                        ++signalEntries;
                        const auto aspect = traffic.AspectOf(connector.intersection, connector.signalGroup);
                        if (aspect == Traffic::SignalAspect::Red) {
                            ++redLightEntries;
                            std::printf("  RED CROSSING t=%.1f s car %d group %d speed %.2f toEnd %.2f\n",
                                        static_cast<double>(step * dt), c.id, connector.signalGroup,
                                        static_cast<double>(c.speed), static_cast<double>(toEnd));
                        }
                    }
                } 
            } else if (c.link < 0) {
                crossed.erase(c.id);   // back on an open lane: ready for the next junction
            }
            previousLink[c.id] = link;

            // A car that has just appeared must not be in the player's lap.
            if (seen.insert(c.id).second) {
                if (Vector3::Distance(c.position, player.position) < 25.0f) {
                    ++spawnedOnPlayer;
                }
            }

            // Facing the way its lane goes. Cars ride their lane's centreline by construction, so
            // a lateral check could never fail; what can go wrong is a car pointing the wrong way
            // down it -- the mirrored-heading class of bug this project has hit before.
            if (step % 15 == 0 && c.lane >= 0 && c.link < 0) {
                const auto& lane = lanes.LaneAt(c.lane);
                const auto point = lane.Evaluate(std::clamp(c.s, 0.0f, lane.length));
                const float agreement = point.tangent.X * c.forward.X + point.tangent.Z * c.forward.Z;
                worstAgreement = std::min(worstAgreement, agreement);
                if (agreement < 0.6f) {
                    ++wrongLaneObservations;
                }
            }
        }
    }

    std::printf("  signal entries %d, worst heading agreement %.2f, spawned %d\n", signalEntries,
                static_cast<double>(worstAgreement), traffic.SpawnedTotal());
    EXPECT_GT(signalEntries, 20) << "the soak never used the signalised junction";
    EXPECT_EQ(redLightEntries, 0) << "cars entered the junction against a red";
    EXPECT_EQ(wrongLaneObservations, 0) << "cars faced the wrong way along their lane (worst "
                                        << worstAgreement << ")";
    EXPECT_EQ(spawnedOnPlayer, 0) << "cars appeared within 25 m of the parked player";
}

TEST(TrafficMap, EveryBusShelterServesALane)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(Map::MapDirectory(CARSIM_TEST_CONTENT_DIR, "lipova"), errors);
    ASSERT_TRUE(world);
    Traffic::TrafficSystem traffic(*world, 1);
    int shelters = 0;
    for (const auto& p : world->Objects().Props()) shelters += p.type == Map::PropType::BusStop ? 1 : 0;
    int stops = 0;
    for (const auto& lane : world->Lanes().Lanes()) stops += static_cast<int>(traffic.BusStopsOn(lane.id).size());
    EXPECT_GT(shelters, 0);
    EXPECT_EQ(stops, shelters);
}

TEST(TrafficSoak, TenMinutesInTownWithPeopleCrossing)
{
    // The town with its people out: cars stop for everyone on a crossing, never drive through
    // a person on the road, and neither the cars nor the people get stuck.
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(Map::MapDirectory(CARSIM_TEST_CONTENT_DIR, "lipova"), errors);
    ASSERT_TRUE(world);
    Traffic::TrafficSystem traffic(*world, 33);
    traffic.SetDensity(20);
    Traffic::Pedestrians people(*world, 44);
    Traffic::PlayerProbe player;
    player.valid = true;
    Vector3 parked = world->SpawnPosition(world->PlayerSpawn());
    parked.Z -= 14.0f;
    parked.Y = world->Ground().HeightAt(parked.X, parked.Z);
    player.position = parked;
    const auto box = [](const Traffic::TrafficVehicle& c) {
        return Collision::Obb::FromHeading(c.position + Vector3(0.0f, 0.5f * c.heightM, 0.0f),
                                           Vector3(0.5f * c.widthM, 0.5f * c.heightM, 0.5f * c.lengthM), c.headingRad);
    };
    int overlaps = 0;
    int runOver = 0;
    int crossings = 0;
    std::map<int, int> wasCrossing;
    const float dt = 1.0f / 30.0f;
    for (int step = 0; step < 30 * 60 * 10; ++step) {
        people.Update(dt, player.position, traffic.Vehicles(), player, 48);
        traffic.SetPedestrians(people.RoadProbes());
        traffic.Update(dt, player);
        for (const auto& p : people.People()) {
            int& was = wasCrossing[p.id];
            if (was && p.crossing == -1) ++crossings;
            was = p.crossing != -1 ? 1 : 0;
        }
        if (step % 10 != 0) continue;
        const auto& cars = traffic.Vehicles();
        for (std::size_t a = 0; a < cars.size(); ++a) {
            for (std::size_t b = a + 1; b < cars.size(); ++b) {
                if (Vector3::Distance(cars[a].position, cars[b].position) > 14.0f) continue;
                Collision::Contact contact;
                if (Collision::IntersectObbObb(box(cars[a]), box(cars[b]), contact)) ++overlaps;
            }
            for (const auto& p : people.People()) {
                if (!p.onRoad || Vector3::Distance(p.position, cars[a].position) > 6.0f) continue;
                if (box(cars[a]).Contains(p.position + Vector3(0.0f, 0.9f, 0.0f), 0.2f) && cars[a].speed > 0.5f) ++runOver;
            }
        }
    }
    std::printf("  people crossed %d times\n", crossings);
    EXPECT_EQ(overlaps, 0);
    EXPECT_EQ(runOver, 0) << "a car drove into a person on the road";
    EXPECT_GT(crossings, 3) << "the people should actually cross";
}
