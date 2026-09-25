#include "CarSim/Render/VegetationGenerator.hpp"

#include <gtest/gtest.h>

using namespace CarSim;

TEST(VegetationGenerator, SeededSilhouettesShareOneAtlasWithoutChangingGeometry)
{
    const auto atlas = Render::VegetationGenerator::CardAtlasTexture(Map::TreeSpecies::Spruce, 100u);
    ASSERT_EQ(atlas.Width(), Render::VegetationGenerator::kAtlasWidth);
    ASSERT_EQ(atlas.Height(), Render::VegetationGenerator::kCardHeight);
    const int left = Render::VegetationGenerator::kAtlasPadding;
    const int right = left + Render::VegetationGenerator::kCardWidth + Render::VegetationGenerator::kAtlasPadding;
    int differingPixels = 0;
    for (int y = 0; y < atlas.Height(); ++y) {
        for (int x = 0; x < Render::VegetationGenerator::kCardWidth; ++x) {
            const auto& a = atlas.At(left + x, y);
            const auto& b = atlas.At(right + x, y);
            differingPixels += a.getRProperty() != b.getRProperty() || a.getGProperty() != b.getGProperty() ||
                               a.getBProperty() != b.getBProperty() || a.getAProperty() != b.getAProperty();
        }
    }
    EXPECT_GT(differingPixels, 1000) << "the second tree card should change the forest silhouette";
    EXPECT_EQ(atlas.At(left - 1, 256).getAProperty(), 0);
    EXPECT_EQ(atlas.At(right - 1, 256).getAProperty(), 0);

    Map::PlacedTree tree;
    tree.species = Map::TreeSpecies::Spruce;
    Render::MeshData even, odd;
    tree.seed = 0;
    Render::VegetationGenerator::AppendTree(tree, even);
    tree.seed = 1;
    Render::VegetationGenerator::AppendTree(tree, odd);
    ASSERT_EQ(even.vertices.size(), odd.vertices.size());
    ASSERT_EQ(even.TriangleCount(), odd.TriangleCount());
    ASSERT_FALSE(even.vertices.empty());
    for (std::size_t i = 0; i < even.vertices.size(); ++i) {
        EXPECT_FLOAT_EQ(even.vertices[i].position.X, odd.vertices[i].position.X);
        EXPECT_FLOAT_EQ(even.vertices[i].position.Y, odd.vertices[i].position.Y);
        EXPECT_FLOAT_EQ(even.vertices[i].position.Z, odd.vertices[i].position.Z);
        EXPECT_LT(even.vertices[i].uv.X, 0.5f);
        EXPECT_GT(odd.vertices[i].uv.X, 0.5f);
    }
}

TEST(VegetationGenerator, WinterAtlasFrostsFoliageWithoutChangingTrunksOrCardEdges)
{
    for (const auto species : {Map::TreeSpecies::Spruce, Map::TreeSpecies::Birch, Map::TreeSpecies::Bush}) {
        const auto summer = Render::VegetationGenerator::CardAtlasTexture(species, 113u);
        const auto winter = Render::VegetationGenerator::WinterAtlasTexture(summer, species, 113u);
        const auto half = Render::VegetationGenerator::BlendSeasonalAtlases(summer, winter, 0.5f);
        int brighterFoliage = 0, unchangedBark = 0, darkFoliage = 0;
        for (std::size_t i = 0; i < summer.Pixels().size(); ++i) {
            const auto& a = summer.Pixels()[i];
            const auto& b = winter.Pixels()[i];
            const auto& m = half.Pixels()[i];
            EXPECT_EQ(a.getAProperty(), b.getAProperty());
            EXPECT_EQ(a.getAProperty(), m.getAProperty());
            if (a.getAProperty() == 0) continue;
            const bool foliage = a.getGProperty() > a.getRProperty() + 9 &&
                                 a.getGProperty() > a.getBProperty() + 5;
            if (foliage) {
                brighterFoliage += b.getRProperty() > a.getRProperty() + 20;
                darkFoliage += b.getGProperty() < 170;
                EXPECT_GE(m.getRProperty(), a.getRProperty());
                EXPECT_LE(m.getRProperty(), b.getRProperty());
            } else {
                unchangedBark += a.getRProperty() == b.getRProperty() &&
                                 a.getGProperty() == b.getGProperty() && a.getBProperty() == b.getBProperty();
            }
        }
        EXPECT_GT(brighterFoliage, 1000);
        EXPECT_GT(darkFoliage, 1000) << "winter foliage must retain shaded depth";
        if (species != Map::TreeSpecies::Bush) {
            EXPECT_GT(unchangedBark, 100);
        }
        EXPECT_EQ(winter.At(Render::VegetationGenerator::kAtlasPadding - 1, 256).getAProperty(), 0);
    }
}
