#include "CarSim/Render/CarTextures.hpp"

#include "CarSim/Core/Noise.hpp"

#include <algorithm>
#include <cmath>

namespace CarSim::Render::CarTextures
{
    namespace
    {
        Color Grey(const float v, const float alpha = 1.0f)
        {
            const int b = static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
            return Color(b, b, b, static_cast<int>(std::lround(std::clamp(alpha, 0.0f, 1.0f) * 255.0f)));
        }

        /// Multiplies a soft dark line into a greyscale "factor" buffer.
        struct Factor
        {
            int size;
            std::vector<float> f;
            explicit Factor(int s) : size(s), f(static_cast<std::size_t>(s) * static_cast<std::size_t>(s), 1.0f) {}
            float& At(int x, int y) { return f[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)]; }

            /// Line from (u0, v0) to (u1, v1) in 0..1 with a width in pixels and darkness 0..1.
            void Line(float u0, float v0, float u1, float v1, float widthPx, float darkness)
            {
                const float s = static_cast<float>(size);
                const float x0 = u0 * s, y0 = v0 * s, x1 = u1 * s, y1 = v1 * s;
                const float minX = std::min(x0, x1) - widthPx - 1.0f, maxX = std::max(x0, x1) + widthPx + 1.0f;
                const float minY = std::min(y0, y1) - widthPx - 1.0f, maxY = std::max(y0, y1) + widthPx + 1.0f;
                const float dx = x1 - x0, dy = y1 - y0;
                const float len2 = std::max(1e-4f, dx * dx + dy * dy);
                for (int y = std::max(0, static_cast<int>(minY)); y <= std::min(size - 1, static_cast<int>(maxY)); ++y) {
                    for (int x = std::max(0, static_cast<int>(minX)); x <= std::min(size - 1, static_cast<int>(maxX)); ++x) {
                        const float px = static_cast<float>(x) + 0.5f, py = static_cast<float>(y) + 0.5f;
                        const float t = std::clamp(((px - x0) * dx + (py - y0) * dy) / len2, 0.0f, 1.0f);
                        const float d = std::hypot(px - (x0 + dx * t), py - (y0 + dy * t));
                        const float a = std::clamp(1.0f - (d - widthPx * 0.5f + 0.5f), 0.0f, 1.0f);
                        At(x, y) *= 1.0f - darkness * a;
                    }
                }
            }

