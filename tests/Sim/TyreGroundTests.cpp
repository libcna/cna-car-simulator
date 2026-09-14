#include "CarSim/Sim/Ground.hpp"
#include "CarSim/Sim/Tyre.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace CarSim::Sim;
using Microsoft::Xna::Framework::Vector3;

class TyreTest : public ::testing::Test
{
protected:
    VehicleDefinition def = MakeReferenceVehicle();
    TyreModel tyre{def.tyres};
};

TEST_F(TyreTest, ShapePeaksAtNormalisedSlipOne)
{
    EXPECT_NEAR(tyre.ShapeFunction(0.0f), 0.0f, 1e-4f);
    EXPECT_NEAR(tyre.ShapeFunction(1.0f), 1.0f, 0.02f);
    EXPECT_LT(tyre.ShapeFunction(4.0f), tyre.ShapeFunction(1.0f));
    EXPECT_GT(tyre.ShapeFunction(4.0f), 0.6f) << "sliding friction should stay a good fraction of the peak";
    EXPECT_GT(tyre.ShapeFunction(0.5f), 0.5f);
}

TEST_F(TyreTest, SignConventionsAndBounds)
{
    const float load = 3000.0f;
    const auto drive = tyre.Compute(0.05f, 0.0f, load, 1.0f);
    EXPECT_GT(drive.longitudinal, 0.0f) << "wheel spinning faster than the road pushes forward";
    EXPECT_NEAR(drive.lateral, 0.0f, 1e-3f);

    const auto brake = tyre.Compute(-0.05f, 0.0f, load, 1.0f);
    EXPECT_LT(brake.longitudinal, 0.0f);

    const auto slipRight = tyre.Compute(0.0f, 0.05f, load, 1.0f);
    EXPECT_LT(slipRight.lateral, 0.0f) << "contact patch sliding right is pushed back left";

    const auto combined = tyre.Compute(0.3f, 0.3f, load, 1.0f);
    const float magnitude = std::sqrt(combined.longitudinal * combined.longitudinal + combined.lateral * combined.lateral);
    EXPECT_LE(magnitude, load * combined.friction * 1.001f);
}

TEST_F(TyreTest, LoadSensitivityAndSurface)
{
    EXPECT_LT(tyre.PeakFriction(6000.0f, 1.0f), tyre.PeakFriction(3000.0f, 1.0f));
    EXPECT_LT(tyre.PeakFriction(3000.0f, SurfaceFrictionFactor(SurfaceType::Grass)), tyre.PeakFriction(3000.0f, 1.0f));
    EXPECT_GT(tyre.LongitudinalStiffness(3000.0f, 1.0f), 10000.0f);
}

TEST(Ground, FlatGroundRaycast)
{
    const FlatGround ground(2.0f, SurfaceType::Gravel);
    GroundHit hit;
    ASSERT_TRUE(ground.Raycast(Vector3(1.0f, 5.0f, 1.0f), Vector3(0.0f, -1.0f, 0.0f), 10.0f, hit));
    EXPECT_NEAR(hit.distance, 3.0f, 1e-5f);
    EXPECT_NEAR(hit.point.Y, 2.0f, 1e-5f);
    EXPECT_EQ(hit.surface, SurfaceType::Gravel);
    EXPECT_FALSE(ground.Raycast(Vector3(1.0f, 5.0f, 1.0f), Vector3(0.0f, -1.0f, 0.0f), 2.0f, hit));
}

TEST(Ground, FunctionGroundSlopeNormal)
{
    const FunctionGround ground([](float x, float) { return 0.1f * x; });
    GroundHit hit;
    ASSERT_TRUE(ground.Raycast(Vector3(10.0f, 5.0f, 0.0f), Vector3(0.0f, -1.0f, 0.0f), 10.0f, hit));
    EXPECT_NEAR(hit.point.Y, 1.0f, 1e-2f);
    EXPECT_LT(hit.normal.X, 0.0f) << "surface rising towards +x leans its normal towards -x";
    EXPECT_NEAR(hit.normal.Length(), 1.0f, 1e-4f);
}
