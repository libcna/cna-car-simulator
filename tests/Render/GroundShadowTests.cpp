// Baked ground shadows: buildings and trees darken the ground on the side away from the sun.
#include "CarSim/Map/MapDocument.hpp"
#include "CarSim/Map/MapWorld.hpp"
#include "CarSim/Render/GroundShadowBaker.hpp"
#include "CarSim/Render/LightingRig.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace CarSim;
using namespace CarSim::Render;
using Microsoft::Xna::Framework::Vector3;

TEST(GroundShadows, ShadowOffsetFollowsTheSunElevation)
{
    Vector3 light(1.0f, -1.0f, 0.0f);
    light.Normalize();
    const Vector3 o = GroundShadows::ShadowOffset(light, 2.0f);
    EXPECT_NEAR(o.X, 2.0f, 1e-5f);   // 45 degrees: as long as the height, away from the sun
    EXPECT_NEAR(o.Z, 0.0f, 1e-5f);
    EXPECT_NEAR(GroundShadows::ShadowOffset(Vector3(0.0f, -1.0f, 0.0f), 3.0f).Length(), 0.0f, 1e-5f);   // zenith sun
}

TEST(GroundShadows, BuildingsShadeTheGroundAwayFromTheSun)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(Map::MapDirectory(CARSIM_TEST_CONTENT_DIR, "lipova"), errors);
    ASSERT_TRUE(world);
    ASSERT_FALSE(world->Objects().Buildings().empty());
    const LightingRig rig;
    const auto& terrain = world->Terrain();
    const Image map = GroundShadows::Bake(*world, rig.sunDirection, 2 * terrain.Columns(), 2 * terrain.Rows());
    EXPECT_EQ(map.Width(), 2 * terrain.Columns());
    if (const char* dump = std::getenv("CARSIM_DUMP_DIR")) {
        // Inspection aid: binary PGM of the shadow map.
        std::FILE* f = std::fopen((std::string(dump) + "/shadow.pgm").c_str(), "wb");
        if (f) {
            std::fprintf(f, "P5\n%d %d\n255\n", map.Width(), map.Height());
            for (int y = 0; y < map.Height(); ++y) {
                for (int x = 0; x < map.Width(); ++x) {
                    const unsigned char v = static_cast<unsigned char>(map.At(x, y).getRProperty());
                    std::fwrite(&v, 1, 1, f);
                }
            }
            std::fclose(f);
        }
    }

    // The first tree of the sample map (a large linden at -30, 30): its crown shadow centre lies
    // about 11 m north-west of the trunk and reads clearly darker than the trunk's sun side.
    {
        const auto& t = world->Objects().Trees().front();
        const Vector3 offset = GroundShadows::ShadowOffset(rig.sunDirection, std::max(0.5f, t.Height() - t.CrownRadius()));
        const float shade = GroundShadows::Sample(map, *world, t.position.X + offset.X, t.position.Z + offset.Z);
        EXPECT_LT(shade, 0.7f) << "offset " << offset.X << " " << offset.Z;
        EXPECT_GT(offset.Length(), 5.0f);
    }

    // Most of the map is lit, some of it is shaded.
    double sum = 0.0;
    int dark = 0;
    for (int y = 0; y < map.Height(); ++y) {
        for (int x = 0; x < map.Width(); ++x) {
            const int v = static_cast<int>(map.At(x, y).getRProperty());
            sum += v;
            if (v < 200) ++dark;
        }
    }
    const double mean = sum / (static_cast<double>(map.Width()) * static_cast<double>(map.Height()));
    EXPECT_GT(mean, 200.0);
    EXPECT_LT(mean, 255.0);
    EXPECT_GT(dark, 100);

    // Per building: the ground just beyond the shadow-side wall is darker than the ground just
    // outside the sun-side wall (which may still be shaded by a neighbour, so count a majority).
    int correct = 0, total = 0;
    Vector3 lightXZ(rig.sunDirection.X, 0.0f, rig.sunDirection.Z);
    lightXZ.Normalize();
    for (const auto& b : world->Objects().Buildings()) {
        const float reach = std::max(b.halfWidth, b.halfDepth) + 1.5f;
        const Vector3 shade = b.position + lightXZ * reach;
        const Vector3 sunSide = b.position - lightXZ * (reach + 4.0f);
        const float s0 = GroundShadows::Sample(map, *world, shade.X, shade.Z);
        const float s1 = GroundShadows::Sample(map, *world, sunSide.X, sunSide.Z);
        if (s0 < s1 - 0.05f) ++correct;
        ++total;
    }
    EXPECT_GT(correct * 10, total * 6) << correct << " of " << total;
}
