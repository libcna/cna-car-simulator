// Vehicle audio mixer: one stereo DynamicSoundEffectInstance stream (XNA API) fed with the
// engine synthesiser, rolling noise, horn and one-shot clips mixed in project code. The
// cockpit view low-passes and attenuates the mix.
#pragma once

#include "CarSim/Audio/EngineSynth.hpp"
#include "CarSim/Audio/SoundSynth.hpp"
#include "CarSim/Collision/CollisionWorld.hpp"
#include "CarSim/Sim/Vehicle.hpp"

#include "Microsoft/Xna/Framework/Audio/DynamicSoundEffectInstance.hpp"

#include <memory>
#include <vector>

namespace CarSim::Audio
{
    struct AudioLevels
    {
        float master = 0.8f;
        float engine = 1.0f;
        float effects = 1.0f;
        float cockpitAttenuation = 0.55f;   // gain applied inside the car
        float cockpitLowPassHz = 1700.0f;
    };

    class VehicleAudio
    {
    public:
        static constexpr int kSampleRate = 44100;
        static constexpr int kBlockFrames = 1024;
        static constexpr int kTargetPendingBlocks = 3;

        /// Creates the stream; `enabled = false` builds a silent no-op mixer (headless runs).
        explicit VehicleAudio(bool enabled);
        ~VehicleAudio();

        /// Mixes and submits as many blocks as the stream needs; call once per frame.
        void Update(const Sim::VehicleState& state, bool cockpit, const std::vector<Collision::ContactEvent>& contacts, float dt);

        AudioLevels levels;
        [[nodiscard]] bool Enabled() const { return enabled_; }
        [[nodiscard]] int BlocksSubmitted() const { return blocksSubmitted_; }
        [[nodiscard]] int Underruns() const { return underruns_; }

        /// Renders one block into `stereo` (interleaved, 2 * frames floats) — public for tests.
        void RenderBlock(std::vector<float>& stereo, const Sim::VehicleState& state, bool cockpit);

    private:
        struct Voice
        {
            const Clip* clip = nullptr;
            std::size_t position = 0;
            float gain = 1.0f;
        };

        void Trigger(const Clip& clip, float gain);
        void DetectEvents(const Sim::VehicleState& state, const std::vector<Collision::ContactEvent>& contacts);

        bool enabled_ = false;
        std::unique_ptr<Microsoft::Xna::Framework::Audio::DynamicSoundEffectInstance> stream_;
        EngineSynth engine_{kSampleRate};
        RollingNoise rolling_{kSampleRate};
        HornVoice horn_{kSampleRate};
        Clip tick_, tock_, clunk_, catch_;
        std::vector<Clip> impacts_;
        std::vector<Voice> voices_;
        OnePoleLowPass cabinLeft_, cabinRight_;
        std::vector<float> mono_;
        std::vector<float> stereo_;
        std::vector<SharpRuntime::bytecs> pcm_;
        int blocksSubmitted_ = 0;
        int underruns_ = 0;
        float cockpitBlend_ = 0.0f;
        bool hornPressed_ = false;
        // Edge detection.
        bool prevLeft_ = false;
        bool prevRight_ = false;
        int prevGear_ = 0;
        Sim::EngineState prevEngineState_ = Sim::EngineState::Off;
        float impactCooldown_ = 0.0f;
    };
}
