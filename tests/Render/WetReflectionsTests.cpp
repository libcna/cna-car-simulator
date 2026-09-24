#include "CarSim/Render/WetReflections.hpp"

#include <gtest/gtest.h>

using namespace CarSim::Render;
using Microsoft::Xna::Framework::Vector3;

TEST(WetReflections, AStreakRunsFromUnderTheLampTowardsTheViewer)
{
    const auto flat = [](float, float) { return 0.0f; };
    ReflectedLight lamp{Vector3(0.0f, 7.0f, 0.0f), Vector3(1.0f, 0.9f, 0.7f), 1.0f};
    ReflectionStreak s;
    ASSERT_TRUE(WetReflections::Streak(lamp, Vector3(0.0f, 1.5f, 40.0f), 1.0f, flat, s));
    const float nearZ = 0.5f * (s.corners[0].Z + s.corners[1].Z);
    const float farZ = 0.5f * (s.corners[2].Z + s.corners[3].Z);
    EXPECT_NEAR(nearZ, 0.0f, 1e-3f) << "starts under the lamp";
    EXPECT_GT(farZ, 10.0f) << "a high lamp throws a long streak";
    EXPECT_LT(farZ, 40.0f) << "never past the viewer";
    for (const auto& c : s.corners) EXPECT_NEAR(c.Y, 0.04f, 1e-3f) << "lies on the road";

    ReflectedLight headlamp{Vector3(0.0f, 0.65f, 0.0f), Vector3(1.0f, 1.0f, 1.0f), 1.0f};
    ReflectionStreak h;
    ASSERT_TRUE(WetReflections::Streak(headlamp, Vector3(0.0f, 1.5f, 40.0f), 1.0f, flat, h));
    EXPECT_LT(0.5f * (h.corners[2].Z + h.corners[3].Z), farZ) << "a low headlamp throws a shorter one";
}

TEST(WetReflections, NothingOnADryRoadOrOutOfRange)
{
    const auto flat = [](float, float) { return 0.0f; };
    ReflectedLight lamp{Vector3(0.0f, 7.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f), 1.0f};
    ReflectionStreak s;
    EXPECT_FALSE(WetReflections::Streak(lamp, Vector3(0.0f, 1.5f, 40.0f), 0.0f, flat, s));
    EXPECT_FALSE(WetReflections::Streak(lamp, Vector3(0.0f, 1.5f, 500.0f), 1.0f, flat, s));
    ASSERT_TRUE(WetReflections::Streak(lamp, Vector3(0.0f, 1.5f, 40.0f), 0.4f, flat, s));
    ReflectionStreak soaked;
    ASSERT_TRUE(WetReflections::Streak(lamp, Vector3(0.0f, 1.5f, 40.0f), 1.0f, flat, soaked));
    EXPECT_LT(s.alpha, soaked.alpha);
}
