// The autopilot that the benchmark scenarios and the validation drive use. These tests drive the
// real vehicle over the real sample map: no teleporting, no shortcuts, so a route that stops
// being drivable -- because the roads moved, a junction broke or the physics changed -- fails
// here rather than in a screenshot three passes later.
#include "CarSim/Map/MapDocument.hpp"
#include "CarSim/Map/MapWorld.hpp"
#include "CarSim/Sim/Vehicle.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"
#include "CarSim/Traffic/RouteDriver.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <iostream>
#include <string>

using namespace CarSim;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;

namespace
{
    std::string LipovaDirectory() { return Map::MapDirectory(CARSIM_TEST_CONTENT_DIR, "lipova"); }

    struct DriveResult
    {
        bool finished = false;
        float distanceM = 0.0f;
        float routeLengthM = 0.0f;
        float worstLateralM = 0.0f;
        float topSpeedKmh = 0.0f;
        float secondsDriven = 0.0f;
        int wheelsOffGroundFrames = 0;
        Vector3 firstAirborneAt{};     // where the car first left the ground, for the failure message
        float firstAirborneSpeedKmh = 0.0f;
        float firstAirborneLateralM = 0.0f;
        int firstAirborneWheels = 0;   // bit per wheel, FL FR RL RR
    };

    /// Drives one route to its end (or until `limitSeconds`), stepping the vehicle at 1/60 s.
    DriveResult Drive(Map::MapWorld& world, const Map::RouteSpec& spec, const float limitSeconds = 240.0f)
    {
        Sim::VehicleDefinition definition = Sim::MakeReferenceVehicle();
        const auto loaded = Sim::LoadVehicleDefinitionFile(std::string(CARSIM_TEST_CONTENT_DIR) + "/vehicles/lipan_12.json");
        if (loaded.ok()) {
            definition = loaded.definition;
        }
        Sim::Vehicle vehicle(definition, Sim::TransmissionMode::Automatic);
        const Map::SpawnSpec spawn = world.PlayerSpawn(spec.spawn);
        vehicle.PlaceAt(world.SpawnPosition(spawn), -spawn.headingDeg * (std::numbers::pi_v<float> / 180.0f));

        Traffic::RouteDriver driver(world.Lanes());
        const auto state0 = vehicle.Snapshot();
        const Vector3 forward = state0.worldMatrix.getForwardProperty();
        EXPECT_TRUE(driver.Plan(state0.originPosition, std::atan2(forward.X, -forward.Z), spec.waypoints))
            << spec.name << ": " << driver.Progress().note;

        DriveResult result;
        result.routeLengthM = driver.Progress().routeLengthM;
        const float dt = 1.0f / 60.0f;
        for (float t = 0.0f; t < limitSeconds; t += dt) {
            const auto state = vehicle.Snapshot();
            const Sim::DriverControls controls = driver.Update(state, dt);
            vehicle.Update(controls, dt, world.Ground());
            result.topSpeedKmh = std::max(result.topSpeedKmh, state.speedKmh);
            // The first step is the car settling onto its springs from PlaceAt; only count
            // wheels leaving the ground once it is standing.
            if (t > 0.5f) {
                const int grounded = static_cast<int>(std::count_if(state.wheels.begin(), state.wheels.end(),
                                                                    [](const auto& w) { return w.grounded; }));
                if (grounded < 4) {
                    if (result.wheelsOffGroundFrames == 0) {
                        result.firstAirborneAt = state.originPosition;
                        result.firstAirborneSpeedKmh = state.speedKmh;
                        result.firstAirborneLateralM = driver.Progress().lateralErrorM;
                        for (std::size_t wi = 0; wi < state.wheels.size(); ++wi) {
                            if (!state.wheels[wi].grounded) result.firstAirborneWheels += static_cast<int>(1u << wi);
                        }
                    }
                    ++result.wheelsOffGroundFrames;
                }
            }
            result.secondsDriven = t;
            if (driver.Progress().finished) {
                break;
            }
        }
        result.finished = driver.Progress().finished;
        result.distanceM = driver.Progress().distanceM;
        result.worstLateralM = driver.Progress().offRouteM;
        return result;
    }
}

