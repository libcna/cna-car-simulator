#include "CarSim/Core/CommandLine.hpp"

#include <gtest/gtest.h>

#include <array>

using CarSim::Core::ParseCommandLine;

TEST(CommandLine, DefaultsWhenNoArguments)
{
    const std::array<const char*, 1> argv{"cna-car-simulator"};
    const auto result = ParseCommandLine(static_cast<int>(argv.size()), argv.data());
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.options.frames.has_value());
    EXPECT_FALSE(result.options.screenshotPath.has_value());
    EXPECT_EQ(result.options.width, 1280);
    EXPECT_EQ(result.options.height, 720);
    EXPECT_FALSE(result.options.fullscreen);
}

TEST(CommandLine, ParsesFramesScreenshotAndSize)
{
    const std::array<const char*, 9> argv{"sim", "--frames", "12", "--screenshot", "out.png",
                                          "--width", "640", "--height", "360"};
    const auto result = ParseCommandLine(static_cast<int>(argv.size()), argv.data());
    ASSERT_TRUE(result.ok()) << result.errors.front();
    ASSERT_TRUE(result.options.frames.has_value());
    EXPECT_EQ(*result.options.frames, 12);
    ASSERT_TRUE(result.options.screenshotPath.has_value());
    EXPECT_EQ(*result.options.screenshotPath, "out.png");
    EXPECT_EQ(result.options.width, 640);
    EXPECT_EQ(result.options.height, 360);
}

TEST(CommandLine, RejectsUnknownArgumentsAndBadNumbers)
{
    const std::array<const char*, 4> argv{"sim", "--bogus", "--frames", "zero"};
    const auto result = ParseCommandLine(static_cast<int>(argv.size()), argv.data());
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.errors.size(), 2u);
}

TEST(CommandLine, RejectsMissingValue)
{
    const std::array<const char*, 2> argv{"sim", "--screenshot"};
    const auto result = ParseCommandLine(static_cast<int>(argv.size()), argv.data());
    EXPECT_FALSE(result.ok());
}

TEST(CommandLine, ParsesBenchmarkJsonAndMirrorRate)
{
    const std::array<const char*, 5> argv{"sim", "--benchmark-json", "out.json", "--mirror-every", "2"};
    const auto result = ParseCommandLine(static_cast<int>(argv.size()), argv.data());
    ASSERT_TRUE(result.ok()) << result.errors.front();
    EXPECT_TRUE(result.options.benchmark);
    ASSERT_TRUE(result.options.benchmarkJsonPath.has_value());
    EXPECT_EQ(*result.options.benchmarkJsonPath, "out.json");
    ASSERT_TRUE(result.options.mirrorEvery.has_value());
    EXPECT_EQ(*result.options.mirrorEvery, 2);
}

TEST(CommandLine, StartsMapAndHelicopterForCaptures)
{
    const std::array<const char*, 3> argv{"sim", "--map-overlay", "--flight"};
    const auto result = ParseCommandLine(static_cast<int>(argv.size()), argv.data());
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.options.showMapOverlay);
    EXPECT_TRUE(result.options.startFlight);
}

TEST(CommandLine, ParsesTheClock)
{
    {
        const std::array<const char*, 5> argv{"sim", "--time", "21:15", "--time-scale", "0"};
        const auto result = ParseCommandLine(static_cast<int>(argv.size()), argv.data());
        ASSERT_TRUE(result.ok()) << result.errors.front();
        ASSERT_TRUE(result.options.timeOfDay.has_value());
        EXPECT_NEAR(*result.options.timeOfDay, 21.25f, 1e-4f);
        ASSERT_TRUE(result.options.timeScale.has_value());
        EXPECT_EQ(*result.options.timeScale, 0.0f);
    }
    {   // Decimal hours and wrapping past midnight.
        const std::array<const char*, 3> argv{"sim", "--time", "25.5"};
        const auto result = ParseCommandLine(static_cast<int>(argv.size()), argv.data());
        ASSERT_TRUE(result.ok()) << result.errors.front();
        ASSERT_TRUE(result.options.timeOfDay.has_value());
        EXPECT_NEAR(*result.options.timeOfDay, 1.5f, 1e-4f);
    }
    for (const char* bad : {"noon", "12:75", "12:", "8:30pm"}) {
        const std::array<const char*, 3> argv{"sim", "--time", bad};
        const auto result = ParseCommandLine(static_cast<int>(argv.size()), argv.data());
        EXPECT_FALSE(result.ok()) << bad;
    }
    for (const char* bad : {"-1", "fast", "5000"}) {
        const std::array<const char*, 3> argv{"sim", "--time-scale", bad};
        const auto result = ParseCommandLine(static_cast<int>(argv.size()), argv.data());
        EXPECT_FALSE(result.ok()) << bad;
    }
}

TEST(CommandLine, ParsesTheWeather)
{
    const std::array<const char*, 3> argv{"sim", "--weather", "rain"};
    const auto result = ParseCommandLine(static_cast<int>(argv.size()), argv.data());
    ASSERT_TRUE(result.ok()) << result.errors.front();
    ASSERT_TRUE(result.options.weather.has_value());
    EXPECT_EQ(*result.options.weather, "rain");

    const std::array<const char*, 3> bad{"sim", "--weather", "hurricane"};
    EXPECT_FALSE(ParseCommandLine(static_cast<int>(bad.size()), bad.data()).ok());
}

TEST(CommandLine, ParsesTheWipers)
{
    const std::array<const char*, 3> argv{"sim", "--wipers", "slow"};
    const auto result = ParseCommandLine(static_cast<int>(argv.size()), argv.data());
    ASSERT_TRUE(result.ok()) << result.errors.front();
    EXPECT_EQ(result.options.wiperSteps, 2);
    const std::array<const char*, 3> bad{"sim", "--wipers", "turbo"};
    EXPECT_FALSE(ParseCommandLine(static_cast<int>(bad.size()), bad.data()).ok());
}
