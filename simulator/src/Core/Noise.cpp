#include "CarSim/Core/Noise.hpp"

#include <algorithm>
#include <cmath>

namespace CarSim::Core
{
    namespace Noise
    {
        float Hash(const int x, const int y, const std::uint32_t seed)
        {
            std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u + seed * 2246822519u;
            h = (h ^ (h >> 13)) * 1274126177u;
            h ^= h >> 16;
            return static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
        }

        namespace
        {
            float Smooth(const float t) { return t * t * (3.0f - 2.0f * t); }
            int Mod(const int a, const int m) { return ((a % m) + m) % m; }
        }

        float Value(const float x, const float y, const int period, const std::uint32_t seed)
        {
            const int p = std::max(1, period);
            const float fx = std::floor(x);
            const float fy = std::floor(y);
            const int ix = static_cast<int>(fx);
            const int iy = static_cast<int>(fy);
            const float tx = Smooth(x - fx);
            const float ty = Smooth(y - fy);
            const float a = Hash(Mod(ix, p), Mod(iy, p), seed);
            const float b = Hash(Mod(ix + 1, p), Mod(iy, p), seed);
            const float c = Hash(Mod(ix, p), Mod(iy + 1, p), seed);
            const float d = Hash(Mod(ix + 1, p), Mod(iy + 1, p), seed);
            return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
        }

        float Fbm(const float x, const float y, const int period, const int octaves, const float persistence, const std::uint32_t seed)
        {
            float amplitude = 1.0f;
            float total = 0.0f;
            float norm = 0.0f;
            float frequency = 1.0f;
            int p = std::max(1, period);
            for (int i = 0; i < octaves; ++i) {
                total += Value(x * frequency, y * frequency, p, seed + static_cast<std::uint32_t>(i) * 101u) * amplitude;
                norm += amplitude;
                amplitude *= persistence;
                frequency *= 2.0f;
                p *= 2;
            }
            return norm > 0.0f ? total / norm : 0.0f;
        }

        float Cellular(const float x, const float y, const int period, const std::uint32_t seed)
        {
            const int p = std::max(1, period);
            const int ix = static_cast<int>(std::floor(x));
            const int iy = static_cast<int>(std::floor(y));
            float best = 4.0f;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int cx = ix + dx;
                    const int cy = iy + dy;
                    const float px = static_cast<float>(cx) + Hash(Mod(cx, p), Mod(cy, p), seed);
                    const float py = static_cast<float>(cy) + Hash(Mod(cx, p), Mod(cy, p), seed + 7919u);
                    const float d = (px - x) * (px - x) + (py - y) * (py - y);
                    best = std::min(best, d);
            }
            }
            return std::clamp(std::sqrt(best), 0.0f, 1.0f);
        }

        float ValueOpen(const float x, const float y, const std::uint32_t seed)
        {
            const float fx = std::floor(x);
            const float fy = std::floor(y);
            const int ix = static_cast<int>(fx);
            const int iy = static_cast<int>(fy);
            const float tx = Smooth(x - fx);
            const float ty = Smooth(y - fy);
            const float a = Hash(ix, iy, seed);
            const float b = Hash(ix + 1, iy, seed);
            const float c = Hash(ix, iy + 1, seed);
            const float d = Hash(ix + 1, iy + 1, seed);
            const float top = a + (b - a) * tx;
            const float bottom = c + (d - c) * tx;
            return top + (bottom - top) * ty;
        }

        float FbmSigned(const float x, const float y, const int octaves, const float persistence, const std::uint32_t seed)
        {
            float amplitude = 1.0f;
            float total = 0.0f;
            float norm = 0.0f;
            float frequency = 1.0f;
            for (int i = 0; i < octaves; ++i) {
                total += (ValueOpen(x * frequency + 13.7f * static_cast<float>(i), y * frequency - 7.1f * static_cast<float>(i),
                                    seed + static_cast<std::uint32_t>(i) * 101u) * 2.0f - 1.0f) * amplitude;
                norm += amplitude;
                amplitude *= persistence;
                frequency *= 2.0f;
            }
            return norm > 0.0f ? total / norm : 0.0f;
        }
    }
}
