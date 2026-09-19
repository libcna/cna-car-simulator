// Small DSP toolbox for the project's synthesised sounds: one-shot clips (indicator tick and
// tock, gear clunk, collision impact), noise layers (tyres, wind), the horn and filters.
#pragma once

#include <cstdint>
#include <vector>

namespace CarSim::Audio
{
    struct Clip
    {
        std::vector<float> samples;   // mono, [-1, 1]
        int sampleRate = 44100;
        [[nodiscard]] float DurationSeconds() const { return static_cast<float>(samples.size()) / static_cast<float>(sampleRate); }
    };

    /// One-pole low-pass filter.
    class OnePoleLowPass
    {
    public:
        void SetCutoff(float hz, int sampleRate);
        [[nodiscard]] float Process(float x);
        void Reset() { y_ = 0.0f; }

    private:
        float a_ = 1.0f;
        float y_ = 0.0f;
    };

    /// One-pole high-pass filter.
    class OnePoleHighPass
    {
    public:
        void SetCutoff(float hz, int sampleRate);
        [[nodiscard]] float Process(float x);

    private:
        float a_ = 0.0f;
        float x1_ = 0.0f;
        float y_ = 0.0f;
    };

    /// Deterministic white noise source.
    class NoiseSource
    {
    public:
        explicit NoiseSource(std::uint32_t seed = 12345u) : state_(seed) {}
        [[nodiscard]] float Next();

    private:
        std::uint32_t state_;
    };

    namespace Clips
    {
        [[nodiscard]] Clip IndicatorTick(int sampleRate);
        [[nodiscard]] Clip IndicatorTock(int sampleRate);
        [[nodiscard]] Clip GearClunk(int sampleRate);
        [[nodiscard]] Clip Impact(int sampleRate, float strength);   // strength 0..1
        [[nodiscard]] Clip StarterCatch(int sampleRate);
        [[nodiscard]] Clip Footstep(int sampleRate);
    }

    /// Continuous rolling noise of the tyres and the wind, rendered per buffer.
    class RollingNoise
    {
    public:
        explicit RollingNoise(int sampleRate);
        struct Input
        {
            float speedKmh = 0.0f;
            float surfaceRoughness = 1.0f;   // 1 = asphalt, 1.8 gravel, 1.4 grass
            bool grounded = true;
        };
        /// Adds (mixes) into `out`.
        void Render(float* out, int frames, const Input& target);

    private:
        int sampleRate_;
        NoiseSource noise_{777u};
        // Two poles each: a single one left enough energy above 2 kHz to hiss.
        OnePoleLowPass tyreLp_;
        OnePoleLowPass tyreLp2_;
        OnePoleHighPass windHp_;
        OnePoleLowPass windLp_;
        OnePoleLowPass windLp2_;
        Input previous_{};
        bool primed_ = false;
    };

    /// Two-tone car horn with a soft gate.
    class HornVoice
    {
    public:
        explicit HornVoice(int sampleRate);
        void Render(float* out, int frames, bool pressed);

    private:
        int sampleRate_;
        double phaseA_ = 0.0;
        double phaseB_ = 0.0;
        float gate_ = 0.0f;
    };
}
