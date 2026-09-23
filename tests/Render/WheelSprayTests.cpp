#include "CarSim/Render/WheelSpray.hpp"

#include <gtest/gtest.h>

using namespace CarSim::Render;
using Microsoft::Xna::Framework::Vector3;

namespace
{
    std::size_t PuffsAfter(const float speedMs, const float wetness, const float share = 1.0f, const float seconds = 1.0f)
    {
        WheelSpray spray;
        std::vector<SprayEmitter> emitters(1);
        emitters[0].velocity = Vector3(0.0f, 0.0f, -speedMs);
        emitters[0].share = share;
        for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i) spray.Update(1.0f / 60.0f, emitters, wetness);
        return spray.Puffs().size();
    }
}

TEST(WheelSpray, OnlyAWetRoadAtSpeedThrowsSpray)
{
    EXPECT_EQ(PuffsAfter(25.0f, 0.0f), 0u) << "dry road";
    EXPECT_EQ(PuffsAfter(3.0f, 1.0f), 0u) << "walking pace";
    EXPECT_EQ(PuffsAfter(25.0f, 1.0f, 0.0f), 0u) << "grass";
    EXPECT_GT(PuffsAfter(10.0f, 1.0f), 0u);
    EXPECT_GT(PuffsAfter(30.0f, 1.0f), PuffsAfter(10.0f, 1.0f)) << "the plume grows with speed";
}

TEST(WheelSpray, PuffsTrailBehindAndStayBounded)
{
    WheelSpray spray;
    std::vector<SprayEmitter> emitters(40);
    for (auto& e : emitters) e.velocity = Vector3(0.0f, 0.0f, -35.0f);
    for (int i = 0; i < 600; ++i) spray.Update(1.0f / 60.0f, emitters, 1.0f);
    EXPECT_LE(static_cast<int>(spray.Puffs().size()), WheelSpray::kMaxPuffs);
    for (const auto& p : spray.Puffs()) {
        EXPECT_GT(p.velocity.Z, -35.0f) << "spray is slower than the car that threw it";
        EXPECT_LE(p.age, p.lifetime);
    }
}
