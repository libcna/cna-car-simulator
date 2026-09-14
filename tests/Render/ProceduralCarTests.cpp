// The procedural car generator: geometry sanity for every body style, wheel contact, roles and
// the UV layout used by the paint detail texture.
#include "CarSim/Render/CarTextures.hpp"
#include "CarSim/Render/ProceduralCar.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

using namespace CarSim;
using namespace CarSim::Render;
using Microsoft::Xna::Framework::Vector3;

namespace
{
    const CarPart* Find(const CarModel& model, const std::string& name)
    {
        for (const auto& p : model.parts) {
            if (p.name == name) return &p;
        }
        return nullptr;
    }

    void ExpectFinite(const CarModel& model)
    {
        for (const auto& part : model.parts) {
            for (const auto& v : part.mesh.vertices) {
                ASSERT_TRUE(std::isfinite(v.position.X) && std::isfinite(v.position.Y) && std::isfinite(v.position.Z)) << part.name;
                ASSERT_TRUE(std::isfinite(v.normal.X) && std::isfinite(v.normal.Y) && std::isfinite(v.normal.Z)) << part.name;
                ASSERT_NEAR(v.normal.Length(), 1.0f, 0.05f) << part.name << " has an unnormalised normal";
            }
            for (const auto i : part.mesh.indices) {
                ASSERT_LT(i, part.mesh.vertices.size()) << part.name;
            }
        }
    }
}

TEST(ProceduralCar, PlayerModelHasAnimatedPartsAndStaysInsideTheChassisBox)
{
    const Sim::VehicleDefinition def = Sim::MakeReferenceVehicle();
    const CarModel model = GenerateCar(def);
    ExpectFinite(model);
    std::set<CarPart::Role> roles;
    for (const auto& p : model.parts) roles.insert(p.role);
    for (const CarPart::Role r : {CarPart::Role::WheelFL, CarPart::Role::WheelFR, CarPart::Role::WheelRL, CarPart::Role::WheelRR,
                                  CarPart::Role::SteeringWheel, CarPart::Role::GearLever, CarPart::Role::Interior}) {
        EXPECT_TRUE(roles.count(r)) << "missing role " << static_cast<int>(r);
    }
    ASSERT_NE(Find(model, "body_paint"), nullptr);
    ASSERT_NE(Find(model, "body_glass"), nullptr);
    ASSERT_NE(Find(model, "lamp_head"), nullptr);
    ASSERT_NE(Find(model, "lamp_tail"), nullptr);
    ASSERT_NE(Find(model, "cluster"), nullptr);
    ASSERT_NE(Find(model, "mirror_face"), nullptr);
    // The skin stays inside the chassis dimensions (plus mirrors) and above the ground.
    const auto bounds = Find(model, "body_paint")->mesh.Bounds();
    EXPECT_GT(bounds.Min.Y, 0.05f);
    EXPECT_LT(bounds.Max.Y, def.chassis.heightM + 0.02f);
    EXPECT_LT(bounds.Max.X - bounds.Min.X, def.chassis.widthM + 0.02f);
    EXPECT_NEAR(bounds.Max.Z - bounds.Min.Z, def.chassis.lengthM, 0.05f);
    EXPECT_LT(model.bodyTriangles, 60000);
}

TEST(ProceduralCar, TyresTouchTheGroundAtTheDefinitionRadius)
{
    const Sim::VehicleDefinition def = Sim::MakeReferenceVehicle();
    const CarModel model = GenerateCar(def);
    for (const auto& part : model.parts) {
        if (part.name.rfind("tyre_", 0) != 0) continue;
        // Wheel meshes are in the wheel frame (centre at the origin): the lowest point is -radius.
        const auto bounds = part.mesh.Bounds();
        EXPECT_NEAR(bounds.Min.Y, -def.wheels.front().radiusM, 0.01f) << part.name;
        EXPECT_NEAR(bounds.Max.Y, def.wheels.front().radiusM, 0.01f) << part.name;
        EXPECT_NEAR(bounds.Max.X - bounds.Min.X, def.wheels.front().widthM, 0.03f) << part.name;
        EXPECT_NEAR(part.pivot.Y, def.wheels.front().radiusM, 1e-4f) << "pivot must be the wheel centre";
    }
}

