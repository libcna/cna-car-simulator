#include "CarSim/Render/ProceduralTextures.hpp"

#include <algorithm>
#include <cmath>

namespace CarSim::Render::Textures
{
    namespace
    {
        Color ToColor(const Rgb& c, const float alpha = 1.0f)
        {
            const auto b = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
            return Color(b(c.r), b(c.g), b(c.b), b(alpha));
        }

        float Clamp01(const float v) { return std::clamp(v, 0.0f, 1.0f); }
    }

    Image Solid(const int size, const Color& color)
    {
        return Image(size, size, color);
    }

    Image Checker(const int size, const int cells, const Color& a, const Color& b)
    {
        Image img(size, size);
        const int cell = std::max(1, size / std::max(1, cells));
        img.Generate([&](int x, int y, float, float) { return ((x / cell) + (y / cell)) % 2 == 0 ? a : b; });
        return img;
    }

    Image Asphalt(const int size, const std::uint32_t seed)
    {
        Image img(size, size);
        const float period = 16.0f;
        img.Generate([&](int, int, float u, float v) {
            const float coarse = Noise::Fbm(u * period, v * period, 16, 4, 0.5f, seed);
            const float fine = Noise::Value(u * 256.0f, v * 256.0f, 256, seed + 11);
            const float cell = Noise::Cellular(u * 96.0f, v * 96.0f, 96, seed + 23);
            float g = 0.30f + (coarse - 0.5f) * 0.10f + (fine - 0.5f) * 0.12f;
            g += (0.5f - cell) * 0.05f;
            const float patch = Noise::Fbm(u * 3.0f, v * 3.0f, 3, 2, 0.5f, seed + 41);
            g *= 0.9f + patch * 0.2f;
            return ToColor({g, g, g * 0.98f});
        });
        return img;
    }

    Image Gravel(const int size, const std::uint32_t seed)
    {
        Image img(size, size);
        img.Generate([&](int, int, float u, float v) {
            const float cell = Noise::Cellular(u * 64.0f, v * 64.0f, 64, seed);
            const float fine = Noise::Value(u * 200.0f, v * 200.0f, 200, seed + 5);
            const float tone = Noise::Fbm(u * 8.0f, v * 8.0f, 8, 3, 0.5f, seed + 9);
            const float stone = Clamp01(1.0f - cell * 1.8f);
            const float g = 0.42f + stone * 0.18f + (fine - 0.5f) * 0.10f + (tone - 0.5f) * 0.12f;
            return ToColor({g * 1.02f, g * 0.97f, g * 0.88f});
        });
        return img;
    }

    Image Grass(const int size, const std::uint32_t seed)
    {
        Image img(size, size);
        img.Generate([&](int, int, float u, float v) {
            const float clumps = Noise::Fbm(u * 12.0f, v * 12.0f, 12, 4, 0.55f, seed);
            const float blades = Noise::Value(u * 300.0f, v * 90.0f, 300, seed + 3);
            const float dry = Noise::Fbm(u * 3.0f, v * 3.0f, 3, 2, 0.5f, seed + 17);
            const Rgb green{0.27f, 0.36f, 0.15f};
            const Rgb light{0.46f, 0.52f, 0.24f};
            const Rgb hay{0.55f, 0.50f, 0.28f};
            const float blades2 = Noise::Value(u * 90.0f, v * 300.0f, 300, seed + 7);
            Rgb c = Lerp(green, light, Clamp01(clumps * 0.7f + (blades - 0.5f) * 0.5f + (blades2 - 0.5f) * 0.4f));
            c = Lerp(c, hay, Clamp01((dry - 0.5f) * 2.2f));
            return ToColor(c);
        });
        return img;
    }

    Image Soil(const int size, const std::uint32_t seed)
    {
        Image img(size, size);
        img.Generate([&](int, int, float u, float v) {
            const float rows = 0.5f + 0.5f * std::sin(v * 6.2831853f * 24.0f);
            const float n = Noise::Fbm(u * 10.0f, v * 10.0f, 10, 4, 0.5f, seed);
            const float g = 0.28f + n * 0.14f + rows * 0.06f;
            return ToColor({g * 1.15f, g * 0.95f, g * 0.72f});
        });
        return img;
    }

