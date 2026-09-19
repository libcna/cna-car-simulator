#include "CarSim/Audio/EngineSynth.hpp"
#include "CarSim/Audio/SoundSynth.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace CarSim::Audio;

namespace
{
    constexpr int kRate = 44100;

    std::vector<float> RenderSeconds(EngineSynth& synth, const EngineSoundInput& in, float seconds, int block = 1024)
    {
        std::vector<float> out;
        const int total = static_cast<int>(seconds * kRate);
        std::vector<float> buffer(static_cast<std::size_t>(block));
        for (int done = 0; done < total; done += block) {
            synth.Render(buffer.data(), block, in);
            out.insert(out.end(), buffer.begin(), buffer.end());
        }
        return out;
    }

    float Rms(const std::vector<float>& s, std::size_t from)
    {
        double acc = 0.0;
        for (std::size_t i = from; i < s.size(); ++i) acc += static_cast<double>(s[i]) * s[i];
        return static_cast<float>(std::sqrt(acc / static_cast<double>(s.size() - from)));
    }

    /// Magnitude of the DFT at `freq` over the tail of the signal.
    float Magnitude(const std::vector<float>& s, float freq, std::size_t from)
    {
        double re = 0.0, im = 0.0;
        const std::size_t n = s.size() - from;
        for (std::size_t i = 0; i < n; ++i) {
            const double w = 2.0 * std::numbers::pi * freq * static_cast<double>(i) / kRate;
            re += s[from + i] * std::cos(w);
            im -= s[from + i] * std::sin(w);
        }
        return static_cast<float>(std::sqrt(re * re + im * im) / static_cast<double>(n));
    }
}

TEST(EngineSynth, SilentWhenOffAndFadesIn)
{
    EngineSynth synth(kRate);
    EngineSoundInput off;
    off.state = EngineSoundState::Off;
    off.rpm = 0.0f;
    const auto silence = RenderSeconds(synth, off, 0.5f);
    EXPECT_LT(Rms(silence, 0), 1e-4f);
    EngineSoundInput idle;
    idle.state = EngineSoundState::Running;
    idle.rpm = 850.0f;
    idle.load = 0.1f;
    const auto running = RenderSeconds(synth, idle, 1.0f);
    EXPECT_GT(Rms(running, running.size() / 2), 0.02f);
}

TEST(EngineSynth, FiringFrequencyTracksRpm)
{
    for (const float rpm : {1500.0f, 3000.0f, 4500.0f}) {
        EngineSynth synth(kRate);
        EngineSoundInput in;
        in.state = EngineSoundState::Running;
        in.rpm = rpm;
        in.load = 0.6f;
        const auto s = RenderSeconds(synth, in, 1.5f);
        const std::size_t from = s.size() / 3;
        const float fire = EngineSynth::FiringFrequency(rpm, 4);
        const float atFire = Magnitude(s, fire, from);
        // Off-frequency probes must be clearly weaker than the firing order.
        const float off1 = Magnitude(s, fire * 1.37f, from);
        const float off2 = Magnitude(s, fire * 0.71f, from);
        EXPECT_GT(atFire, off1 * 3.0f) << "rpm " << rpm;
        EXPECT_GT(atFire, off2 * 3.0f) << "rpm " << rpm;
    }
    EXPECT_FLOAT_EQ(EngineSynth::FiringFrequency(3000.0f, 4), 100.0f);
}

TEST(EngineSynth, LoadMakesItLouderAndBuffersStayContinuous)
{
    EngineSynth quiet(kRate);
    EngineSynth loud(kRate);
    EngineSoundInput in;
    in.state = EngineSoundState::Running;
    in.rpm = 2500.0f;
    in.load = 0.1f;
    const auto q = RenderSeconds(quiet, in, 1.0f);
    in.load = 1.0f;
    in.throttle = 1.0f;
    const auto l = RenderSeconds(loud, in, 1.0f);
    EXPECT_GT(Rms(l, l.size() / 2), Rms(q, q.size() / 2) * 1.3f);
    // Continuity across block boundaries: the jump between the last sample of one block and
    // the first of the next is no larger than the biggest jump inside a block.
    float maxInside = 0.0f;
    float maxBoundary = 0.0f;
    for (std::size_t i = kRate / 2; i + 1 < l.size(); ++i) {
        const float jump = std::fabs(l[i + 1] - l[i]);
        if ((i + 1) % 1024 == 0) maxBoundary = std::max(maxBoundary, jump); else maxInside = std::max(maxInside, jump);
    }
    EXPECT_LE(maxBoundary, maxInside * 1.05f + 1e-3f);
}