TEST(ProceduralCar, EveryBodyStyleGeneratesACompleteExterior)
{
    for (const CarStyle::Body body : {CarStyle::Body::Hatchback, CarStyle::Body::Sedan, CarStyle::Body::Estate, CarStyle::Body::Suv, CarStyle::Body::Van}) {
        for (const unsigned seed : {1u, 4u}) {
            const CarStyle style = CarStyle::Preset(body, seed);
            const CarModel model = GenerateCar(style, nullptr, false);
            ExpectFinite(model);
            ASSERT_NE(Find(model, "body_paint"), nullptr) << CarStyle::ToString(body);
            ASSERT_NE(Find(model, "body_glass"), nullptr) << CarStyle::ToString(body);
            ASSERT_NE(Find(model, "plates"), nullptr) << CarStyle::ToString(body);
            const auto bounds = Find(model, "body_paint")->mesh.Bounds();
            EXPECT_NEAR(bounds.Max.Z - bounds.Min.Z, style.length, 0.06f) << CarStyle::ToString(body);
            EXPECT_LT(bounds.Max.Y, style.height + 0.02f) << CarStyle::ToString(body);
            EXPECT_GT(bounds.Max.Y, style.height - 0.08f) << CarStyle::ToString(body);
            int wheels = 0;
            int cabin = 0;
            for (const auto& p : model.parts) {
                if (p.role == CarPart::Role::WheelFL || p.role == CarPart::Role::WheelFR || p.role == CarPart::Role::WheelRL || p.role == CarPart::Role::WheelRR) ++wheels;
                if (p.role == CarPart::Role::Interior) {
                    EXPECT_EQ(p.name, "cabin") << "without a cockpit only the cabin block is an interior part";
                    EXPECT_TRUE(p.cabin);
                    ++cabin;
                }
            }
            EXPECT_EQ(wheels, 12) << "tyre, rim and disc per wheel";
            EXPECT_EQ(cabin, 1);
        }
    }
}

TEST(ProceduralCar, UvLayoutIsOrderedAndPaintDetailDrawsShutLines)
{
    const Sim::VehicleDefinition def = Sim::MakeReferenceVehicle();
    const CarModel model = GenerateCar(def);
    const BodyUvLayout& uv = model.uv;
    EXPECT_LT(uv.uUnderbody, uv.uRockerTop);
    EXPECT_LT(uv.uRockerTop, uv.uBelt);
    EXPECT_LT(uv.uBelt, uv.uRoofRail);
    EXPECT_LT(uv.uRoofRail, uv.uTop);
    EXPECT_NEAR(uv.uTop, 0.5f, 0.02f);
    EXPECT_LT(uv.vHoodStart, uv.vCowl);
    EXPECT_LT(uv.vCowl, uv.vDoorFront);
    EXPECT_LT(uv.vDoorFront, uv.vBPillar);
    EXPECT_LT(uv.vBPillar, uv.vDoorRear);
    EXPECT_LT(uv.vDoorRear, uv.vTailgate);
    const Image detail = CarTextures::PaintDetail(uv, 256);
    // A pixel on the B-pillar shut line is darker than the door skin next to it.
    const int x = static_cast<int>((uv.uRockerTop + uv.uBelt) * 0.5f * 256.0f);
    const int yLine = static_cast<int>(uv.vBPillar * 256.0f);
    const int yPanel = static_cast<int>((uv.vDoorFront + uv.vBPillar) * 0.5f * 256.0f);
    EXPECT_LT(detail.At(x, yLine).getRProperty(), detail.At(x, yPanel).getRProperty() - 40);
}