    Image PavingSlabs(const int size, const std::uint32_t seed)
    {
        Image img(size, size);
        const int slabs = 4;
        img.Generate([&](int, int, float u, float v) {
            const float fu = u * slabs;
            const float fv = v * slabs;
            const float gu = std::fabs(fu - std::round(fu));
            const float gv = std::fabs(fv - std::round(fv));
            const float joint = std::min(gu, gv);
            const int ix = static_cast<int>(fu);
            const int iy = static_cast<int>(fv);
            const float slabTone = Noise::Hash(ix, iy, seed) * 0.12f;
            const float grain = Noise::Value(u * 180.0f, v * 180.0f, 180, seed + 2);
            float g = 0.58f + slabTone + (grain - 0.5f) * 0.08f;
            if (joint < 0.03f) {
                g *= 0.62f;
            }
            return ToColor({g, g * 0.99f, g * 0.95f});
        });
        return img;
    }

    Image Plaster(const int size, const Rgb& base, const std::uint32_t seed)
    {
        Image img(size, size);
        img.Generate([&](int, int, float u, float v) {
            const float grain = Noise::Value(u * 220.0f, v * 220.0f, 220, seed);
            const float stains = Noise::Fbm(u * 4.0f, v * 4.0f, 4, 3, 0.5f, seed + 8);
            const float shade = 0.94f + (grain - 0.5f) * 0.10f - Clamp01(stains - 0.6f) * 0.3f;
            return ToColor(base * shade);
        });
        return img;
    }

    Image RoofTiles(const int size, const Rgb& base, const std::uint32_t seed)
    {
        Image img(size, size);
        const int rows = 12;
        const int cols = 8;
        img.Generate([&](int, int, float u, float v) {
            const float fv = v * rows;
            const int row = static_cast<int>(fv);
            const float offset = (row % 2) * 0.5f;
            const float fu = u * cols + offset;
            const int col = static_cast<int>(std::floor(fu));
            const float lu = fu - std::floor(fu);
            const float lv = fv - std::floor(fv);
            const float curve = std::sqrt(std::max(0.0f, 1.0f - (lu - 0.5f) * (lu - 0.5f) * 4.0f));
            float shade = 1.0f;
            if (lv > 0.85f + curve * 0.1f) shade *= 0.55f;
            if (lv < 0.08f) shade *= 0.75f;
            shade *= 0.85f + 0.15f * (1.0f - std::fabs(lu - 0.5f) * 2.0f);
            const float tileTone = 0.9f + Noise::Hash(col, row, seed) * 0.2f;
            const float moss = Clamp01(Noise::Fbm(u * 5.0f, v * 5.0f, 5, 3, 0.5f, seed + 3) - 0.6f) * 2.0f;
            Rgb c = base * (shade * tileTone);
            c = Lerp(c, {0.35f, 0.38f, 0.22f}, moss * 0.6f);
            return ToColor(c);
        });
        return img;
    }

    Image Bark(const int size, const std::uint32_t seed)
    {
        Image img(size, size);
        img.Generate([&](int, int, float u, float v) {
            const float ridges = Noise::Fbm(u * 24.0f, v * 4.0f, 24, 4, 0.55f, seed);
            const float fine = Noise::Value(u * 200.0f, v * 60.0f, 200, seed + 1);
            const float g = 0.22f + ridges * 0.25f + (fine - 0.5f) * 0.08f;
            return ToColor({g * 1.1f, g * 0.95f, g * 0.8f});
        });
        return img;
    }

