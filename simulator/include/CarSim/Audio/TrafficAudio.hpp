// A small, device-independent spatial bed for nearby AI vehicles. The game supplies poses;
// this class selects audible sources and keeps oscillator/envelope state across audio blocks.
#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cstdint>
#include <vector>
#include <span>

namespace CarSim::Audio
{
    struct TrafficSoundSource
    {
        int id = 0;
        Microsoft::Xna::Framework::Vector3 position{};
        Microsoft::Xna::Framework::Vector3 velocity{};
        float speedMs = 0.0f;
        float acceleration = 0.0f;
        bool heavy = false;
    };

    class TrafficAudio
    {
    public:
        explicit TrafficAudio(int sampleRate = 44100);

        void SetScene(std::span<const TrafficSoundSource> sources,
                      const Microsoft::Xna::Framework::Vector3& listener,
                      const Microsoft::Xna::Framework::Vector3& listenerVelocity,
                      const Microsoft::Xna::Framework::Vector3& listenerRight);
        /// Adds interleaved stereo samples. No output when no vehicle is within 90 metres.
        void Render(float* stereo, int frames);
        [[nodiscard]] int ActiveVoices() const { return static_cast<int>(voices_.size()); }

    private:
        struct Voice
        {
            int id = -1;
            double phase = 0.0;
            float gain = 0.0f;
            float targetGain = 0.0f;
            float pan = 0.0f;
            float targetPan = 0.0f;
            float frequency = 35.0f;
            float targetFrequency = 35.0f;
            float roadShare = 0.0f;
            float targetRoadShare = 0.0f;
            float roadLow = 0.0f;
            float roadBass = 0.0f;
            std::uint32_t roadSeed = 1u;
        };
        int sampleRate_;
        std::vector<Voice> voices_;
    };
}
