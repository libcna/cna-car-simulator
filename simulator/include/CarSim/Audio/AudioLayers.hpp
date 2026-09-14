// Pure envelope and gain helpers for the vehicle audio layers (gear-change dip, overrun
// burble, brake hiss, surface roughness). Kept free of any device so tests can check them.
#pragma once

#include "CarSim/Sim/Ground.hpp"

namespace CarSim::Audio::Layers
{
    /// Load multiplier after a gear change: the engine "breathes" for a quarter of a second.
    /// 0.2 right after the shift, back to 1.0 after `kShiftDipSeconds`.
    constexpr float kShiftDipSeconds = 0.26f;
    [[nodiscard]] float ShiftDip(float secondsSinceShift);

    /// Overrun burble: extra exhaust load in [0, 0.22] for the block with this index when the
    /// engine is being pushed by the wheels (no load, throttle closed, rpm above ~2200);
    /// a hashed gate gives irregular pops at roughly 8-14 per second. Returns 0 otherwise.
    [[nodiscard]] float OverrunBurble(unsigned blockIndex, float rpm, float engineLoad, float throttle, float speedKmh);

    /// Gain of the brake hiss layer: grows with pedal travel and speed, silent when stopped.
    [[nodiscard]] float BrakeHissGain(float brakePedal, float speedKmh);

    /// Rolling-noise roughness of a surface (1 = asphalt).
    [[nodiscard]] float SurfaceRoughness(Sim::SurfaceType surface);
}
