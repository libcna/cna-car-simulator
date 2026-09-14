#include "CarSim/Collision/CollisionWorld.hpp"
#include "CarSim/Collision/Shapes.hpp"
#include "CarSim/Map/MapDocument.hpp"
#include "CarSim/Map/MapWorld.hpp"
#include "CarSim/Sim/Ground.hpp"
#include "CarSim/Sim/Vehicle.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace CarSim;
using namespace CarSim::Collision;
using Microsoft::Xna::Framework::Vector3;

namespace
{
    Obb Unit(const Vector3& centre, const float heading = 0.0f) { return Obb::FromHeading(centre, Vector3(1.0f, 1.0f, 1.0f), heading); }

    struct Rig
    {
        Sim::VehicleDefinition def = Sim::MakeReferenceVehicle();
        Sim::Vehicle vehicle{def, Sim::TransmissionMode::Automatic};
        Sim::FlatGround ground{0.0f};
        CollisionWorld world;
        std::vector<ContactEvent> events;

        void Settle()
        {
            Sim::DriverControls idle;
            for (int i = 0; i < 60; ++i) {
                vehicle.Update(idle, 1.0f / 60.0f, ground);
            }
        }

        void Run(const float seconds, const float throttle = 0.0f)
        {
            Sim::DriverControls c;
            c.throttle = throttle;
            const int frames = static_cast<int>(seconds * 60.0f);
            for (int i = 0; i < frames; ++i) {
                vehicle.Update(c, 1.0f / 60.0f, ground);
                world.ResolveVehicle(vehicle, events);
            }
        }
    };
}

TEST(Shapes, SeparatedAndOverlappingBoxes)
{
    Contact c;
    EXPECT_FALSE(IntersectObbObb(Unit(Vector3(0, 0, 0)), Unit(Vector3(2.5f, 0, 0)), c));
    ASSERT_TRUE(IntersectObbObb(Unit(Vector3(0, 0, 0)), Unit(Vector3(1.9f, 0, 0)), c));
    EXPECT_NEAR(c.penetration, 0.1f, 1e-4f);
    EXPECT_NEAR(c.normal.X, -1.0f, 1e-4f);   // pushes the first box away from the second (towards -x)
    EXPECT_NEAR(c.point.X, 0.95f, 0.06f);
}

TEST(Shapes, RotatedBoxUsesEdgeAxes)
{
    Contact c;
    // A box rotated 45 degrees touching a unit box: corner-to-face contact along x.
    const Obb rotated = Obb::FromHeading(Vector3(2.3f, 0, 0), Vector3(1.0f, 1.0f, 1.0f), 3.14159265f * 0.25f);
    ASSERT_TRUE(IntersectObbObb(rotated, Unit(Vector3(0, 0, 0)), c));
    EXPECT_GT(c.penetration, 0.05f);
    EXPECT_LT(c.penetration, 0.2f);
    EXPECT_NEAR(c.normal.X, 1.0f, 1e-3f);
    EXPECT_FALSE(IntersectObbObb(Obb::FromHeading(Vector3(2.6f, 0, 0), Vector3(1, 1, 1), 3.14159265f * 0.25f), Unit(Vector3(0, 0, 0)), c));
}

TEST(Shapes, CylinderAgainstBox)
{
    Contact c;
    VerticalCylinder cyl;
    cyl.base = Vector3(1.1f, -1.0f, 0.0f);
    cyl.radius = 0.2f;
    cyl.height = 4.0f;
    ASSERT_TRUE(IntersectObbCylinder(Unit(Vector3(0, 0, 0)), cyl, c));
    EXPECT_NEAR(c.penetration, 0.1f, 1e-4f);
    EXPECT_NEAR(c.normal.X, -1.0f, 1e-4f);
    EXPECT_NEAR(c.point.X, 1.0f, 1e-3f);
    cyl.base.X = 1.3f;
    EXPECT_FALSE(IntersectObbCylinder(Unit(Vector3(0, 0, 0)), cyl, c));
    // A cylinder well above the box does not touch it.
    cyl.base = Vector3(0.0f, 1.5f, 0.0f);
    EXPECT_FALSE(IntersectObbCylinder(Unit(Vector3(0, 0, 0)), cyl, c));
}

TEST(CollisionWorld, VehicleBoxMatchesChassis)
{
    Rig rig;
    rig.vehicle.PlaceAt(Vector3(10.0f, 0.0f, -5.0f), 0.0f);
    rig.Settle();
    const Obb box = CollisionWorld::VehicleBox(rig.vehicle);
    EXPECT_NEAR(box.half.X, rig.def.chassis.widthM * 0.5f, 1e-4f);
    EXPECT_NEAR(box.half.Z, rig.def.chassis.lengthM * 0.5f, 1e-4f);
    EXPECT_NEAR(box.centre.X, 10.0f, 0.05f);
    EXPECT_NEAR(box.centre.Y, rig.def.chassis.heightM * 0.5f, 0.15f);
    EXPECT_NEAR(box.axes[2].Z, 1.0f, 1e-3f);   // local +z is backward = +z world at yaw 0
}

