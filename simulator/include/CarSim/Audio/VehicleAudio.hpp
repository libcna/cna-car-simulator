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
#if defined(__EMSCRIPTEN__)
        // The browser pulls audio in ~43 ms bursts on the main thread and the web frame loop runs
        // zero, one or several updates per animation frame, so three blocks (~70 ms) ran dry and
        // played silence. Eight (~186 ms) ride over a slow frame; the cost is latency, not sound.
        static constexpr int kTargetPendingBlocks = 8;
#else
        static constexpr int kTargetPendingBlocks = 3;
#endif

        /// Creates the stream; `enabled = false` builds a silent no-op mixer (headless runs).
        explicit VehicleAudio(bool enabled);
        ~VehicleAudio();

        /// Mixes and submits as many blocks as the stream needs; call once per frame.
        void Update(const Sim::VehicleState& state, bool cockpit, const std::vector<Collision::ContactEvent>& contacts, float dt);

        AudioLevels levels;
        /// Falling rain (0..1) and how wet the road is (0..1): rain hisses on the roof and the
        /// screen, a wet road adds spray under the wheels. Set once per frame from the weather.
        void SetWeather(float rain, float wetness);
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
        unsigned blockIndex_ = 0;            // for the overrun burble gate
        float secondsSinceShift_ = 1e9f;     // gear-change dip envelope
        float brakeGain_ = 0.0f;             // smoothed brake hiss gain
        NoiseSource brakeNoise_{4242u};
        OnePoleLowPass brakeLp_;
        float rain_ = 0.0f;                  // falling rain, 0..1
        float wetness_ = 0.0f;               // wet road, 0..1
        float rainGain_ = 0.0f;              // smoothed rain hiss gain
        float sprayGain_ = 0.0f;             // smoothed spray gain
        NoiseSource rainNoise_{9137u};
        NoiseSource sprayNoise_{5521u};
        OnePoleLowPass rainLp_;
        OnePoleHighPass rainHp_;
        OnePoleLowPass sprayLp_;
        // Edge detection.
        bool prevLeft_ = false;
        bool prevRight_ = false;
        int prevGear_ = 0;
        Sim::EngineState prevEngineState_ = Sim::EngineState::Off;
        float impactCooldown_ = 0.0f;
    };
}
