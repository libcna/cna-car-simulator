#include "CarSim/Sim/Electrics.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"

#include <gtest/gtest.h>

using namespace CarSim::Sim;

TEST(Electrics, IndicatorBlinksAtTheConfiguredPeriod)
{
    const VehicleDefinition def = MakeReferenceVehicle();
    Electrics e(def.electrics);
    e.ApplyIndicator(IndicatorRequest::ToggleLeft);
    EXPECT_EQ(e.Indicator(), IndicatorMode::Left);
    const float dt = 1.0f / 120.0f;
    int edges = 0;
    int litSteps = 0;
    const int steps = static_cast<int>(def.electrics.indicatorPeriodS * 4.0f / dt);
    for (int i = 0; i < steps; ++i) {
        e.Step(dt, true);
        edges += e.BlinkEdge() ? 1 : 0;
        litSteps += e.LeftIndicatorLit() ? 1 : 0;
        EXPECT_FALSE(e.RightIndicatorLit());
    }
    EXPECT_NEAR(edges, 8, 1);                      // two edges per period over four periods
    EXPECT_NEAR(static_cast<float>(litSteps) / static_cast<float>(steps), 0.5f, 0.05f);
    e.ApplyIndicator(IndicatorRequest::ToggleLeft);
    EXPECT_EQ(e.Indicator(), IndicatorMode::Off);
    e.Step(dt, true);
    EXPECT_FALSE(e.LeftIndicatorLit());
}

TEST(Electrics, HazardWorksWithoutIgnitionButIndicatorsDoNot)
{
    const VehicleDefinition def = MakeReferenceVehicle();
    Electrics e(def.electrics);
    e.ApplyIndicator(IndicatorRequest::ToggleRight);
    e.Step(0.01f, false);
    EXPECT_FALSE(e.RightIndicatorLit());
    e.ApplyIndicator(IndicatorRequest::ToggleHazard);
    e.Step(0.01f, false);
    EXPECT_TRUE(e.LeftIndicatorLit());
    EXPECT_TRUE(e.RightIndicatorLit());
}

TEST(Electrics, HeadlightModesNeedIgnition)
{
    const VehicleDefinition def = MakeReferenceVehicle();
    Electrics e(def.electrics);
    e.ToggleHeadlights();
    e.Step(0.01f, false);
    EXPECT_FALSE(e.LowBeamOn());
    e.Step(0.01f, true);
    EXPECT_TRUE(e.LowBeamOn());
    EXPECT_FALSE(e.HighBeamOn());
    e.ToggleHighBeam();
    e.Step(0.01f, true);
    EXPECT_TRUE(e.HighBeamOn());
    e.ToggleHeadlights();
    e.Step(0.01f, true);
    EXPECT_FALSE(e.LowBeamOn());
    e.ToggleHighBeam();
    EXPECT_TRUE(e.HighBeamOn());
    e.ToggleHighBeam();
    EXPECT_TRUE(e.LowBeamOn());
    EXPECT_FALSE(e.HighBeamOn());
}

TEST(Electrics, IndicatorCancelsItselfWhenTheWheelReturnsAfterATurn)
{
    const VehicleDefinition def = MakeReferenceVehicle();
    Electrics e(def.electrics);
    e.ApplyIndicator(IndicatorRequest::ToggleLeft);
    e.TrackSteering(-0.05f);
    e.TrackSteering(0.0f);
    EXPECT_EQ(e.Indicator(), IndicatorMode::Left) << "not armed before the turn";
    for (float s = 0.0f; s >= -0.6f; s -= 0.05f) e.TrackSteering(s);
    EXPECT_EQ(e.Indicator(), IndicatorMode::Left) << "still turning";
    for (float s = -0.6f; s <= 0.0f; s += 0.05f) e.TrackSteering(s);
    EXPECT_EQ(e.Indicator(), IndicatorMode::Off);
}

TEST(Electrics, LaneChangeAndOppositeSteeringDoNotCancel)
{
    const VehicleDefinition def = MakeReferenceVehicle();
    Electrics e(def.electrics);
    e.ApplyIndicator(IndicatorRequest::ToggleRight);
    for (float s : {0.05f, 0.15f, 0.2f, 0.1f, 0.0f, -0.15f, 0.0f}) e.TrackSteering(s);
    EXPECT_EQ(e.Indicator(), IndicatorMode::Right) << "a lane change keeps the indicator";
    for (float s : {-0.5f, -0.8f, 0.0f}) e.TrackSteering(s);
    EXPECT_EQ(e.Indicator(), IndicatorMode::Right) << "turning the other way does not arm the cam";
    e.ApplyIndicator(IndicatorRequest::ToggleHazard);
    for (float s : {0.8f, 0.0f, -0.8f, 0.0f}) e.TrackSteering(s);
    EXPECT_EQ(e.Indicator(), IndicatorMode::Hazard) << "hazards never self-cancel";
}
