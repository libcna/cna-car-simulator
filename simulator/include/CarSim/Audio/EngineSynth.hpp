// Procedural engine sound: phase-continuous harmonic bank driven by engine speed and load,
// exhaust pulse train at the firing frequency, load/throttle-driven intake, valve-train
// whine and the starter.
// Pure DSP (no audio device); rendered into float buffers by the vehicle audio mixer.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace CarSim::Audio
{
    enum class EngineSoundState
    {
        Off,
        Starting,
        Running,
        Stalled
    };

    struct EngineSoundInput
    {
        float rpm = 0.0f;
        float load = 0.0f;        // 0..1 delivered torque fraction (0 on overrun)
        float throttle = 0.0f;    // 0..1 pedal
        EngineSoundState state = EngineSoundState::Off;
        int cylinders = 4;
    };

    /// End-of-block mix coefficients for the developer overlay (not measured acoustic levels).
    struct EngineSoundLevels
    {
        float rpm = 0.0f;
        float load = 0.0f;
        float throttle = 0.0f;
        float firingHz = 0.0f;
        float low = 0.0f;
        float upper = 0.0f;
        float exhaust = 0.0f;
        float intake = 0.0f;
        float master = 0.0f;
    };

    class EngineSynth
    {
    public:
        explicit EngineSynth(int sampleRate = 44100);

        /// Renders `frames` mono samples in [-1, 1] into `out` (overwrites). Parameters ramp
        /// linearly from the previous call's values so consecutive buffers stay continuous.
        void Render(float* out, int frames, const EngineSoundInput& target);
        void Reset();

        [[nodiscard]] int SampleRate() const { return sampleRate_; }
        [[nodiscard]] const EngineSoundLevels& Levels() const { return levels_; }
        /// Firing frequency (Hz) for a four-stroke engine at `rpm`.
        [[nodiscard]] static float FiringFrequency(float rpm, int cylinders);

    private:
        struct Pulse
        {
            float age = 1e9f;   // seconds since firing
        };

        [[nodiscard]] float NextNoise();
        [[nodiscard]] float NextIntakeNoise();

        int sampleRate_;
        double crankPhase_ = 0.0;      // crank revolutions (fractional)
        double whinePhase_ = 0.0;
        double starterPhase_ = 0.0;
        double resonancePhase_ = 0.0;
        std::array<Pulse, 6> pulses_{};
        int nextPulse_ = 0;
        std::uint32_t noiseState_ = 0x9E3779B9u;
        std::uint32_t intakeNoiseState_ = 0xB5297A4Du;
        float rumbleLp_ = 0.0f;
        float intakeLp_ = 0.0f;
        float intakeBassLp_ = 0.0f;
        float gain_ = 0.0f;            // master fade for off/stalled
        float starterGain_ = 0.0f;     // sample-rate fade between cranking and ignition catch
        float combustionGain_ = 0.0f;  // cranking motor precedes the running combustion layers
        EngineSoundInput previous_{};
        EngineSoundLevels levels_{};
        bool primed_ = false;
    };
}
