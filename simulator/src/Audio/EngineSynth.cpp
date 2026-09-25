#include "CarSim/Audio/EngineSynth.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace CarSim::Audio
{
    namespace
    {
        constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;

        struct Harmonic
        {
            float order;      // multiple of the crank frequency
            float amplitude;  // at full load
            float idleShare;  // fraction kept at zero load
        };

        // Four-cylinder character: dominant second order (firing), even orders, weaker odd orders.
        constexpr Harmonic kHarmonics[] = {
            {1.0f, 0.22f, 0.7f}, {2.0f, 1.00f, 0.45f}, {3.0f, 0.16f, 0.6f}, {4.0f, 0.55f, 0.4f},
            {5.0f, 0.10f, 0.6f}, {6.0f, 0.30f, 0.35f}, {8.0f, 0.18f, 0.3f}, {10.0f, 0.10f, 0.3f},
            {12.0f, 0.06f, 0.3f}, {16.0f, 0.03f, 0.3f},
        };
    }

    EngineSynth::EngineSynth(const int sampleRate) : sampleRate_(sampleRate > 8000 ? sampleRate : 44100) {}

    void EngineSynth::Reset()
    {
        crankPhase_ = whinePhase_ = starterPhase_ = resonancePhase_ = 0.0;
        for (auto& p : pulses_) p.age = 1e9f;
        gain_ = 0.0f;
        starterGain_ = 0.0f;
        rumbleLp_ = 0.0f;
        intakeLp_ = intakeBassLp_ = 0.0f;
        intakeNoiseState_ = 0xB5297A4Du;
        levels_ = {};
        primed_ = false;
    }

    float EngineSynth::FiringFrequency(const float rpm, const int cylinders)
    {
        return rpm / 60.0f * static_cast<float>(std::max(1, cylinders)) * 0.5f;
    }

    float EngineSynth::NextNoise()
    {
        noiseState_ = noiseState_ * 1664525u + 1013904223u;
        return static_cast<float>(noiseState_ >> 8) / static_cast<float>(1u << 24) * 2.0f - 1.0f;
    }

    float EngineSynth::NextIntakeNoise()
    {
        intakeNoiseState_ = intakeNoiseState_ * 1664525u + 1013904223u;
        return static_cast<float>(intakeNoiseState_ >> 8) / static_cast<float>(1u << 24) * 2.0f - 1.0f;
    }

    void EngineSynth::Render(float* out, const int frames, const EngineSoundInput& target)
    {
        if (!primed_) {
            previous_ = target;
            primed_ = true;
        }
        const float dt = 1.0f / static_cast<float>(sampleRate_);
        const bool audible = target.state == EngineSoundState::Running || target.state == EngineSoundState::Starting;
        const float targetGain = audible ? 1.0f : 0.0f;
        const float gainRate = dt / (audible ? 0.05f : 0.18f);
        const float starterTarget = target.state == EngineSoundState::Starting ? 1.0f : 0.0f;
        const float starterRate = dt / (starterTarget > 0.0f ? 0.025f : 0.040f);
        const float loadTarget = std::clamp(target.load, 0.0f, 1.0f);
        const float loadPrev = std::clamp(previous_.load, 0.0f, 1.0f);
        const float throttleTarget = std::clamp(target.throttle, 0.0f, 1.0f);
        const float throttlePrev = std::clamp(previous_.throttle, 0.0f, 1.0f);
        const float rpmPrev = std::max(0.0f, previous_.rpm);
        const float rpmTarget = std::max(0.0f, target.rpm);
        const int cylinders = std::max(1, target.cylinders);
        const float firePerRev = static_cast<float>(cylinders) * 0.5f;
        const float nyquist = 0.45f * static_cast<float>(sampleRate_);

        for (int i = 0; i < frames; ++i) {
            const float t = frames > 1 ? static_cast<float>(i) / static_cast<float>(frames - 1) : 1.0f;
            const float rpm = rpmPrev + (rpmTarget - rpmPrev) * t;
            const float load = loadPrev + (loadTarget - loadPrev) * t;
            const float throttle = throttlePrev + (throttleTarget - throttlePrev) * t;
            gain_ += std::clamp(targetGain - gain_, -gainRate, gainRate);
            starterGain_ += std::clamp(starterTarget - starterGain_, -starterRate, starterRate);

            const double f0 = static_cast<double>(rpm) / 60.0;
            const double previousPhase = crankPhase_;
            crankPhase_ += f0 * dt;
            // Firing events: every 1/firePerRev of a revolution.
            const double firings = static_cast<double>(firePerRev);
            if (std::floor(crankPhase_ * firings) != std::floor(previousPhase * firings) && rpm > 50.0f) {
                pulses_[static_cast<std::size_t>(nextPulse_)].age = 0.0f;
                nextPulse_ = (nextPulse_ + 1) % static_cast<int>(pulses_.size());
            }
            if (crankPhase_ > 1e6) crankPhase_ -= 1e6;

            // Harmonic bank.
            float harmonics = 0.0f;
            const float phase = static_cast<float>(std::fmod(crankPhase_, 1.0)) * kTwoPi;
            const float highRpm = std::clamp((rpm - 1800.0f) / 4300.0f, 0.0f, 1.0f);
            const float upperShare = highRpm * highRpm * (3.0f - 2.0f * highRpm);
            for (const Harmonic& h : kHarmonics) {
                const float freq = static_cast<float>(f0) * h.order;
                if (freq > nyquist) continue;
                float amp = h.amplitude * (h.idleShare + (1.0f - h.idleShare) * load);
                // Lower orders carry idle and low-speed cruising. The upper bank crossfades
                // in with RPM rather than making the idle sound like a pitched-up buzz.
                if (h.order >= 6.0f) amp *= 0.50f + 0.50f * upperShare;
                if (h.order <= 2.0f) amp *= 1.08f - 0.13f * upperShare;
                harmonics += amp * std::sin(phase * h.order);
            }
            // Low-order rumble is slightly low-passed so that high rpm does not become buzzy.
            rumbleLp_ += (harmonics - rumbleLp_) * std::min(1.0f, kTwoPi * 1800.0f * dt);
            harmonics = rumbleLp_;

            // Exhaust pulses: short decaying noise bursts with a resonant thump.
            float pulses = 0.0f;
            const float fire = std::max(5.0f, FiringFrequency(rpm, cylinders));
            const float tau = 0.22f / fire;
            const float resonance = 95.0f + 0.01f * rpm;
            resonancePhase_ += resonance * dt;
            for (auto& p : pulses_) {
                if (p.age < 4.0f * tau) {
                    const float env = std::exp(-p.age / tau);
                    pulses += env * (0.24f * NextNoise() + 0.8f * std::sin(static_cast<float>(resonancePhase_) * kTwoPi + p.age * 40.0f));
                }
                p.age += dt;
            }
            pulses *= 0.25f + 0.75f * load;

            // The former continuous broadband intake layer hissed even at idle. The exhaust
            // pulses already provide the irregular texture; keep the sustained engine tonal.
            whinePhase_ += f0 * 7.5 * dt;
            const float whine = 0.045f * std::min(1.0f, rpm / 6000.0f) * std::sin(static_cast<float>(std::fmod(whinePhase_, 1.0)) * kTwoPi);

            // Intake rasp is band-limited and gated by the firing rhythm. It follows the
            // actual pedal independently of torque, making a throttle blip audible without
            // a permanent idle hiss or a sharp layer switch at the next audio block.
            const float intakeCutoff = 450.0f + 0.40f * rpm;
            intakeLp_ += (NextIntakeNoise() - intakeLp_) * std::min(1.0f, kTwoPi * intakeCutoff * dt);
            intakeBassLp_ += (intakeLp_ - intakeBassLp_) * std::min(1.0f, kTwoPi * 180.0f * dt);
            const float intakeRise = std::clamp((rpm - 1000.0f) / 4500.0f, 0.0f, 1.0f);
            const float inhale = std::max(0.0f, std::sin(phase * 2.0f));
            const float intake = (intakeLp_ - intakeBassLp_) * 0.22f * throttle *
                                 (0.2f + 0.8f * load) * intakeRise * (0.30f + 0.70f * inhale * inhale);

            float sample = harmonics * 0.30f * (0.45f + 0.55f * load) + pulses * 0.22f + whine + intake;

            // Starter motor: whine with a slow wobble while cranking.
            if (starterGain_ > 0.0f) {
                starterPhase_ += 96.0 * dt;
                const float wobble = 0.85f + 0.15f * std::sin(static_cast<float>(previousPhase) * kTwoPi * 2.0f);
                sample += starterGain_ * 0.10f * wobble * (std::sin(static_cast<float>(std::fmod(starterPhase_, 1.0)) * kTwoPi) +
                                                           0.4f * std::sin(static_cast<float>(std::fmod(starterPhase_ * 2.0, 1.0)) * kTwoPi));
            }
            // Soft limiter.
            sample = std::tanh(sample * 1.4f) * 0.8f;
            out[i] = sample * gain_;
        }
        previous_ = target;
        const float highRpm = std::clamp((rpmTarget - 1800.0f) / 4300.0f, 0.0f, 1.0f);
        const float upperShare = highRpm * highRpm * (3.0f - 2.0f * highRpm);
        const float tonal = 0.30f * (0.45f + 0.55f * loadTarget);
        levels_ = {rpmTarget, loadTarget, throttleTarget, FiringFrequency(rpmTarget, cylinders),
                   tonal * (1.08f - 0.13f * upperShare), tonal * (0.50f + 0.50f * upperShare),
                   0.22f * (0.25f + 0.75f * loadTarget),
                   0.22f * throttleTarget * (0.20f + 0.80f * loadTarget) *
                       std::clamp((rpmTarget - 1000.0f) / 4500.0f, 0.0f, 1.0f),
                   gain_};
    }
}
