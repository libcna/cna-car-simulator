// Offline listening excerpts from the same VehicleAudio mixer used by the simulator.
// No audio device or display is opened; each scenario starts with a fresh deterministic mixer.
#include "CarSim/Audio/VehicleAudio.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using CarSim::Audio::TrafficSoundSource;
    using CarSim::Audio::VehicleAudio;
    using CarSim::Sim::EngineState;
    using CarSim::Sim::SurfaceType;
    using CarSim::Sim::TurboMode;
    using CarSim::Sim::VehicleState;
    using Microsoft::Xna::Framework::Vector3;

    struct Scenario
    {
        std::string_view name;
        float seconds;
        std::string_view description;
    };

    constexpr std::array scenarios{
        Scenario{"startup", 4.5f, "off, starter, catch, idle"},
        Scenario{"idle", 4.0f, "steady 850 rpm outside"},
        Scenario{"rpm_sweep", 7.0f, "850 to 5850 rpm and back"},
        Scenario{"load", 6.0f, "fixed 3000 rpm, light/full/light load"},
        Scenario{"lift_off", 5.0f, "4500 rpm load then closed-throttle overrun"},
        Scenario{"shifts", 6.0f, "three accelerating manual gears"},
        Scenario{"engine_braking", 5.0f, "closed throttle, 3500 to 1700 rpm"},
        Scenario{"tyre_road", 6.0f, "70 km/h asphalt, gravel, cobbles"},
        Scenario{"wind", 5.0f, "airborne, 0 to 130 km/h to isolate airflow"},
        Scenario{"rain", 6.0f, "wet 50 km/h with wipers, exterior then cabin"},
        Scenario{"snow", 5.0f, "snow-covered 50 km/h, exterior then cabin"},
        Scenario{"traffic_passby", 6.0f, "one car passing left to right at 28 m/s"},
        Scenario{"cabin_switch", 6.0f, "70 km/h cruise, exterior/cabin/exterior"},
        Scenario{"helicopter", 6.0f, "rotor modes at 5, 6, 7 and 8 Hz"},
    };

    void U16(std::ofstream& out, const std::uint16_t value)
    {
        const char bytes[2]{static_cast<char>(value & 0xffu), static_cast<char>((value >> 8) & 0xffu)};
        out.write(bytes, sizeof(bytes));
    }

    void U32(std::ofstream& out, const std::uint32_t value)
    {
        const char bytes[4]{static_cast<char>(value & 0xffu), static_cast<char>((value >> 8) & 0xffu),
                            static_cast<char>((value >> 16) & 0xffu), static_cast<char>((value >> 24) & 0xffu)};
        out.write(bytes, sizeof(bytes));
    }

    void WriteWav(const std::filesystem::path& path, const std::vector<std::int16_t>& samples)
    {
        const auto byteCount = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
        std::ofstream out(path, std::ios::binary);
        if (!out) throw std::runtime_error("cannot write " + path.string());
        out.write("RIFF", 4);
        U32(out, 36u + byteCount);
        out.write("WAVEfmt ", 8);
        U32(out, 16);
        U16(out, 1);  // PCM
        U16(out, 2);  // stereo
        U32(out, VehicleAudio::kSampleRate);
        U32(out, VehicleAudio::kSampleRate * 4u);
        U16(out, 4);
        U16(out, 16);
        out.write("data", 4);
        U32(out, byteCount);
        for (const auto sample : samples) U16(out, static_cast<std::uint16_t>(sample));
        if (!out) throw std::runtime_error("failed writing " + path.string());
    }

    void Configure(const std::string_view name, const float t, VehicleAudio& audio,
                   VehicleState& state, bool& cockpit, std::vector<TrafficSoundSource>& traffic)
    {
        state.engineState = EngineState::Running;
        state.ignitionOn = true;
        state.engineRpm = 850.0f;
        if (name == "startup") {
            if (t < 0.35f) { state.engineState = EngineState::Off; state.ignitionOn = false; state.engineRpm = 0.0f; }
            else if (t < 1.25f) { state.engineState = EngineState::Starting; state.engineRpm = 280.0f; }
            else state.engineRpm = 850.0f + 170.0f * std::exp(-(t - 1.25f) * 2.0f);
        } else if (name == "idle") {
            state.engineLoad = 0.05f;
        } else if (name == "rpm_sweep") {
            const float phase = t < 3.5f ? t / 3.5f : (7.0f - t) / 3.5f;
            state.engineRpm = 850.0f + 5000.0f * std::clamp(phase, 0.0f, 1.0f);
            state.throttlePedal = 0.25f + 0.55f * phase;
            state.engineLoad = 0.25f + 0.50f * phase;
        } else if (name == "load") {
            state.engineRpm = 3000.0f;
            state.throttlePedal = t >= 2.0f && t < 4.0f ? 1.0f : 0.20f;
            state.engineLoad = t >= 2.0f && t < 4.0f ? 0.95f : 0.20f;
        } else if (name == "lift_off") {
            state.speedKmh = 80.0f;
            state.engineRpm = t < 1.5f ? 4500.0f : std::max(2200.0f, 4500.0f - (t - 1.5f) * 660.0f);
            state.throttlePedal = t < 1.5f ? 0.85f : 0.0f;
            state.engineLoad = t < 1.5f ? 0.85f : 0.0f;
        } else if (name == "shifts") {
            state.speedKmh = 35.0f + t * 10.0f;
            state.gear = std::min(4, 1 + static_cast<int>(t / 1.5f));
            state.engineRpm = 2400.0f + 1400.0f * std::fmod(t, 1.5f) / 1.5f;
            state.throttlePedal = 0.7f;
            state.engineLoad = 0.70f;
        } else if (name == "engine_braking") {
            state.speedKmh = 70.0f - t * 8.0f;
            state.engineRpm = 3500.0f - t * 360.0f;
        } else if (name == "tyre_road" || name == "wind" || name == "rain" || name == "snow") {
            state.engineState = EngineState::Off;
            state.ignitionOn = false;
            state.engineRpm = 0.0f;
            state.speedKmh = name == "wind" ? t * 26.0f : name == "tyre_road" ? 70.0f : 50.0f;
            for (auto& wheel : state.wheels) {
                wheel.grounded = name != "wind";
                wheel.surface = name == "tyre_road" && t >= 2.0f && t < 4.0f ? SurfaceType::Gravel
                              : name == "tyre_road" && t >= 4.0f ? SurfaceType::Cobbles : SurfaceType::Asphalt;
            }
            if (name == "rain") {
                audio.SetWeather(1.0f, 1.0f);
                const float phase = std::fmod(t * 1.4f, 2.0f);
                state.wiperPosition = phase < 1.0f ? phase : 2.0f - phase;
                cockpit = t >= 3.0f;
            } else if (name == "snow") {
                audio.SetWeather(0.0f, 0.0f, 1.0f);
                cockpit = t >= 2.5f;
            }
        } else if (name == "traffic_passby") {
            state.engineState = EngineState::Off;
            state.ignitionOn = false;
            state.engineRpm = 0.0f;
            TrafficSoundSource source;
            source.id = 17;
            source.position = Vector3(-84.0f + t * 28.0f, 0.0f, -9.0f);
            source.velocity = Vector3(28.0f, 0.0f, 0.0f);
            source.speedMs = 28.0f;
            traffic.push_back(source);
        } else if (name == "cabin_switch") {
            state.speedKmh = 70.0f;
            state.engineRpm = 2900.0f;
            state.throttlePedal = 0.35f;
            state.engineLoad = 0.35f;
            for (auto& wheel : state.wheels) wheel.grounded = true;
            cockpit = t >= 2.0f && t < 4.0f;
        } else if (name == "helicopter") {
            state.flightMode = true;
            state.turboMode = t < 1.5f ? TurboMode::Off : t < 3.0f ? TurboMode::Turbo
                              : t < 4.5f ? TurboMode::Ultra : TurboMode::UltraUltra;
        }
    }

    void RenderScenario(const Scenario& scenario, const std::filesystem::path& outDir)
    {
        VehicleAudio audio(false, CARSIM_SOURCE_CONTENT_DIR);
        std::vector<std::int16_t> pcm;
        const int targetFrames = static_cast<int>(std::lround(scenario.seconds * VehicleAudio::kSampleRate));
        pcm.reserve(static_cast<std::size_t>(targetFrames) * 2);
        std::vector<float> block;
        double sumSq = 0.0;
        float peak = 0.0f;
        const auto renderAt = [&](const float t) {
            VehicleState state;
            bool cockpit = false;
            std::vector<TrafficSoundSource> traffic;
            audio.SetWeather(0.0f, 0.0f, 0.0f);
            Configure(scenario.name, t, audio, state, cockpit, traffic);
            audio.SetTrafficScene(std::span<const TrafficSoundSource>(traffic), Vector3(0, 0, 0),
                                  Vector3(0, 0, 0), Vector3(1, 0, 0));
            audio.Update(state, cockpit, {}, static_cast<float>(VehicleAudio::kBlockFrames) / VehicleAudio::kSampleRate);
            audio.RenderBlock(block, state, cockpit);
        };
        // Let steady states settle before capture. Startup deliberately begins with an off
        // engine so its starter and catch remain part of the actual exported timeline.
        if (scenario.name != "startup") {
            for (int warm = 0; warm < 22; ++warm) renderAt(0.0f);
        }
        for (int rendered = 0; rendered < targetFrames; rendered += VehicleAudio::kBlockFrames) {
            const float t = static_cast<float>(rendered) / static_cast<float>(VehicleAudio::kSampleRate);
            renderAt(t);
            const int frames = std::min(VehicleAudio::kBlockFrames, targetFrames - rendered);
            for (int i = 0; i < frames * 2; ++i) {
                const float sample = std::clamp(block[static_cast<std::size_t>(i)] * audio.levels.master, -1.0f, 1.0f);
                pcm.push_back(static_cast<std::int16_t>(std::lround(sample * 32767.0f)));
                sumSq += static_cast<double>(sample) * static_cast<double>(sample);
                peak = std::max(peak, std::fabs(sample));
            }
        }
        WriteWav(outDir / (std::string(scenario.name) + ".wav"), pcm);
        const float rms = static_cast<float>(std::sqrt(sumSq / static_cast<double>(pcm.size())));
        std::cout << scenario.name << ": " << scenario.description << ", " << scenario.seconds
                  << " s, peak " << peak << ", RMS " << rms << '\n';
    }
}

int main(const int argc, char* argv[])
{
    if (argc != 2) {
        std::cerr << "usage: carsim-audiopreview <output-directory>\n";
        return 2;
    }
    try {
        const std::filesystem::path outDir(argv[1]);
        std::filesystem::create_directories(outDir);
        for (const Scenario& scenario : scenarios) RenderScenario(scenario, outDir);
    } catch (const std::exception& ex) {
        std::cerr << "audio preview: " << ex.what() << '\n';
        return 1;
    }
}
