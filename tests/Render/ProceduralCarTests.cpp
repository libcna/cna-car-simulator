// The procedural car generator: geometry sanity for every body style, wheel contact, roles and
// the UV layout used by the paint detail texture.
#include "CarSim/Render/CarTextures.hpp"
#include "CarSim/Render/ProceduralCar.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <set>

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