    Image LeafCluster(const int size, const Rgb& leaf, const bool conifer, const std::uint32_t seed)
    {
        Image img(size, size, Color(0, 0, 0, 0));
        const int count = conifer ? 160 : 90;
        for (int i = 0; i < count; ++i) {
            const float cx = Noise::Hash(i, 1, seed) * static_cast<float>(size);
            const float cy = Noise::Hash(i, 2, seed) * static_cast<float>(size);
            const float tone = 0.75f + Noise::Hash(i, 3, seed) * 0.5f;
            const Color c = ToColor(leaf * tone);
            if (conifer) {
                const float angle = Noise::Hash(i, 4, seed) * 6.2831853f;
                const float len = static_cast<float>(size) * 0.06f;
                for (int s = 0; s < 12; ++s) {
                    const float t = static_cast<float>(s) / 11.0f;
                    img.FillCircle(cx + std::cos(angle) * len * (t - 0.5f), cy + std::sin(angle) * len * (t - 0.5f),
                                   static_cast<float>(size) * 0.008f, c);
                }
            } else {
                img.FillCircle(cx, cy, static_cast<float>(size) * (0.035f + Noise::Hash(i, 5, seed) * 0.03f), c);
            }
        }
        Image faded(size, size, Color(0, 0, 0, 0));
        faded.Generate([&](int x, int y, float u, float v) {
            const Color& c = img.At(x, y);
            const float d = std::sqrt((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f)) * 2.0f;
            const float edge = Clamp01((1.0f - d) * 4.0f);
            const int a = static_cast<int>(static_cast<float>(c.getAProperty()) * edge);
            return Color(static_cast<int>(c.getRProperty()), static_cast<int>(c.getGProperty()),
                         static_cast<int>(c.getBProperty()), a);
        });
        return faded;
    }

    std::vector<Image> SkyCubeFaces(const int size, const Rgb& zenith, const Rgb& horizon, const Rgb& ground,
                                    const Microsoft::Xna::Framework::Vector3& toSun, const float sunSharpness)
    {
        std::vector<Image> faces;
        faces.reserve(6);
        for (int face = 0; face < 6; ++face) {
            Image img(size, size);
            img.Generate([&](int, int, float u, float v) {
                const float a = u * 2.0f - 1.0f;
                const float b = v * 2.0f - 1.0f;
                float dx = 0, dy = 0, dz = 0;
                switch (face) {
                    case 0: dx = 1; dy = -b; dz = -a; break;
                    case 1: dx = -1; dy = -b; dz = a; break;
                    case 2: dx = a; dy = 1; dz = b; break;
                    case 3: dx = a; dy = -1; dz = -b; break;
                    case 4: dx = a; dy = -b; dz = 1; break;
                    default: dx = -a; dy = -b; dz = -1; break;
                }
                const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
                const float y = dy / len;
                Rgb c;
                if (y >= 0.0f) {
                    c = Lerp(horizon, zenith, std::pow(y, 0.6f));
                } else {
                    c = Lerp(horizon, ground, Clamp01(-y * 3.0f));
                }
                const float cosSun = (dx * toSun.X + dy * toSun.Y + dz * toSun.Z) / len;
                const float glint = cosSun > 0.0f ? std::pow(cosSun, sunSharpness) : 0.0f;
                return ToColor(c, glint);
            });
            faces.push_back(std::move(img));
        }
        return faces;
    }

    Image CloudLayer(const int size, const std::uint32_t seed)
    {
        Image img(size, size, Color(255, 255, 255, 0));
        img.Generate([&](int, int, float u, float v) {
            const float n = Noise::Fbm(u * 5.0f, v * 5.0f, 5, 6, 0.55f, seed);
            const float detail = Noise::Fbm(u * 24.0f, v * 24.0f, 24, 3, 0.5f, seed + 3);
            const float coverage = Clamp01((n - 0.50f) * 3.6f + (detail - 0.5f) * 0.5f);
            const float shade = 0.78f + 0.24f * Clamp01((n - 0.48f) * 3.0f);
            return ToColor({shade, shade, shade * 1.03f}, coverage * 0.95f);
        });
        return img;
    }

    Image InteriorGrain(const int size, const Rgb& base, const std::uint32_t seed)
    {
        Image img(size, size);
        img.Generate([&](int, int, float u, float v) {
            const float grain = Noise::Value(u * 160.0f, v * 160.0f, 160, seed);
            return ToColor(base * (0.92f + (grain - 0.5f) * 0.16f));
        });
        return img;
    }
}
