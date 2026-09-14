#include "CarSim/Sim/Clutch.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"

#include <gtest/gtest.h>

using namespace CarSim::Sim;

TEST(Clutch, CapacityFollowsPedal)
{
    const auto def = MakeReferenceVehicle();
    const Clutch clutch(def.clutch);
    EXPECT_FLOAT_EQ(clutch.Capacity(0.0f), def.clutch.maxTorqueNm);
    EXPECT_FLOAT_EQ(clutch.Capacity(1.0f), 0.0f);
    EXPECT_FLOAT_EQ(clutch.Capacity(def.clutch.engageStart), def.clutch.maxTorqueNm);
    EXPECT_FLOAT_EQ(clutch.Capacity(def.clutch.engageEnd), 0.0f);
    const float mid = clutch.Capacity(0.5f * (def.clutch.engageStart + def.clutch.engageEnd));
    EXPECT_NEAR(mid, def.clutch.maxTorqueNm * 0.5f, 1.0f);

    float previous = clutch.Capacity(0.0f);
    for (float pedal = 0.05f; pedal <= 1.0f; pedal += 0.05f) {
        const float c = clutch.Capacity(pedal);
        EXPECT_LE(c, previous + 1e-4f) << "capacity must not increase as the pedal is pressed";
        previous = c;
    }
}
