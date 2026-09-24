#include "CarSim/Render/VegetationGenerator.hpp"

#include "CarSim/Core/Noise.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace CarSim::Render
{
    using Map::TreeSpecies;
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;

    namespace
    {
        Color ToColor(const Rgb& c, const float a = 1.0f)
        {
            return Color(static_cast<int>(std::clamp(c.r, 0.0f, 1.0f) * 255.0f), static_cast<int>(std::clamp(c.g, 0.0f, 1.0f) * 255.0f),
                         static_cast<int>(std::clamp(c.b, 0.0f, 1.0f) * 255.0f), static_cast<int>(std::clamp(a, 0.0f, 1.0f) * 255.0f));
        }

        void Blob(Image& img, const float cx, const float cy, const float r, const Rgb& dark, const Rgb& light, const unsigned seed)
        {
            // Irregular disc: radius modulated by angle noise, shaded from bottom-left (dark) to top-right (light).
            const int x0 = std::max(0, static_cast<int>(cx - r * 1.3f));
            const int x1 = std::min(img.Width() - 1, static_cast<int>(cx + r * 1.3f));
            const int y0 = std::max(0, static_cast<int>(cy - r * 1.3f));
            const int y1 = std::min(img.Height() - 1, static_cast<int>(cy + r * 1.3f));
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const float dx = static_cast<float>(x) + 0.5f - cx;
                    const float dy = static_cast<float>(y) + 0.5f - cy;
                    const float d = std::sqrt(dx * dx + dy * dy);
                    const float angle = std::atan2(dy, dx);
                    const float wobble = 0.78f + 0.3f * Core::Noise::ValueOpen(std::cos(angle) * 2.5f + 10.0f, std::sin(angle) * 2.5f + 10.0f, seed);
                    if (d > r * wobble) continue;
                    const float shade = std::clamp(0.5f + 0.5f * ((-dx * 0.6f + -dy) / std::max(1.0f, r)), 0.0f, 1.0f);
                    const float speck = Core::Noise::ValueOpen(static_cast<float>(x) * 0.35f, static_cast<float>(y) * 0.35f, seed + 3u);
                    const Rgb c = Lerp(dark, light, std::clamp(shade * 0.8f + speck * 0.35f, 0.0f, 1.0f));
                    img.Set(x, y, c, 1.0f);
                }
            }
        }

        void Trunk(Image& img, const float xCentre, const float yTop, const float yBottom, const float wTop, const float wBottom, const Rgb& colour,
                   const Rgb& shade, const unsigned seed)
        {
            for (int y = static_cast<int>(yTop); y < static_cast<int>(yBottom) && y < img.Height(); ++y) {
                const float t = (static_cast<float>(y) - yTop) / std::max(1.0f, yBottom - yTop);
                const float w = wTop + (wBottom - wTop) * t;
                for (int x = static_cast<int>(xCentre - w * 0.5f); x <= static_cast<int>(xCentre + w * 0.5f); ++x) {
                    if (x < 0 || x >= img.Width()) continue;
                    const float u = (static_cast<float>(x) - xCentre) / std::max(1.0f, w * 0.5f);
                    const float grain = Core::Noise::ValueOpen(static_cast<float>(x) * 0.5f, static_cast<float>(y) * 0.08f, seed);
                    const Rgb c = Lerp(shade, colour, std::clamp(0.6f - u * 0.4f + grain * 0.3f, 0.0f, 1.0f));
                    img.Set(x, y, c, 1.0f);
                }
            }
        }
    }

    Image VegetationGenerator::CardTexture(const TreeSpecies species, const int width, const int height, const unsigned seed)
    {
        Image img(width, height, Color(0, 0, 0, 0));
        const float W = static_cast<float>(width);
        const float H = static_cast<float>(height);
        const float cx = W * 0.5f;
        switch (species) {
            case TreeSpecies::Spruce: {
                Trunk(img, cx, H * 0.55f, H, W * 0.035f, W * 0.06f, Rgb::FromBytes(96, 70, 52), Rgb::FromBytes(52, 38, 28), seed);
                // Layered triangular crown from the tip down. The second seeded silhouette
                // is narrower and lifts the lowest boughs to expose more trunk at the edge.
                const bool narrow = (seed & 1u) == 0u;
                const int layers = narrow ? 8 : 9;
                for (int i = 0; i < layers; ++i) {
                    const float t = static_cast<float>(i) / static_cast<float>(layers - 1);
                    const float yTop = H * (0.02f + 0.86f * t * 0.9f);
                    const float layerH = H * (narrow ? 0.14f : 0.16f);
                    const float halfW = W * (narrow ? 0.05f + 0.34f * std::pow(t, 0.90f)
                                                    : 0.06f + 0.42f * std::pow(t, 0.85f));
                    const Rgb dark = Rgb::FromBytes(22, 46, 26);
                    const Rgb light = Rgb::FromBytes(64, 98, 50);
                    for (int k = 0; k < 7; ++k) {
                        const float u = (static_cast<float>(k) + 0.5f) / 7.0f - 0.5f;
                        const float bx = cx + u * halfW * 1.9f;
                        const float by = yTop + layerH * (0.55f + 0.35f * std::fabs(u) * 2.0f);
                        const float r = layerH * (0.34f + 0.16f * (1.0f - std::fabs(u) * 2.0f));
                        Blob(img, bx, by, r, dark, light, seed + static_cast<unsigned>(i * 13 + k));
                    }
                    img.FillTriangle(cx - halfW, yTop + layerH, cx + halfW, yTop + layerH, cx, yTop, ToColor(Lerp(dark, light, 0.35f)));
                }
                break;
            }
            case TreeSpecies::Bush: {
                // Shrub: a dense cluster of small crowns in the lower two thirds of the card, no trunk.
                const Rgb dark = Rgb::FromBytes(24, 50, 22);
                const Rgb light = Rgb::FromBytes(92, 134, 56);
                for (int k = 0; k < 18; ++k) {
                    const float bx = cx + (Core::Noise::Hash(k, 1, seed) - 0.5f) * W * 0.70f;
                    const float by = H * (0.42f + 0.48f * Core::Noise::Hash(k, 2, seed));
                    const float r = W * (0.11f + 0.09f * Core::Noise::Hash(k, 3, seed));
                    Blob(img, bx, by, r, dark, light, seed + static_cast<unsigned>(k * 7));
                }
                break;
            }
            case TreeSpecies::Pine: {
                Trunk(img, cx, H * 0.30f, H, W * 0.03f, W * 0.07f, Rgb::FromBytes(150, 96, 62), Rgb::FromBytes(72, 44, 30), seed);
                const Rgb dark = Rgb::FromBytes(26, 50, 30);
                const Rgb light = Rgb::FromBytes(78, 114, 58);
                for (int k = 0; k < 9; ++k) {
                    const float a = Core::Noise::Hash(k, 1, seed);
                    const float bx = cx + (a - 0.5f) * W * 0.7f;
                    const float by = H * (0.10f + 0.22f * Core::Noise::Hash(k, 2, seed));
                    const float r = W * (0.14f + 0.12f * Core::Noise::Hash(k, 3, seed));
                    Blob(img, bx, by, r, dark, light, seed + static_cast<unsigned>(k * 7));
                }
                break;
            }
            case TreeSpecies::Birch: {
                Trunk(img, cx, H * 0.25f, H, W * 0.025f, W * 0.05f, Rgb::FromBytes(236, 236, 228), Rgb::FromBytes(120, 120, 116), seed);
                for (int k = 0; k < 24; ++k) {
                    const float y = H * (0.3f + 0.7f * Core::Noise::Hash(k, 5, seed));
                    img.FillRect(static_cast<int>(cx - W * 0.02f), static_cast<int>(y), static_cast<int>(cx + W * 0.015f), static_cast<int>(y) + 2, Color(40, 40, 40, 255));
                }
                const Rgb dark = Rgb::FromBytes(62, 102, 44);
                const Rgb light = Rgb::FromBytes(138, 176, 84);
                for (int k = 0; k < 18; ++k) {
                    const float bx = cx + (Core::Noise::Hash(k, 1, seed) - 0.5f) * W * 0.75f;
                    const float by = H * (0.06f + 0.42f * Core::Noise::Hash(k, 2, seed));
                    const float r = W * (0.10f + 0.10f * Core::Noise::Hash(k, 3, seed));
                    Blob(img, bx, by, r, dark, light, seed + static_cast<unsigned>(k * 5));
                }
                break;
            }
            default: {
                // Broadleaf crowns: linden/maple round, oak broad, beech tall.
                const bool broad = species == TreeSpecies::Oak;
                const bool tall = species == TreeSpecies::Beech;
                Trunk(img, cx, H * 0.55f, H, W * 0.05f, W * 0.10f, Rgb::FromBytes(110, 84, 60), Rgb::FromBytes(54, 40, 30), seed);
                Rgb dark = Rgb::FromBytes(30, 64, 28);
                Rgb light = Rgb::FromBytes(104, 150, 62);
                if (species == TreeSpecies::Maple) { dark = Rgb::FromBytes(50, 84, 28); light = Rgb::FromBytes(150, 178, 68); }
                if (species == TreeSpecies::Beech) { dark = Rgb::FromBytes(30, 60, 26); light = Rgb::FromBytes(92, 140, 56); }
                const int blobs = 40;
                for (int k = 0; k < blobs; ++k) {
                    const float a = Core::Noise::Hash(k, 1, seed) * 2.0f * std::numbers::pi_v<float>;
                    const float rr = std::sqrt(Core::Noise::Hash(k, 2, seed));
                    const float ex = broad ? 0.46f : (tall ? 0.34f : 0.40f);
                    const float ey = tall ? 0.36f : (broad ? 0.26f : 0.30f);
                    const float bx = cx + std::cos(a) * rr * W * ex;
                    const float by = H * (tall ? 0.36f : 0.33f) + std::sin(a) * rr * H * ey;
                    const float r = W * (0.09f + 0.08f * Core::Noise::Hash(k, 3, seed));
                    Blob(img, bx, by, r, dark, light, seed + static_cast<unsigned>(k * 11));
                }
                break;
            }
        }
        // Crown underside in shade: darken towards the bottom of the card so the foliage reads lit
        // from above rather than as a flat cut-out.
        for (int y = 0; y < height; ++y) {
            const float t = static_cast<float>(y) / std::max(1.0f, H - 1.0f);
            const float k = 0.70f + 0.30f * std::pow(1.0f - t, 0.8f);
            for (int x = 0; x < width; ++x) {
                Color& c = img.At(x, y);
                if (c.getAProperty() == 0) continue;
                c = Color(static_cast<int>(c.getRProperty() * k), static_cast<int>(c.getGProperty() * k), static_cast<int>(c.getBProperty() * k),
                          static_cast<int>(c.getAProperty()));
            }
        }
        return img;
    }

    Image VegetationGenerator::CardAtlasTexture(const TreeSpecies species, const unsigned seed)
    {
        Image atlas(kAtlasWidth, kCardHeight, Color(0, 0, 0, 0));
        for (int variant = 0; variant < 2; ++variant) {
            // Keep the original first silhouette; vary the second with a separate seed. The
            // 16-pixel gutters let the uploader's mip chain filter each card independently.
            const Image card = CardTexture(species, kCardWidth, kCardHeight,
                                           seed + static_cast<unsigned>(variant) * 2017u);
            const int left = kAtlasPadding + variant * (kCardWidth + kAtlasPadding);
            for (int y = 0; y < kCardHeight; ++y) {
                for (int x = 0; x < kCardWidth; ++x) atlas.At(left + x, y) = card.At(x, y);
            }
        }
        return atlas;
    }

    void VegetationGenerator::AppendTree(const Map::PlacedTree& tree, MeshData& mesh)
    {
        const float h = tree.Height();
        const float halfW = std::max(tree.CrownRadius() * 1.15f, h * 0.22f);
        const float shade = 0.72f + 0.28f * Core::Noise::Hash(static_cast<int>(tree.seed), 9, 3u);
        const Color colour(static_cast<int>(shade * 255.0f), static_cast<int>(shade * 255.0f), static_cast<int>(shade * 255.0f), 255);
        const Vector3 base = tree.position - Vector3(0.0f, 0.15f, 0.0f);
        const int left = kAtlasPadding + static_cast<int>(tree.seed & 1u) * (kCardWidth + kAtlasPadding);
        const float u0 = (static_cast<float>(left) + 0.5f) / static_cast<float>(kAtlasWidth);
        const float u1 = (static_cast<float>(left + kCardWidth) - 0.5f) / static_cast<float>(kAtlasWidth);
        for (int k = 0; k < 3; ++k) {
            const float a = tree.rotationRad + static_cast<float>(k) * std::numbers::pi_v<float> / 3.0f;
            const Vector3 right(std::cos(a) * halfW, 0.0f, std::sin(a) * halfW);
            const Vector3 up(0.0f, h, 0.0f);
            const Vector3 n(-std::sin(a), 0.0f, std::cos(a));
            const std::uint32_t i0 = mesh.AddVertex(base - right, n, Vector2(u0, 1.0f), colour);
            const std::uint32_t i1 = mesh.AddVertex(base + right, n, Vector2(u1, 1.0f), colour);
            const std::uint32_t i2 = mesh.AddVertex(base + right + up, n, Vector2(u1, 0.0f), colour);
            const std::uint32_t i3 = mesh.AddVertex(base - right + up, n, Vector2(u0, 0.0f), colour);
            // Both windings so the cards are visible from both sides even with culling on.
            mesh.AddQuad(i0, i1, i2, i3);
            mesh.AddQuad(i1, i0, i3, i2);
        }
    }

    void VegetationGenerator::AppendTrunk(const Map::PlacedTree& tree, MeshData& mesh)
    {
        if (tree.species == TreeSpecies::Bush) return;
        const float h = tree.Height();
        const float trunkH = Map::IsConifer(tree.species) ? h * 0.5f : h * 0.45f;
        mesh.AddCylinder(tree.position - Vector3(0.0f, 0.2f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), tree.TrunkRadius(), trunkH + 0.2f, 7, false,
                         Color(255, 255, 255, 255), 0.5f);
    }
}
