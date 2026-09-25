// Accepted real-car start and idle recording, streamed from a small PCM asset in memory.
// Pure DSP: no audio device or renderer dependency.
#pragma once

#include "CarSim/Audio/EngineSynth.hpp"

#include <string>
#include <vector>

namespace CarSim::Audio
{
    class EngineRecording
    {
    public:
        [[nodiscard]] bool LoadWav(const std::string& path);
        [[nodiscard]] bool Available() const { return !samples_.empty(); }
        [[nodiscard]] static float IdleShare(float rpm);
        /// Overwrites `stereo` with interleaved samples. With no asset it writes silence.
        void Render(float* stereo, int frames, const EngineSoundInput& input);

    private:
        [[nodiscard]] float Sample(double frame, int channel) const;

        std::vector<float> samples_; // stereo, 44.1 kHz
        double cursor_ = 0.0;
        float gain_ = 0.0f;
        EngineSoundState previousState_ = EngineSoundState::Off;
        bool playing_ = false;
    };
}
