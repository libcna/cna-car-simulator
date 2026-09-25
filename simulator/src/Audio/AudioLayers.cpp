#include "CarSim/Audio/AudioLayers.hpp"

#include <algorithm>
#include <cmath>

namespace CarSim::Audio::Layers
{
    float ShiftDip(const float secondsSinceShift)
    {
        if (secondsSinceShift < 0.0f) return 1.0f;
        const float t = std::clamp(secondsSinceShift / kShiftDipSeconds, 0.0f, 1.0f);
        return 0.2f + 0.8f * t * t;   // fast cut, slow recovery
    }

    namespace
    {
        float Hash(unsigned x)
        {
            x ^= x >> 16;
            x *= 0x7feb352du;
            x ^= x >> 15;
            x *= 0x846ca68bu;
            x ^= x >> 16;
            return static_cast<float>(x & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
        }
    }

    float OverrunBurble(const unsigned blockIndex, const float rpm, const float engineLoad, const float throttle, const float speedKmh)
    {
        if (engineLoad > 0.05f || throttle > 0.05f || rpm < 2200.0f || speedKmh < 15.0f) return 0.0f;
        // Blocks are ~23 ms; a pop lasts one block and about a third of the blocks fire, denser
        // at higher rpm.
        const float chance = 0.22f + 0.18f * std::clamp((rpm - 2200.0f) / 3000.0f, 0.0f, 1.0f);
        const float r = Hash(blockIndex * 2654435761u + 17u);
        if (r > chance) return 0.0f;
        return 0.10f + 0.12f * Hash(blockIndex * 40503u + 3u);
    }

    float BrakeHissGain(const float brakePedal, const float speedKmh)
    {
        const float pedal = std::clamp(brakePedal, 0.0f, 1.0f);
        const float speed = std::clamp(speedKmh / 60.0f, 0.0f, 1.0f);
        return 0.16f * pedal * pedal * speed;
    }

    float RainDropRate(const float rain, const float speedKmh)
    {
        if (rain <= 0.02f) return 0.0f;
        return rain * (60.0f + 220.0f * rain) * (1.0f + std::clamp(speedKmh / 90.0f, 0.0f, 1.0f));
    }

    float WiperSwishGain(const float bladeSpeed, const float wetness)
    {
        const float speed = std::clamp(bladeSpeed / 2.0f, 0.0f, 1.0f);
        return speed * (0.05f + 0.04f * (1.0f - std::clamp(wetness, 0.0f, 1.0f)));
    }

    float SplashRate(const float wetness, const float speedKmh)
    {
        const float standing = std::clamp((wetness - 0.35f) / 0.65f, 0.0f, 1.0f);
        if (standing <= 0.0f || speedKmh < 10.0f) return 0.0f;
        return standing * std::clamp(speedKmh / 60.0f, 0.0f, 1.5f) * 1.6f;
    }

    float SurfaceRoughness(const Sim::SurfaceType surface)
    {
        switch (surface) {
            case Sim::SurfaceType::Asphalt: return 1.0f;
            case Sim::SurfaceType::Concrete: return 1.1f;
            case Sim::SurfaceType::Cobbles: return 1.7f;
            case Sim::SurfaceType::Gravel: return 1.8f;
            case Sim::SurfaceType::Grass: return 1.4f;
            case Sim::SurfaceType::Dirt: return 1.6f;
        }
        return 1.0f;
    }
}
