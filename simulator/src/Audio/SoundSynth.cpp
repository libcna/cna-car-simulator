#include "CarSim/Audio/SoundSynth.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace CarSim::Audio
{
    namespace
    {
        constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
    }

    void OnePoleLowPass::SetCutoff(const float hz, const int sampleRate)
    {
        a_ = std::clamp(kTwoPi * hz / static_cast<float>(sampleRate), 0.0f, 1.0f);
    }

    float OnePoleLowPass::Process(const float x)
    {
        y_ += (x - y_) * a_;
        return y_;
    }

    void OnePoleHighPass::SetCutoff(const float hz, const int sampleRate)
    {
        const float rc = 1.0f / (kTwoPi * std::max(1.0f, hz));
        const float dt = 1.0f / static_cast<float>(sampleRate);
        a_ = rc / (rc + dt);
    }

    float OnePoleHighPass::Process(const float x)
    {
        y_ = a_ * (y_ + x - x1_);
        x1_ = x;
        return y_;
    }

    float NoiseSource::Next()
    {
        state_ = state_ * 1664525u + 1013904223u;
        return static_cast<float>(state_ >> 8) / static_cast<float>(1u << 24) * 2.0f - 1.0f;
    }

    namespace Clips
    {
        namespace
        {
            Clip Make(const int sampleRate, const float seconds)
            {
                Clip c;
                c.sampleRate = sampleRate;
                c.samples.assign(static_cast<std::size_t>(seconds * static_cast<float>(sampleRate)), 0.0f);
                return c;
            }

            void AddDecayingSine(Clip& c, const float freq, const float amp, const float tau, const float start = 0.0f)
            {
                const float dt = 1.0f / static_cast<float>(c.sampleRate);
                for (std::size_t i = 0; i < c.samples.size(); ++i) {
                    const float t = static_cast<float>(i) * dt - start;
                    if (t < 0.0f) continue;
                    c.samples[i] += amp * std::exp(-t / tau) * std::sin(kTwoPi * freq * t);
                }
            }

            void AddNoiseBurst(Clip& c, const float amp, const float tau, const float lowPassHz, const float start = 0.0f, const std::uint32_t seed = 99u)
            {
                NoiseSource noise(seed);
                OnePoleLowPass lp;
                lp.SetCutoff(lowPassHz, c.sampleRate);
                const float dt = 1.0f / static_cast<float>(c.sampleRate);
                for (std::size_t i = 0; i < c.samples.size(); ++i) {
                    const float t = static_cast<float>(i) * dt - start;
                    const float n = lp.Process(noise.Next());
                    if (t < 0.0f) continue;
                    c.samples[i] += amp * std::exp(-t / tau) * n;
                }
            }

            void Normalise(Clip& c, const float peak)
            {
                float m = 0.0f;
                for (const float s : c.samples) m = std::max(m, std::fabs(s));
                if (m > 1e-6f) {
                    const float k = peak / m;
                    for (float& s : c.samples) s *= k;
                }
            }
        }

        Clip IndicatorTick(const int sampleRate)
        {
            Clip c = Make(sampleRate, 0.045f);
            AddNoiseBurst(c, 0.9f, 0.003f, 6000.0f);
            AddDecayingSine(c, 1900.0f, 0.5f, 0.006f);
            AddDecayingSine(c, 620.0f, 0.35f, 0.012f);
            Normalise(c, 0.7f);
            return c;
        }

        Clip IndicatorTock(const int sampleRate)
        {
            Clip c = Make(sampleRate, 0.05f);
            AddNoiseBurst(c, 0.8f, 0.004f, 3500.0f, 0.0f, 31u);
            AddDecayingSine(c, 1250.0f, 0.5f, 0.007f);
            AddDecayingSine(c, 430.0f, 0.4f, 0.014f);
            Normalise(c, 0.6f);
            return c;
        }

        Clip GearClunk(const int sampleRate)
        {
            Clip c = Make(sampleRate, 0.12f);
            AddDecayingSine(c, 70.0f, 0.8f, 0.05f);
            AddDecayingSine(c, 1600.0f, 0.3f, 0.01f);
            AddNoiseBurst(c, 0.5f, 0.02f, 2500.0f, 0.0f, 55u);
            Normalise(c, 0.55f);
            return c;
        }

        Clip Impact(const int sampleRate, const float strength)
        {
            const float s = std::clamp(strength, 0.0f, 1.0f);
            Clip c = Make(sampleRate, 0.35f + 0.4f * s);
            AddDecayingSine(c, 48.0f, 1.0f, 0.10f + 0.12f * s);
            AddNoiseBurst(c, 0.9f, 0.05f + 0.08f * s, 2500.0f + 3000.0f * s, 0.0f, 77u);
            AddDecayingSine(c, 820.0f, 0.25f * s, 0.15f, 0.01f);    // sheet metal ring
            AddDecayingSine(c, 1340.0f, 0.18f * s, 0.12f, 0.015f);
            Normalise(c, 0.5f + 0.45f * s);
            return c;
        }

        Clip StarterCatch(const int sampleRate)
        {
            Clip c = Make(sampleRate, 0.25f);
            AddNoiseBurst(c, 0.7f, 0.06f, 1800.0f, 0.0f, 13u);
            AddDecayingSine(c, 140.0f, 0.5f, 0.12f);
            Normalise(c, 0.35f);
            return c;
        }
    }

    RollingNoise::RollingNoise(const int sampleRate) : sampleRate_(sampleRate)
    {
        tyreLp_.SetCutoff(700.0f, sampleRate_);
        tyreLp2_.SetCutoff(700.0f, sampleRate_);
        windHp_.SetCutoff(250.0f, sampleRate_);
        windLp_.SetCutoff(1400.0f, sampleRate_);
        windLp2_.SetCutoff(1400.0f, sampleRate_);
    }

    void RollingNoise::Render(float* out, const int frames, const Input& target)
    {
        if (!primed_) {
            previous_ = target;
            primed_ = true;
        }
        for (int i = 0; i < frames; ++i) {
            const float t = frames > 1 ? static_cast<float>(i) / static_cast<float>(frames - 1) : 1.0f;
            const float v = std::max(0.0f, previous_.speedKmh + (target.speedKmh - previous_.speedKmh) * t);
            const float rough = previous_.surfaceRoughness + (target.surfaceRoughness - previous_.surfaceRoughness) * t;
            if ((i & 63) == 0) {
                tyreLp_.SetCutoff(250.0f + 6.0f * v, sampleRate_);
                tyreLp2_.SetCutoff(250.0f + 6.0f * v, sampleRate_);
            }
            // Tyre roar grows with speed to the power 1.5 up to 100 km/h and wind with its cube up to
            // 130 km/h. Both used to reach full level at town speeds (38 and 60 km/h), where they
            // were as loud as the engine and made driving sound like hiss.
            const float n = noise_.Next();
            const float tyreGain = std::min(0.45f, std::pow(v / 100.0f, 1.5f) * 0.45f);
            const float tyre = tyreLp2_.Process(tyreLp_.Process(n)) * tyreGain * rough * (target.grounded ? 1.0f : 0.2f);
            const float windGain = std::min(0.25f, std::pow(v / 130.0f, 3.0f) * 0.25f);
            const float wind = windLp2_.Process(windLp_.Process(windHp_.Process(noise_.Next()))) * windGain;
            out[i] += tyre + wind;
        }
        previous_ = target;
    }

    HornVoice::HornVoice(const int sampleRate) : sampleRate_(sampleRate) {}

    void HornVoice::Render(float* out, const int frames, const bool pressed)
    {
        const float dt = 1.0f / static_cast<float>(sampleRate_);
        const float rate = dt / 0.012f;
        for (int i = 0; i < frames; ++i) {
            gate_ += std::clamp((pressed ? 1.0f : 0.0f) - gate_, -rate, rate);
            if (gate_ <= 0.0f) {
                continue;
            }
            phaseA_ += 420.0 * dt;
            phaseB_ += 505.0 * dt;
            // Bright, slightly brassy dual tone: sawtooth-like via a few harmonics.
            float s = 0.0f;
            for (int h = 1; h <= 5; ++h) {
                const float a = 1.0f / static_cast<float>(h);
                s += a * (std::sin(static_cast<float>(std::fmod(phaseA_ * h, 1.0)) * kTwoPi) + std::sin(static_cast<float>(std::fmod(phaseB_ * h, 1.0)) * kTwoPi));
            }
            out[i] += 0.16f * gate_ * s;
        }
    }
}
