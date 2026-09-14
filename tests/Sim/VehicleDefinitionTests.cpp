#include "CarSim/Sim/VehicleDefinition.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace CarSim::Sim;

#ifndef CARSIM_TEST_CONTENT_DIR
#define CARSIM_TEST_CONTENT_DIR "content"
#endif

TEST(VehicleDefinition, ReferenceVehicleIsValid)
{
    const auto def = MakeReferenceVehicle();
    const auto errors = def.Validate();
    EXPECT_TRUE(errors.empty()) << (errors.empty() ? "" : errors.front());
    EXPECT_NEAR(def.WheelbaseM(), 2.56f, 1e-4f);
    EXPECT_NEAR(def.FrontTrackM(), 1.46f, 1e-4f);
    EXPECT_GT(def.engine.torqueCurve.MaxY(), 100.0f);
}

TEST(VehicleDefinition, ContentFileLoadsAndMatchesReference)
{
    const auto loaded = LoadVehicleDefinitionFile(std::string(CARSIM_TEST_CONTENT_DIR) + "/vehicles/lipan_12.json");
    ASSERT_TRUE(loaded.ok()) << loaded.errors.front();
    const auto& def = loaded.definition;
    const auto reference = MakeReferenceVehicle();
    EXPECT_EQ(def.id, reference.id);
    EXPECT_FLOAT_EQ(def.chassis.massKg, reference.chassis.massKg);
    ASSERT_EQ(def.wheels.size(), 4u);
    EXPECT_EQ(def.wheels[2].name, "RL");
    EXPECT_TRUE(def.wheels[0].driven);
    EXPECT_FALSE(def.wheels[3].steered);
    EXPECT_FLOAT_EQ(def.gearbox.finalDrive, reference.gearbox.finalDrive);
    ASSERT_EQ(def.gearbox.ratios.size(), 5u);
    EXPECT_FLOAT_EQ(def.gearbox.ratios[0], 3.77f);
    EXPECT_FLOAT_EQ(def.engine.torqueCurve.Evaluate(2500.0f), 122.0f);
    EXPECT_FLOAT_EQ(def.fuel.reserveLiters, 7.0f);
    EXPECT_FLOAT_EQ(def.fuel.refillAtReserveFraction, 0.5f);
    EXPECT_EQ(def.gearbox.defaultMode, TransmissionMode::Manual);
    EXPECT_FLOAT_EQ(def.engine.fuel.bsfcGPerKwh.Evaluate(1.0f), 250.0f);
}

TEST(VehicleDefinition, MissingRequiredSectionsAreReported)
{
    const auto result = ParseVehicleDefinition(R"({"schemaVersion": 1, "id": "x"})");
    EXPECT_FALSE(result.ok());
    bool sawChassis = false;
    bool sawEngine = false;
    for (const auto& e : result.errors) {
        sawChassis = sawChassis || e.find("chassis") != std::string::npos;
        sawEngine = sawEngine || e.find("engine") != std::string::npos;
    }
    EXPECT_TRUE(sawChassis);
    EXPECT_TRUE(sawEngine);
}

TEST(VehicleDefinition, InvalidJsonIsReported)
{
    const auto result = ParseVehicleDefinition("{ not json");
    ASSERT_FALSE(result.ok());
    EXPECT_NE(result.errors.front().find("invalid JSON"), std::string::npos);
}

TEST(VehicleDefinition, ValidationCatchesPhysicallyAbsurdValues)
{
    auto def = MakeReferenceVehicle();
    def.chassis.massKg = 50.0f;
    def.gearbox.ratios = {1.0f, 2.0f, 3.0f};
    def.fuel.reserveLiters = 100.0f;
    def.engine.stallRpm = 2000.0f;
    const auto errors = def.Validate();
    EXPECT_GE(errors.size(), 4u);
}

TEST(VehicleDefinition, AutomaticModeParses)
{
    auto reference = MakeReferenceVehicle();
    (void)reference;
    const auto result = ParseVehicleDefinition(R"({
        "schemaVersion": 1, "id": "auto_test",
        "chassis": {"mass": 1300, "length": 4.3, "width": 1.8, "height": 1.5},
        "wheels": [
          {"name":"FL","position":[-0.75,0.31,-1.3],"radius":0.31,"steered":true,"driven":true},
          {"name":"FR","position":[0.75,0.31,-1.3],"radius":0.31,"steered":true,"driven":true},
          {"name":"RL","position":[-0.75,0.31,1.3],"radius":0.31,"handbrakeTorque":900},
          {"name":"RR","position":[0.75,0.31,1.3],"radius":0.31,"handbrakeTorque":900}],
        "engine": {"idleRpm": 750, "redlineRpm": 6000, "limiterRpm": 6300,
                   "torqueCurve": [[500,80],[2000,150],[4000,160],[6300,120]]},
        "gearbox": {"defaultMode": "automatic", "ratios": [3.5,2.0,1.4,1.0,0.8,0.65], "finalDrive": 3.9},
        "fuel": {"tankLiters": 50, "reserveLiters": 8}
    })");
    ASSERT_TRUE(result.ok()) << result.errors.front();
    EXPECT_EQ(result.definition.gearbox.defaultMode, TransmissionMode::Automatic);
    EXPECT_EQ(result.definition.gearbox.ratios.size(), 6u);
    EXPECT_FLOAT_EQ(result.definition.wheels[2].handbrakeTorqueNm, 900.0f);
}
