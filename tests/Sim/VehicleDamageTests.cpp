#include "CarSim/Sim/VehicleDamage.hpp"

#include <gtest/gtest.h>

using namespace CarSim::Sim;
using Microsoft::Xna::Framework::Vector3;

TEST(VehicleDamage, AGentleKnockLeavesNoMarkAHardOneDentsTheBody)
{
    VehicleDamage d;
    EXPECT_FALSE(d.AddImpact(Vector3(0.0f, 0.6f, -2.0f), Vector3(0.0f, 0.0f, 1.0f), 1.5f, -2.0f, 2.0f));
    EXPECT_TRUE(d.Dents().empty());
    ASSERT_TRUE(d.AddImpact(Vector3(0.0f, 0.6f, -2.0f), Vector3(0.0f, 0.0f, 1.0f), 8.0f, -2.0f, 2.0f));
    ASSERT_EQ(d.Dents().size(), 1u);
    const Vector3 atCentre = d.Displacement(Vector3(0.0f, 0.6f, -2.0f));
    EXPECT_GT(atCentre.Z, 0.03f) << "pushed into the body (front is -Z)";
    EXPECT_NEAR(d.Displacement(Vector3(0.0f, 0.6f, 2.0f)).Length(), 0.0f, 1e-6f) << "the tail is untouched";
    EXPECT_TRUE(d.HeadlampsBroken());
    EXPECT_FALSE(d.TailLampsBroken());
}

TEST(VehicleDamage, RepeatedKnocksDeepenOneDentUpToALimitAndRepairClearsAll)
{
    VehicleDamage d;
    for (int i = 0; i < 20; ++i) d.AddImpact(Vector3(0.9f, 0.7f, 0.0f), Vector3(-1.0f, 0.0f, 0.0f), 6.0f, -2.0f, 2.0f);
    ASSERT_EQ(d.Dents().size(), 1u);
    EXPECT_LE(d.Dents().front().depth, VehicleDamage::kMaxDepthM + 1e-6f);
    EXPECT_FALSE(d.HeadlampsBroken()) << "a side impact leaves the lamps";
    const int version = d.Version();
    d.Repair();
    EXPECT_TRUE(d.Dents().empty());
    EXPECT_GT(d.Version(), version);
}