TEST(SoundSynth, ClipsAreBoundedAndShort)
{
    for (const Clip& c : {Clips::IndicatorTick(kRate), Clips::IndicatorTock(kRate), Clips::GearClunk(kRate), Clips::Impact(kRate, 1.0f), Clips::StarterCatch(kRate)}) {
        EXPECT_GT(c.samples.size(), 100u);
        EXPECT_LT(c.DurationSeconds(), 1.0f);
        float peak = 0.0f;
        for (const float s : c.samples) peak = std::max(peak, std::fabs(s));
        EXPECT_LE(peak, 1.0f);
        EXPECT_GT(peak, 0.2f);
    }
    RollingNoise rolling(kRate);
    std::vector<float> slow(2048, 0.0f), fast(2048, 0.0f);
    RollingNoise::Input in;
    in.speedKmh = 10.0f;
    rolling.Render(slow.data(), 2048, in);
    in.speedKmh = 90.0f;
    rolling.Render(fast.data(), 2048, in);
    rolling.Render(fast.data(), 2048, in);
    EXPECT_GT(Rms(fast, 0), Rms(slow, 0) * 2.0f);
}

namespace
{
    /// Mean DFT power per probe over [lo, hi], probed every `step` Hz.
    float BandPower(const std::vector<float>& s, float lo, float hi, float step, std::size_t from)
    {
        double sum = 0.0;
        int probes = 0;
        for (float f = lo; f <= hi; f += step, ++probes) {
            const float m = Magnitude(s, f, from);
            sum += static_cast<double>(m) * m;
        }
        return static_cast<float>(sum / std::max(1, probes));
    }
}

TEST(RollingNoise, IsARoadRoarNotAHissAndStaysBelowTheEngineInTown)
{
    // Tyre and wind noise used to reach full level by 38 and 60 km/h -- as loud as the engine,
    // with over a third of its energy above 2 kHz -- so driving through town sounded like hiss.
    const auto render = [](float kmh) {
        RollingNoise rolling(kRate);
        RollingNoise::Input in;
        in.speedKmh = kmh;
        std::vector<float> s(static_cast<std::size_t>(kRate), 0.0f);
        for (std::size_t at = 0; at < s.size(); at += 1024) {
            rolling.Render(s.data() + at, static_cast<int>(std::min<std::size_t>(1024, s.size() - at)), in);
        }
        return s;
    };
    const auto town = render(50.0f);
    const auto road = render(90.0f);
    const std::size_t from = town.size() / 2;
    EXPECT_LT(Rms(town, from), 0.03f) << "tyre roar at 50 km/h should sit well below the engine";
    EXPECT_GT(Rms(road, from), Rms(town, from) * 2.0f) << "and still grow clearly with speed";
    const float roar = BandPower(town, 200.0f, 500.0f, 25.0f, from);
    const float hiss = BandPower(town, 3000.0f, 6000.0f, 100.0f, from);
    EXPECT_LT(hiss, roar * 0.01f) << "the 3-6 kHz hiss band must sit at least 20 dB under the roar";
}

TEST(EngineSynth, IdleHasNoContinuousHighFrequencyHiss)
{
    EngineSynth synth(kRate);
    EngineSoundInput idle;
    idle.state = EngineSoundState::Running;
    idle.rpm = 850.0f;
    idle.load = 0.1f;
    const auto sound = RenderSeconds(synth, idle, 1.0f);
    const std::size_t from = sound.size() / 2;
    const float engineBand = BandPower(sound, 30.0f, 300.0f, 10.0f, from);
    const float hissBand = BandPower(sound, 3000.0f, 6000.0f, 100.0f, from);
    EXPECT_LT(hissBand, engineBand * 0.01f);
}
