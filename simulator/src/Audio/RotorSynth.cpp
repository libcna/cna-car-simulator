#include "CarSim/Audio/RotorSynth.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace CarSim::Audio
{
    namespace
    {
        constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
    }

    RotorSynth::RotorSynth(const int sampleRate) : sampleRate_(sampleRate > 8000 ? sampleRate : 44100) {}

    void RotorSynth::Reset()
    {
        rotorPhase_ = turbinePhase_ = 0.0;
        gain_ = bladeLowPass_ = bladeLowPass2_ = 0.0f;
        noiseState_ = 0x726f746fu;
    }

    void RotorSynth::RenderAdd(float* out, const int frames, const bool active, const float rotorHz, const float level)
    {
        const float hz = std::clamp(rotorHz, 2.0f, 12.0f);
        const float rate = 1.0f / (0.12f * static_cast<float>(sampleRate_));
        const float bladeFilter = std::min(1.0f, kTwoPi * 1150.0f / static_cast<float>(sampleRate_));
        const float turbineHz = 150.0f + hz * 14.0f;
        for (int i = 0; i < frames; ++i) {
            gain_ += std::clamp((active ? 1.0f : 0.0f) - gain_, -rate, rate);
            rotorPhase_ += static_cast<double>(hz) / sampleRate_;
            if (rotorPhase_ >= 1.0) rotorPhase_ -= 1.0;
            turbinePhase_ += static_cast<double>(turbineHz) / sampleRate_;
            if (turbinePhase_ >= 1.0) turbinePhase_ -= 1.0;

            const float phase = static_cast<float>(rotorPhase_) * kTwoPi;
            // Preserve the existing low blade thrum, then add a narrow, rhythmic sweep and
            // a quieter turbine note. Noise is filtered and shaped by each blade's passage
            // instead of playing as a constant hiss over the car mix.
            const float thrum = 0.14f * std::sin(phase * 2.0f) +
                                0.07f * std::sin(phase * 4.0f) +
                                0.04f * std::sin(phase * 9.0f);
            const float bladeWave = std::max(0.0f, std::sin(phase * 2.0f));
            const float bladeSquared = bladeWave * bladeWave;
            const float blade = bladeSquared * bladeSquared * bladeSquared;
            noiseState_ = noiseState_ * 1664525u + 1013904223u;
            const float noise = static_cast<float>(noiseState_ >> 8) / static_cast<float>(1u << 24) * 2.0f - 1.0f;
            bladeLowPass_ += (noise - bladeLowPass_) * bladeFilter;
            bladeLowPass2_ += (bladeLowPass_ - bladeLowPass2_) * bladeFilter;
            const float sweep = bladeLowPass2_ * (0.012f + 0.065f * blade);
            const float turbinePhase = static_cast<float>(turbinePhase_) * kTwoPi;
            const float turbine = 0.016f * std::sin(turbinePhase) + 0.008f * std::sin(turbinePhase * 2.0f);
            out[i] += gain_ * level * (thrum + sweep + turbine);
        }
    }
}
