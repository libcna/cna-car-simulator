// Project-owned helicopter rotor sound. Pure DSP; no audio device or renderer dependency.
#pragma once

#include <cstdint>

namespace CarSim::Audio
{
    class RotorSynth
    {
    public:
        explicit RotorSynth(int sampleRate = 44100);

        /// Adds the flight rotor to a mono buffer. The rotor speed keeps the accepted
        /// 5/6/7/8 Hz cadence for the normal/turbo/ultra/ultra-ultra modes.
        void RenderAdd(float* out, int frames, bool active, float rotorHz, float level);
        void Reset();

    private:
        int sampleRate_;
        double rotorPhase_ = 0.0;
        double turbinePhase_ = 0.0;
        float gain_ = 0.0f;
        float bladeLowPass_ = 0.0f;
        float bladeLowPass2_ = 0.0f;
        std::uint32_t noiseState_ = 0x726f746fu;
    };
}