TEST(CollisionWorld, VehicleStopsAtWall)
{
    Rig rig;
    // Wall across the road 20 m ahead (north = -z).
    StaticCollider wall;
    wall.kind = ColliderKind::Wall;
    wall.box = Obb::FromHeading(Vector3(0.0f, 1.0f, -20.0f), Vector3(6.0f, 1.5f, 0.3f), 0.0f);
    wall.centre = wall.box.centre;
    wall.boundingRadius = wall.box.BoundingRadius();
    rig.world.AddStatic(wall);
    rig.world.Finish();
    rig.vehicle.PlaceAt(Vector3(0.0f, 0.0f, 0.0f), 0.0f);
    rig.Settle();
    rig.vehicle.ForceForwardSpeed(12.0f);
    rig.Run(3.0f);
    EXPECT_LT(std::fabs(rig.vehicle.ForwardSpeedMs()), 0.6f);
    const Obb box = CollisionWorld::VehicleBox(rig.vehicle);
    const float frontZ = box.centre.Z - box.half.Z;
    EXPECT_GT(frontZ, -20.0f + 0.3f - 0.06f);   // no more than 6 cm into the wall face
    EXPECT_FALSE(rig.events.empty());
    EXPECT_GT(rig.events.front().closingSpeed, 8.0f);
    EXPECT_EQ(rig.events.front().kind, ColliderKind::Wall);
}

TEST(CollisionWorld, OffsetImpactSpinsTheVehicle)
{
    Rig rig;
    StaticCollider post;
    post.kind = ColliderKind::Post;
    post.isBox = false;
    post.cylinder.base = Vector3(0.6f, -0.5f, -15.0f);
    post.cylinder.radius = 0.12f;
    post.cylinder.height = 4.0f;
    post.centre = post.cylinder.base + Vector3(0, 2, 0);
    post.boundingRadius = 2.1f;
    rig.world.AddStatic(post);
    rig.world.Finish();
    rig.vehicle.PlaceAt(Vector3(0.0f, 0.0f, 0.0f), 0.0f);
    rig.Settle();
    rig.vehicle.ForceForwardSpeed(8.0f);
    float maxYawRate = 0.0f;
    for (int i = 0; i < 120; ++i) {
        rig.Run(1.0f / 60.0f);
        maxYawRate = std::max(maxYawRate, std::fabs(rig.vehicle.Body().AngularVelocity().Y));
    }
    EXPECT_GT(maxYawRate, 0.15f);
    EXPECT_FALSE(rig.events.empty());
}

TEST(CollisionWorld, HeadOnPairConservesMomentum)
{
    Sim::VehicleDefinition def = Sim::MakeReferenceVehicle();
    Sim::Vehicle a(def, Sim::TransmissionMode::Automatic);
    Sim::Vehicle b(def, Sim::TransmissionMode::Automatic);
    Sim::FlatGround ground(0.0f);
    a.PlaceAt(Vector3(0.0f, 0.0f, 0.0f), 0.0f);
    b.PlaceAt(Vector3(0.3f, 0.0f, -12.0f), 3.14159265f);
    Sim::DriverControls idle;
    for (int i = 0; i < 60; ++i) {
        a.Update(idle, 1.0f / 60.0f, ground);
        b.Update(idle, 1.0f / 60.0f, ground);
    }
    a.ForceForwardSpeed(6.0f);
    b.ForceForwardSpeed(6.0f);
    const Vector3 before = a.Body().LinearVelocity() * a.Body().Mass() + b.Body().LinearVelocity() * b.Body().Mass();
    CollisionWorld world;
    std::vector<ContactEvent> events;
    for (int i = 0; i < 90; ++i) {
        a.Update(idle, 1.0f / 60.0f, ground);
        b.Update(idle, 1.0f / 60.0f, ground);
        world.ResolveVehiclePair(a, b, events);
    }
    EXPECT_FALSE(events.empty());
    // Both were pushed apart; z momentum stays near the (zero) initial value up to tyre friction.
    const Vector3 after = a.Body().LinearVelocity() * a.Body().Mass() + b.Body().LinearVelocity() * b.Body().Mass();
    EXPECT_NEAR(before.Z, 0.0f, 1.0f);
    EXPECT_LT(std::fabs(after.Z), 2500.0f);
    EXPECT_LT(a.ForwardSpeedMs(), 1.0f);
    EXPECT_LT(b.ForwardSpeedMs(), 1.0f);
    Contact leftover;
    EXPECT_FALSE(IntersectObbObb(CollisionWorld::VehicleBox(a), CollisionWorld::VehicleBox(b), leftover));
}

TEST(CollisionWorld, SampleMapHasCollidersAndClearSpawns)
{
    std::vector<std::string> errors;
    auto map = Map::MapWorld::Load(Map::MapDirectory(CARSIM_TEST_CONTENT_DIR, "lipova"), errors);
    ASSERT_TRUE(map);
    CollisionWorld world;
    world.Build(*map);
    EXPECT_GT(world.StaticCount(), 40000u);
    Sim::VehicleDefinition def = Sim::MakeReferenceVehicle();
    Sim::Vehicle vehicle(def, Sim::TransmissionMode::Automatic);
    for (const auto& spawn : map->Data().traffic.playerSpawns) {
        vehicle.PlaceAt(map->SpawnPosition(spawn), -spawn.headingDeg * 3.14159265f / 180.0f);
        EXPECT_FALSE(world.Overlaps(CollisionWorld::VehicleBox(vehicle))) << "spawn " << spawn.name;
    }
}