            /// Soft rectangular darkening (falls off over `feather` in pixels).
            void Shade(float u0, float v0, float u1, float v1, float darkness, float featherPx)
            {
                const float s = static_cast<float>(size);
                for (int y = 0; y < size; ++y) {
                    for (int x = 0; x < size; ++x) {
                        const float px = static_cast<float>(x) + 0.5f, py = static_cast<float>(y) + 0.5f;
                        const float ddx = std::max({u0 * s - px, px - u1 * s, 0.0f});
                        const float ddy = std::max({v0 * s - py, py - v1 * s, 0.0f});
                        const float d = std::hypot(ddx, ddy);
                        const float a = std::clamp(1.0f - d / std::max(1.0f, featherPx), 0.0f, 1.0f);
                        if (a > 0.0f) At(x, y) *= 1.0f - darkness * a;
                    }
                }
            }
        };
    }

    Image PaintDetail(const BodyUvLayout& uv, const int size)
    {
        Factor f(size);
        const float px = static_cast<float>(size) / 1024.0f;   // widths scale with the resolution
        const auto mirrored = [&](auto&& draw) {
            draw(false);
            draw(true);
        };
        const auto U = [](float u, bool left) { return left ? 1.0f - u : u; };
        // Sill and underbody darkening (ambient occlusion under the car).
        f.Shade(0.0f, 0.0f, uv.uRockerTop, 1.0f, 0.22f, 26.0f * px);
        f.Shade(1.0f - uv.uRockerTop, 0.0f, 1.0f, 1.0f, 0.22f, 26.0f * px);
        // Wheel arches: darker just above the openings.
        for (const float v : {uv.vFrontArch, uv.vRearArch}) {
            f.Shade(uv.uRockerBottom, v - uv.archHalfV, uv.uDoorMid - 0.01f, v + uv.archHalfV, 0.12f, 40.0f * px);
            f.Shade(1.0f - (uv.uDoorMid - 0.01f), v - uv.archHalfV, 1.0f - uv.uRockerBottom, v + uv.archHalfV, 0.12f, 40.0f * px);
        }
        const float line = 2.6f * px;
        const float dark = 0.62f;
        // Hood: rear shut line at the cowl and the crease to the fenders.
        f.Line(uv.uRoofRail + 0.01f, uv.vCowl - 0.004f, 1.0f - (uv.uRoofRail + 0.01f), uv.vCowl - 0.004f, line, dark);
        mirrored([&](bool left) {
            f.Line(U(uv.uRoofRail + 0.006f, left), uv.vHoodStart, U(uv.uRoofRail + 0.01f, left), uv.vCowl - 0.004f, line, dark * 0.8f);
            // Doors: front edge, B-pillar cut, rear edge and the bottom edge along the rocker.
            f.Line(U(uv.uRockerTop + 0.003f, left), uv.vDoorFront, U(uv.uBelt + 0.006f, left), uv.vDoorFront, line, dark);
            f.Line(U(uv.uRockerTop + 0.003f, left), uv.vBPillar, U(uv.uBelt + 0.006f, left), uv.vBPillar, line, dark);
            f.Line(U(uv.uRockerTop + 0.003f, left), uv.vDoorRear, U(uv.uBelt + 0.006f, left), uv.vDoorRear, line, dark);
            f.Line(U(uv.uRockerTop + 0.003f, left), uv.vDoorFront, U(uv.uRockerTop + 0.003f, left), uv.vDoorRear, line, dark * 0.9f);
            // Bumper seams.
            f.Line(U(uv.uRockerBottom, left), uv.vFrontBumper, U(uv.uBelt + 0.02f, left), uv.vFrontBumper, line, dark * 0.8f);
            f.Line(U(uv.uRockerBottom, left), uv.vRearBumper, U(uv.uBelt + 0.02f, left), uv.vRearBumper, line, dark * 0.8f);
            // Tailgate side cut.
            f.Line(U(uv.uRoofRail + 0.004f, left), uv.vTailgate, U(uv.uRoofRail + 0.004f, left), 0.985f, line, dark * 0.8f);
            // Character crease along the doors: a light edge above and a shade below.
            f.Shade(U(uv.uDoorMid, left) - 0.012f, uv.vDoorFront - 0.03f, U(uv.uDoorMid, left) + 0.0f, uv.vDoorRear + 0.08f, 0.06f, 14.0f * px);
        });
        // Tailgate top seam.
        f.Line(uv.uRoofRail + 0.004f, uv.vTailgate, 1.0f - (uv.uRoofRail + 0.004f), uv.vTailgate, line, dark * 0.8f);
        // Fuel filler flap (right rear quarter): outlined square.
        {
            const float w = 0.024f, h = 0.036f;
            const float u0 = uv.fuelFlapU - w, u1 = uv.fuelFlapU + w, v0 = uv.fuelFlapV - h * 0.5f, v1 = uv.fuelFlapV + h * 0.5f;
            f.Line(u0, v0, u1, v0, line, dark * 0.7f);
            f.Line(u1, v0, u1, v1, line, dark * 0.7f);
            f.Line(u1, v1, u0, v1, line, dark * 0.7f);
            f.Line(u0, v1, u0, v0, line, dark * 0.7f);
        }
        Image img(size, size);
        img.Generate([&](int x, int y, float, float) {
            const float fine = 0.985f + 0.03f * Core::Noise::Value(static_cast<float>(x) / static_cast<float>(size) * 96.0f,
                                                                   static_cast<float>(y) / static_cast<float>(size) * 96.0f, 96, 5u);
            return Grey(f.At(x, y) * fine);
        });
        return img;
    }

    Image GlassTint(const BodyUvLayout& uv, const int size, const float alpha)
    {
        Image img(size, size);
        const float fritV = 0.012f;
        const float fritU = 0.010f;
        img.Generate([&](int, int, float u, float v) {
            const bool windshield = v > uv.vCowl - 0.001f && v < uv.vDoorFront + 0.25f && u > uv.uRoofRail - 0.02f && u < 1.0f - (uv.uRoofRail - 0.02f);
            float a = alpha;
            float tone = 1.0f;
            if (windshield) {
                // Frit band around the windshield edges, opaque; shade band along the top.
                const float dTop = (uv.vDoorFront + 0.25f) - v;
                const float dBottom = v - uv.vCowl;
                const float dSide = std::min(u - (uv.uRoofRail - 0.02f), (1.0f - (uv.uRoofRail - 0.02f)) - u);
                if (dTop < fritV || dBottom < fritV * 0.7f || dSide < fritU) {
                    a = 1.0f;
                    tone = 0.0f;
                } else if (dTop < 0.06f) {
                    a = std::min(1.0f, alpha + 0.35f * (1.0f - dTop / 0.06f));
                    tone = 0.75f;
                }
            } else {
                // Side and rear glass: thin ceramic edge band.
                const float edge = std::min(u - uv.uBelt, (1.0f - uv.uBelt) - u);
                if (edge > -0.02f && edge < 0.004f) { a = 0.9f; tone = 0.15f; }
            }
            // Premultiplied: colour times alpha.
            const int c = static_cast<int>(std::lround(tone * a * 255.0f));
            return Color(c, c, c, static_cast<int>(std::lround(a * 255.0f)));
        });
        return img;
    }

    Image TyreTread(const int size)
    {
        Image img(size, size);
        img.Generate([&](int, int, float u, float v) {
            float g = 0.16f;
            const float grain = Core::Noise::Value(u * 64.0f, v * 64.0f, 64, 21u);
            if (v > 0.19f && v < 0.47f) {
                g = 0.20f;
                // Three circumferential grooves and lateral sipes.
                for (const float gv : {0.245f, 0.33f, 0.415f}) {
                    if (std::fabs(v - gv) < 0.012f) g = 0.07f;
                }
                const float sipe = std::fmod(u * 3.0f + (v < 0.33f ? 0.0f : 0.5f), 1.0f);
                if (sipe < 0.05f) g *= 0.6f;
                const float block = std::fmod(u * 3.0f, 1.0f);
                g += 0.02f * std::sin(block * 6.2831853f);
            } else if (v < 0.19f || v < 0.62f) {
                // Sidewalls: a lettering ring and a subtle radial texture.
                const float ring = v < 0.19f ? std::fabs(v - 0.10f) : std::fabs(v - 0.54f);
                if (ring < 0.012f && std::fmod(u * 24.0f, 1.0f) < 0.55f) g = 0.26f;
                g += 0.01f * std::sin(u * 6.2831853f * 40.0f);
            } else {
                g = 0.10f;
            }
            g += (grain - 0.5f) * 0.04f;
            return Grey(g);
        });
        return img;
    }

    Image RimFinish(const int size)
    {
        Image img(size, size);
        img.Generate([&](int, int, float u, float v) {
            const float brush = Core::Noise::Value(u * 200.0f, v * 8.0f, 200, 33u);
            return Grey(0.80f + (brush - 0.5f) * 0.10f);
        });
        return img;
    }

    Image HeadlampLens(const int size)
    {
        Image img(size, size);
        img.Generate([&](int, int, float u, float v) {
            // Projector: two reflector bowls (bright rings) on a dark chrome background.
            float g = 0.52f;
            for (const float cx : {0.30f, 0.68f}) {
                const float d = std::hypot((u - cx) * 1.3f, v - 0.5f);
                if (d < 0.18f) g = 0.70f + 0.30f * std::clamp(1.0f - d / 0.18f, 0.0f, 1.0f);
                if (std::fabs(d - 0.18f) < 0.015f) g = 0.95f;
                if (d < 0.06f) g = 0.25f;
            }
            const float rib = 0.5f + 0.5f * std::sin(v * 6.2831853f * 12.0f);
            g *= 0.9f + 0.1f * rib;
            return Grey(g);
        });
        return img;
    }

    Image TailLampLens(const int size)
    {
        Image img(size, size);
        img.Generate([&](int, int, float u, float v) {
            const float rib = 0.5f + 0.5f * std::sin(v * 6.2831853f * 14.0f);
            const float edge = std::min({u, 1.0f - u, v, 1.0f - v});
            float g = 0.80f + 0.20f * rib;
            if (edge < 0.06f) g *= 0.7f;
            return Grey(g);
        });
        return img;
    }

    Image GrilleMesh(const int size)
    {
        Image img(size, size);
        img.Generate([&](int, int, float u, float v) {
            // Hexagonal lattice: bars lighter than the dark openings.
            const float cell = 8.0f;
            const float x = u * cell;
            const float y = v * cell * 0.866f;
            const float row = std::floor(y);
            const float fx = std::fmod(x + (static_cast<int>(row) % 2 == 0 ? 0.0f : 0.5f) + 100.0f, 1.0f);
            const float fy = y - row;
            const float dx = std::fabs(fx - 0.5f);
            const float dy = std::fabs(fy - 0.5f);
            const float hex = std::max(dx, dy * 0.5f + dx * 0.5f);
            const bool bar = hex > 0.38f;
            return Grey(bar ? 0.30f : 0.045f);
        });
        return img;
    }

    Image InteriorPlastic(const int size, const Rgb& base, const unsigned seed)
    {
        Image img(size, size);
        img.Generate([&](int, int, float u, float v) {
            const float grain = Core::Noise::Value(u * 180.0f, v * 180.0f, 180, seed);
            const float coarse = Core::Noise::Fbm(u * 12.0f, v * 12.0f, 12, 3, 0.5f, seed + 3u);
            const float k = 0.90f + (grain - 0.5f) * 0.16f + (coarse - 0.5f) * 0.08f;
            const Rgb c = base * k;
            return Color(static_cast<int>(std::clamp(c.r, 0.0f, 1.0f) * 255.0f), static_cast<int>(std::clamp(c.g, 0.0f, 1.0f) * 255.0f),
                         static_cast<int>(std::clamp(c.b, 0.0f, 1.0f) * 255.0f), 255);
        });
        return img;
    }

    Image Fabric(const int size, const Rgb& base, const unsigned seed)
    {
        Image img(size, size);
        img.Generate([&](int, int, float u, float v) {
            const float weaveU = 0.5f + 0.5f * std::sin(u * 6.2831853f * 90.0f);
            const float weaveV = 0.5f + 0.5f * std::sin(v * 6.2831853f * 90.0f);
            const float weave = 0.86f + 0.14f * (weaveU * weaveV);
            const float grain = Core::Noise::Value(u * 120.0f, v * 120.0f, 120, seed);
            const float patches = Core::Noise::Fbm(u * 6.0f, v * 6.0f, 6, 3, 0.5f, seed + 7u);
            const float k = weave * (0.94f + (grain - 0.5f) * 0.12f + (patches - 0.5f) * 0.10f);
            const Rgb c = base * k;
            return Color(static_cast<int>(std::clamp(c.r, 0.0f, 1.0f) * 255.0f), static_cast<int>(std::clamp(c.g, 0.0f, 1.0f) * 255.0f),
                         static_cast<int>(std::clamp(c.b, 0.0f, 1.0f) * 255.0f), 255);
        });
        return img;
    }

    Image Headliner(const int size)
    {
        Image img(size, size);
        img.Generate([&](int, int, float u, float v) {
            const float grain = Core::Noise::Value(u * 160.0f, v * 160.0f, 160, 41u);
            return Grey(0.78f + (grain - 0.5f) * 0.10f);
        });
        return img;
    }

    Image VentSlats(const int size)
    {
        Image img(size, size);
        img.Generate([&](int, int, float u, float v) {
            const float slat = std::fmod(v * 6.0f, 1.0f);
            const float g = slat < 0.55f ? 0.34f + 0.10f * (slat / 0.55f) : 0.05f;
            const float edge = std::min(u, 1.0f - u);
            return Grey(edge < 0.03f ? 0.30f : g);
        });
        return img;
    }

    Image Chrome(const int size)
    {
        Image img(size, size);
        img.Generate([&](int, int, float u, float v) {
            const float band = 0.5f + 0.5f * std::sin((u * 0.4f + v) * 6.2831853f * 1.5f);
            (void)band;
            return Grey(0.86f);
        });
        return img;
    }
}
