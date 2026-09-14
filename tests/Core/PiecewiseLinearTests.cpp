#include "CarSim/Core/PiecewiseLinear.hpp"

#include <gtest/gtest.h>

using CarSim::Core::PiecewiseLinear;

TEST(PiecewiseLinear, EmptyEvaluatesToZero)
{
    const PiecewiseLinear curve;
    EXPECT_FLOAT_EQ(curve.Evaluate(3.0f), 0.0f);
    EXPECT_FLOAT_EQ(curve.MaxY(), 0.0f);
}

TEST(PiecewiseLinear, InterpolatesAndClamps)
{
    const PiecewiseLinear curve({{0.0f, 0.0f}, {10.0f, 100.0f}, {20.0f, 50.0f}});
    EXPECT_FLOAT_EQ(curve.Evaluate(-5.0f), 0.0f);
    EXPECT_FLOAT_EQ(curve.Evaluate(5.0f), 50.0f);
    EXPECT_FLOAT_EQ(curve.Evaluate(10.0f), 100.0f);
    EXPECT_FLOAT_EQ(curve.Evaluate(15.0f), 75.0f);
    EXPECT_FLOAT_EQ(curve.Evaluate(25.0f), 50.0f);
    EXPECT_FLOAT_EQ(curve.MaxY(), 100.0f);
    EXPECT_FLOAT_EQ(curve.ArgMaxY(), 10.0f);
}

TEST(PiecewiseLinear, SortsUnorderedInputAndAddPoint)
{
    PiecewiseLinear curve({{10.0f, 1.0f}, {0.0f, 0.0f}});
    curve.AddPoint(5.0f, 10.0f);
    ASSERT_EQ(curve.Size(), 3u);
    EXPECT_FLOAT_EQ(curve.Points()[1].first, 5.0f);
    EXPECT_FLOAT_EQ(curve.Evaluate(2.5f), 5.0f);
}
