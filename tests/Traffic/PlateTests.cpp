#include "CarSim/Traffic/PlateGenerator.hpp"

#include <gtest/gtest.h>

#include <set>

using namespace CarSim::Traffic;

TEST(PlateGenerator, GeneratesValidUniqueStandardPlates)
{
    PlateGenerator gen(42);
    gen.SetElectricShare(0.0f);
    std::set<std::string> seen;
    for (int i = 0; i < 2000; ++i) {
        const std::string plate = gen.Next();
        ASSERT_EQ(plate.size(), 8u) << plate;
        EXPECT_EQ(plate[3], ' ');
        EXPECT_TRUE(PlateGenerator::IsValidStandard(plate)) << plate;
        EXPECT_TRUE(seen.insert(plate).second) << "duplicate " << plate;
        for (const char c : plate) {
            EXPECT_TRUE(c != 'G' && c != 'O' && c != 'Q' && c != 'W') << plate;
        }
    }
    EXPECT_EQ(gen.IssuedCount(), 2000u);
}

TEST(PlateGenerator, IsDeterministicPerSeed)
{
    PlateGenerator a(9);
    PlateGenerator b(9);
    PlateGenerator c(10);
    EXPECT_EQ(a.Next(), b.Next());
    EXPECT_EQ(a.Next(), b.Next());
    EXPECT_NE(PlateGenerator(9).Next(), c.Next());
}

TEST(PlateGenerator, ValidatorRejectsBadPlates)
{
    EXPECT_TRUE(PlateGenerator::IsValidStandard("1A2 3456"));
    EXPECT_TRUE(PlateGenerator::IsValidStandard("9TX0001"));
    EXPECT_FALSE(PlateGenerator::IsValidStandard("0A2 3456"));   // first digit 1-9
    EXPECT_FALSE(PlateGenerator::IsValidStandard("1G2 3456"));   // G is not a region
    EXPECT_FALSE(PlateGenerator::IsValidStandard("1AO 3456"));   // O never used
    EXPECT_FALSE(PlateGenerator::IsValidStandard("1A2 345"));
    EXPECT_FALSE(PlateGenerator::IsValidStandard("1A2 34X6"));
    EXPECT_TRUE(PlateGenerator::IsValidElectric("EL0 12AB"));
    EXPECT_FALSE(PlateGenerator::IsValidElectric("EL0 12OB"));
    EXPECT_EQ(PlateGenerator::Compact("1A2 3456"), "1A23456");
    EXPECT_EQ(PlateGenerator::Format("1A23456"), "1A2 3456");
}

TEST(PlateGenerator, RegionWeightsCoverAllFourteenRegions)
{
    PlateGenerator gen(3);
    gen.SetElectricShare(0.0f);
    std::set<char> letters;
    for (int i = 0; i < 3000; ++i) {
        letters.insert(gen.Next()[1]);
    }
    EXPECT_EQ(letters.size(), 14u);
    EXPECT_EQ(PlateGenerator::Regions().size(), 14u);
}

TEST(PlateGenerator, ElectricShareProducesElPlates)
{
    PlateGenerator gen(5);
    gen.SetElectricShare(1.0f);
    for (int i = 0; i < 20; ++i) {
        const std::string plate = gen.Next();
        EXPECT_TRUE(PlateGenerator::IsValidElectric(plate)) << plate;
    }
}