TEST(RouteDriver, SampleMapDefinesTheBenchmarkRoutes)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(LipovaDirectory(), errors);
    ASSERT_TRUE(world) << (errors.empty() ? "" : errors.front());
    const auto& routes = world->Data().traffic.routes;
    ASSERT_FALSE(routes.empty()) << "the sample map must define the benchmark routes";
    for (const auto& route : routes) {
        EXPECT_FALSE(route.name.empty());
        EXPECT_FALSE(route.spawn.empty()) << route.name << " must name the spawn it starts from";
        EXPECT_GE(route.waypoints.size(), 1u) << route.name << " needs at least one waypoint";
        // The spawn must exist: PlayerSpawn falls back to the first one, so compare the names.
        EXPECT_EQ(world->PlayerSpawn(route.spawn).name, route.spawn) << route.name << " names an unknown spawn";
    }
}

TEST(RouteDriver, EveryRouteOfTheSampleMapCanBeDriven)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(LipovaDirectory(), errors);
    ASSERT_TRUE(world) << (errors.empty() ? "" : errors.front());
    for (const auto& route : world->Data().traffic.routes) {
        const DriveResult result = Drive(*world, route);
        std::cout << "  route " << route.name << ": " << result.routeLengthM << " m in " << result.secondsDriven
                  << " s, top " << result.topSpeedKmh << " km/h, worst lateral " << result.worstLateralM << " m\n";
        EXPECT_TRUE(result.finished)
            << route.name << ": stopped after " << result.distanceM << " of " << result.routeLengthM << " m";
        EXPECT_GT(result.routeLengthM, 200.0f) << route.name << " is too short to be a useful scenario";
        // Staying inside a lane and a half of the centreline means the car drove the road rather
        // than cutting across the countryside.
        EXPECT_LT(result.worstLateralM, 2.5f) << route.name << ": worst lateral error -- the car left its lane";
        EXPECT_GT(result.topSpeedKmh, 25.0f) << route.name << ": never got going";
        EXPECT_EQ(result.wheelsOffGroundFrames, 0)
            << route.name << ": left the ground near (" << result.firstAirborneAt.X << ", " << result.firstAirborneAt.Z
            << ") at " << result.firstAirborneSpeedKmh << " km/h, " << result.firstAirborneLateralM
            << " m off the lane centre, wheel mask " << result.firstAirborneWheels;
    }
}

TEST(RouteDriver, RefusesARouteItCannotPlan)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(LipovaDirectory(), errors);
    ASSERT_TRUE(world);
    Traffic::RouteDriver driver(world->Lanes());
    // A point far out in the fields is nowhere near a lane.
    EXPECT_FALSE(driver.Plan(Vector3(0.0f, 0.0f, 0.0f), 0.0f, {Vector2(-3000.0f, 3000.0f)}));
    EXPECT_FALSE(driver.Progress().note.empty());
    // And with no route planned it asks for the brake rather than driving off.
    Sim::VehicleState state;
    const auto controls = driver.Update(state, 1.0f / 60.0f);
    EXPECT_GT(controls.brake, 0.9f);
}

TEST(RouteDriver, IsDeterministic)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(LipovaDirectory(), errors);
    ASSERT_TRUE(world);
    ASSERT_FALSE(world->Data().traffic.routes.empty());
    const auto& route = world->Data().traffic.routes.front();
    const DriveResult a = Drive(*world, route);
    const DriveResult b = Drive(*world, route);
    EXPECT_EQ(a.finished, b.finished);
    EXPECT_FLOAT_EQ(a.distanceM, b.distanceM);
    EXPECT_FLOAT_EQ(a.worstLateralM, b.worstLateralM);
    EXPECT_FLOAT_EQ(a.topSpeedKmh, b.topSpeedKmh);
}