TEST(ProceduralCar, CockpitPlacementMatchesTheSeatingReference)
{
    // Regression for the cockpit whose eye point sat level with the windshield header: the
    // A-pillar filled a quarter of the view and the mirror hung outside the glass.
    const Sim::VehicleDefinition def = Sim::MakeReferenceVehicle();
    const CarModel model = GenerateCar(def);
    const CarPart* paint = Find(model, "body_paint");
    const CarPart* glass = Find(model, "body_glass");
    ASSERT_NE(paint, nullptr);
    ASSERT_NE(glass, nullptr);
    const auto& vis = def.visual;

    // Body top (roof or glass) height on the centre line at a given z.
    const auto topAt = [&](const float z) {
        float best = -1.0f;
        for (const CarPart* part : {paint, glass}) {
            for (const auto& v : part->mesh.vertices) {
                if (std::fabs(v.position.X) < 0.06f && std::fabs(v.position.Z - z) < 0.03f) best = std::max(best, v.position.Y);
            }
        }
        return best;
    };
    // Windshield header: the rearmost windshield glass vertex on the centre line.
    float headerZ = -10.0f;
    for (const auto& v : glass->mesh.vertices) {
        if (std::fabs(v.position.X) < 0.10f && v.position.Z < 0.3f) headerZ = std::max(headerZ, v.position.Z);
    }
    EXPECT_LT(headerZ, vis.driverEye.Z - 0.25f) << "the header must be well ahead of the eye";
    EXPECT_GT(topAt(vis.driverEye.Z), vis.driverEye.Y + 0.15f) << "head clearance under the roof";

    // The interior mirror hangs below the glass with room for its housing, and high enough that
    // its housing stays out of the driver's view of the road (it used to hang at eye level, where
    // it covered the right-hand third of the windscreen).
    EXPECT_GT(topAt(vis.mirrorCenter.Z) - 0.05f, vis.mirrorCenter.Y + 0.04f);
    EXPECT_GT(vis.driverEye.Z - vis.mirrorCenter.Z, 0.45f);
    EXPECT_GT(vis.mirrorCenter.Y - 0.04f, vis.driverEye.Y + 0.05f) << "the mirror sits in the driver's sight line";

    // Steering wheel and cluster: reach and sight lines.
    EXPECT_GE(vis.driverEye.Z - vis.steeringWheelCenter.Z, 0.45f);
    EXPECT_LE(vis.driverEye.Z - vis.steeringWheelCenter.Z, 0.70f);
    EXPECT_GE(vis.driverEye.Y - vis.steeringWheelCenter.Y, 0.25f);
    EXPECT_LT(vis.clusterCenter.Z, vis.steeringWheelCenter.Z - 0.25f);

    // Nothing in the cabin comes closer to the eye than the cockpit camera's near plane.
    float nearest = 1e9f;
    std::string nearestPart;
    for (const auto& part : model.parts) {
        if (part.role != CarPart::Role::Interior && part.role != CarPart::Role::SteeringWheel && part.role != CarPart::Role::GearLever) continue;
        for (const auto& v : part.mesh.vertices) {
            const float d = Vector3::Distance(v.position, vis.driverEye);
            if (d < nearest) { nearest = d; nearestPart = part.name; }
        }
    }
    EXPECT_GT(nearest, 0.14f) << nearestPart;
}

TEST(ProceduralCar, SeatBackrestsLeanRearwardBehindTheEye)
{
    // The head restraints must sit behind (larger z than) and above the seat cushions, i.e. the
    // backrests lean towards the rear of the car, never into the driver's view.
    const Sim::VehicleDefinition def = Sim::MakeReferenceVehicle();
    const CarModel model = GenerateCar(def);
    const CarPart* fabric = Find(model, "interior_fabric");
    ASSERT_NE(fabric, nullptr);
    // Highest fabric vertices on the driver's side are the head restraint.
    float topY = -1.0f;
    for (const auto& v : fabric->mesh.vertices) {
        if (v.position.X < -0.2f && v.position.Z < 0.9f) topY = std::max(topY, v.position.Y);
    }
    float headZ = 0.0f;
    int count = 0;
    for (const auto& v : fabric->mesh.vertices) {
        if (v.position.X < -0.2f && v.position.Z < 0.9f && v.position.Y > topY - 0.10f) { headZ += v.position.Z; ++count; }
    }
    ASSERT_GT(count, 0);
    headZ /= static_cast<float>(count);
    EXPECT_GT(headZ, def.visual.driverEye.Z + 0.08f) << "head restraint behind the eye";
    EXPECT_GT(topY, def.visual.driverEye.Y - 0.05f) << "head restraint reaches eye height";
}

TEST(ProceduralCar, FogLampsFollowTheNoseInsteadOfHangingBesideIt)
{
    // Regression: the fog lamps were placed at the depth of the centre-line nose tip, but the
    // nose sweeps inwards and backwards towards the corners, so the outboard lamps hung beside
    // the bumper over the road (they were 3-4 cm in front of the frontmost painted point, at a
    // lateral position where the body has already curved back by more than 10 cm). They are now
    // projected onto the skin, which also tilts each ring with the surface.
    const Sim::VehicleDefinition def = Sim::MakeReferenceVehicle();
    const CarModel model = GenerateCar(def);
    const CarPart* paint = Find(model, "body_paint");
    const CarPart* chrome = Find(model, "chrome");
    ASSERT_NE(paint, nullptr);
    ASSERT_NE(chrome, nullptr);
    const float noseZ = paint->mesh.Bounds().Min.Z;
    float front = 1e9f, back = -1e9f;
    int lampVertices = 0;
    for (const auto& v : chrome->mesh.vertices) {
        if (std::fabs(v.position.X) < 0.35f || v.position.Z > 0.0f) continue;   // fog lamps only
        front = std::min(front, v.position.Z);
        back = std::max(back, v.position.Z);
        ++lampVertices;
    }
    ASSERT_GT(lampVertices, 100) << "two fog lamp rings with a lens each";
    EXPECT_GE(front, noseZ) << "a fog lamp reaches further forward than the nose tip";
    EXPECT_GT(back - front, 0.03f) << "the ring should follow the curved nose, not sit in a flat plane";
}
