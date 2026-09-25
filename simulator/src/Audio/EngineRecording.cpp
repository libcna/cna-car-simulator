#include "CarSim/Audio/EngineRecording.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>

namespace CarSim::Audio
{
    namespace
    {
        constexpr int kSampleRate = 44100;
        constexpr int kLoopStart = 36 * kSampleRate / 10; // 3.6 s in the accepted recording
        constexpr int kLoopFade = 18 * kSampleRate / 100; // 0.18 s crossfade
        constexpr int kLoadLoopFade = 35 * kSampleRate / 100; // listener-approved 0.35 s overlap

        std::uint16_t U16(const std::vector<unsigned char>& bytes, const std::size_t at)
        {
            return static_cast<std::uint16_t>(bytes[at] | (static_cast<unsigned>(bytes[at + 1]) << 8));
        }

        std::uint32_t U32(const std::vector<unsigned char>& bytes, const std::size_t at)
        {
            return static_cast<std::uint32_t>(bytes[at]) | (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
                   (static_cast<std::uint32_t>(bytes[at + 2]) << 16) | (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
        }

        bool Tag(const std::vector<unsigned char>& bytes, const std::size_t at, const char* tag)
        {
            return bytes[at] == static_cast<unsigned char>(tag[0]) && bytes[at + 1] == static_cast<unsigned char>(tag[1]) &&
                   bytes[at + 2] == static_cast<unsigned char>(tag[2]) && bytes[at + 3] == static_cast<unsigned char>(tag[3]);
        }

        bool ReadWav(const std::string& path, const std::size_t minFrames, std::vector<float>& samples)
        {
            samples.clear();
            std::ifstream file(path, std::ios::binary);
            if (!file) return false;
            const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
            if (bytes.size() < 44 || !Tag(bytes, 0, "RIFF") || !Tag(bytes, 8, "WAVE")) return false;
            bool formatOk = false;
            std::size_t dataAt = 0, dataSize = 0;
            for (std::size_t at = 12; at + 8 <= bytes.size();) {
                const std::size_t size = U32(bytes, at + 4);
                const std::size_t start = at + 8;
                if (size > bytes.size() - start) return false;
                if (Tag(bytes, at, "fmt ") && size >= 16) {
                    formatOk = U16(bytes, start) == 1 && U16(bytes, start + 2) == 2 &&
                               U32(bytes, start + 4) == kSampleRate && U16(bytes, start + 12) == 4 &&
                               U16(bytes, start + 14) == 16;
                } else if (Tag(bytes, at, "data")) {
                    dataAt = start;
                    dataSize = size;
                }
                at = start + size + (size & 1u);
            }
            if (!formatOk || dataAt == 0 || dataSize % 4 != 0 || dataSize < minFrames * 4u) return false;
            samples.reserve(dataSize / 2);
            for (std::size_t at = dataAt; at < dataAt + dataSize; at += 2) {
                const int value = U16(bytes, at);
                samples.push_back(static_cast<float>(value < 32768 ? value : value - 65536) / 32768.0f);
            }
            return true;
        }
    }

    bool EngineRecording::LoadWav(const std::string& path)
    {
        samples_.clear();
        playing_ = false;
        gain_ = 0.0f;
        previousState_ = EngineSoundState::Off;
        return ReadWav(path, 8u * kSampleRate, samples_);
    }

    bool EngineRecording::LoadLoadWav(const std::string& path)
    {
        loadCursor_ = 0.0;
        loadGain_ = 0.0f;
        return ReadWav(path, 3u * kSampleRate, loadSamples_);
    }

    float EngineRecording::IdleShare(const float rpm)
    {
        const float t = std::clamp((rpm - 1250.0f) / 1750.0f, 0.0f, 1.0f);
        return 1.0f - t * t * (3.0f - 2.0f * t);
    }

    float EngineRecording::LoadShare(const float rpm, const float load)
    {
        const float t = std::clamp((rpm - 1700.0f) / 1800.0f, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t) * (0.4f + 0.6f * std::clamp(load, 0.0f, 1.0f));
    }

    float EngineRecording::Sample(const double frame, const int channel) const
    {
        const auto i = static_cast<std::size_t>(frame);
        const float t = static_cast<float>(frame - static_cast<double>(i));
        const std::size_t a = i * 2 + static_cast<std::size_t>(channel);
        const std::size_t b = std::min(a + 2, samples_.size() - 2 + static_cast<std::size_t>(channel));
        return samples_[a] + (samples_[b] - samples_[a]) * t;
    }

    float EngineRecording::LoadSample(const double frame, const int channel) const
    {
        const auto i = static_cast<std::size_t>(frame);
        const float t = static_cast<float>(frame - static_cast<double>(i));
        const std::size_t a = i * 2 + static_cast<std::size_t>(channel);
        const std::size_t b = std::min(a + 2, loadSamples_.size() - 2 + static_cast<std::size_t>(channel));
        return loadSamples_[a] + (loadSamples_[b] - loadSamples_[a]) * t;
    }

    void EngineRecording::Render(float* stereo, const int frames, const EngineSoundInput& input)
    {
        std::fill_n(stereo, static_cast<std::size_t>(frames) * 2, 0.0f);
        if (!Available()) return;
        const bool audible = input.state == EngineSoundState::Starting || input.state == EngineSoundState::Running;
        if (audible && !playing_) {
            cursor_ = input.state == EngineSoundState::Starting ? 0.0 : static_cast<double>(kLoopStart);
            playing_ = true;
            gain_ = 0.0f;
        }
        if (input.state == EngineSoundState::Starting && previousState_ != EngineSoundState::Starting) {
            cursor_ = 0.0; // each ignition plays the complete recorded crank and catch
            playing_ = true;
            gain_ = 0.0f;
        }
        previousState_ = input.state;
        if (!playing_) return;
        const int end = static_cast<int>(samples_.size() / 2);
        const float target = !audible ? 0.0f :
                             input.state == EngineSoundState::Starting ? 1.0f : IdleShare(input.rpm);
        const float gainStep = 1.0f / (kSampleRate * (audible ? 0.03f : 0.12f));
        const float rate = input.state == EngineSoundState::Starting ? 1.0f :
                           std::clamp(1.0f + (input.rpm - 850.0f) / 2400.0f, 0.9f, 1.5f);
        for (int i = 0; i < frames; ++i) {
            gain_ += std::clamp(target - gain_, -gainStep, gainStep);
            if (cursor_ >= end) cursor_ = kLoopStart + kLoopFade + (cursor_ - end);
            const bool fadingLoop = cursor_ >= end - kLoopFade;
            const float alpha = fadingLoop ? static_cast<float>((cursor_ - (end - kLoopFade)) / kLoopFade) : 0.0f;
            for (int channel = 0; channel < 2; ++channel) {
                const float head = Sample(cursor_, channel);
                const float tail = fadingLoop ? Sample(kLoopStart + cursor_ - (end - kLoopFade), channel) : 0.0f;
                stereo[static_cast<std::size_t>(i) * 2 + static_cast<std::size_t>(channel)] =
                    (head * (1.0f - alpha) + tail * alpha) * gain_ * 1.2f;
            }
            cursor_ += static_cast<double>(rate);
        }
        if (!audible && gain_ <= 0.0f) playing_ = false;
    }

    void EngineRecording::RenderLoad(float* stereo, const int frames, const EngineSoundInput& input)
    {
        if (!LoadAvailable()) return;
        const int end = static_cast<int>(loadSamples_.size() / 2);
        const float target = input.state == EngineSoundState::Running ? LoadShare(input.rpm, input.load) : 0.0f;
        const float gainStep = 1.0f / (kSampleRate * (target > loadGain_ ? 0.09f : 0.12f));
        // The source is a steady mid-RPM run. Modest resampling tracks the driven engine
        // without jumping to a new portion of a recording during every shift.
        const double rate = std::clamp(static_cast<double>(input.rpm) / 3500.0, 0.75, 1.35);
        for (int i = 0; i < frames; ++i) {
            loadGain_ += std::clamp(target - loadGain_, -gainStep, gainStep);
            if (loadCursor_ >= end) loadCursor_ = kLoadLoopFade + (loadCursor_ - end);
            const bool crossing = loadCursor_ >= end - kLoadLoopFade;
            const float alpha = crossing ? static_cast<float>((loadCursor_ - (end - kLoadLoopFade)) / kLoadLoopFade) : 0.0f;
            for (int channel = 0; channel < 2; ++channel) {
                const float first = LoadSample(loadCursor_, channel);
                const float next = crossing ? LoadSample(loadCursor_ - (end - kLoadLoopFade), channel) : 0.0f;
                stereo[static_cast<std::size_t>(i) * 2 + static_cast<std::size_t>(channel)] +=
                    (first * (1.0f - alpha) + next * alpha) * loadGain_;
            }
            loadCursor_ += rate;
        }
    }
}
