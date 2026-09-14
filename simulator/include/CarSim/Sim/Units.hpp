// Unit conversions and physical constants used by the simulation.
#pragma once

#include <numbers>

namespace CarSim::Sim::Units
{
    inline constexpr float kGravity = 9.81f;                 // m/s^2
    inline constexpr float kAirDensity = 1.2041f;            // kg/m^3 at 20 C, sea level
    inline constexpr float kPetrolDensityKgPerL = 0.745f;    // kg per litre
    inline constexpr float kPetrolEnergyMjPerKg = 43.5f;     // lower heating value

    [[nodiscard]] constexpr float KmhToMs(float kmh) { return kmh / 3.6f; }
    [[nodiscard]] constexpr float MsToKmh(float ms) { return ms * 3.6f; }
    [[nodiscard]] constexpr float RpmToRadS(float rpm) { return rpm * (2.0f * std::numbers::pi_v<float> / 60.0f); }
    [[nodiscard]] constexpr float RadSToRpm(float radS) { return radS * (60.0f / (2.0f * std::numbers::pi_v<float>)); }
    [[nodiscard]] constexpr float DegToRad(float deg) { return deg * (std::numbers::pi_v<float> / 180.0f); }
    [[nodiscard]] constexpr float RadToDeg(float rad) { return rad * (180.0f / std::numbers::pi_v<float>); }
}
