#include "CarSim/Audio/TrafficAudio.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <numbers>

namespace CarSim::Audio
{
    namespace
    {
        constexpr int kAudibleVoices = 6;
        constexpr int kRetainedVoices = 12;  // room for sources fading out as new cars arrive
        constexpr float kRangeM = 90.0f;
        constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;

        struct Candidate
        {
            int id = -1;
            float gain = 0.0f;
            float pan = 0.0f;
            float frequency = 35.0f;
        };
    }

    TrafficAudio::TrafficAudio(const int sampleRate) : sampleRate_(std::max(8000, sampleRate))
    {
        voices_.reserve(kRetainedVoices);
    }

    void TrafficAudio::SetScene(const std::span<const TrafficSoundSource> sources,
                                const Microsoft::Xna::Framework::Vector3& listener,
                                const Microsoft::Xna::Framework::Vector3& listenerVelocity,
                                const Microsoft::Xna::Framework::Vector3& listenerRight)
    {
        using Microsoft::Xna::Framework::Vector3;
        std::array<Candidate, kAudibleVoices> nearest{};
        for (const auto& source : sources) {
            const Vector3 delta = source.position - listener;
            const float distance = delta.Length();
            if (distance >= kRangeM) continue;
            const Vector3 direction = delta / std::max(0.1f, distance);
            const float proximity = 1.0f - distance / kRangeM;
            const float gain = (source.heavy ? 0.18f : 0.13f) * proximity * proximity *
                               (1.0f + 0.12f * std::clamp(source.acceleration, 0.0f, 2.0f));
            const float pan = std::clamp(Vector3::Dot(direction, listenerRight), -1.0f, 1.0f);
            const float closure = Vector3::Dot(source.velocity - listenerVelocity, -direction);
            const float doppler = std::clamp(1.0f + closure / 343.0f, 0.88f, 1.12f);
            const float frequency = (source.heavy ? 28.0f + source.speedMs * 1.2f :
                                     37.0f + source.speedMs * 2.4f) * doppler;
            Candidate candidate{source.id, gain, pan, frequency};
            for (auto& slot : nearest) {
                if (candidate.gain <= slot.gain) continue;
                std::swap(slot, candidate);
            }
        }
        for (auto& voice : voices_) voice.targetGain = 0.0f;
        for (const auto& c : nearest) {
            if (c.id < 0) continue;
            auto it = std::find_if(voices_.begin(), voices_.end(), [&](const Voice& v) { return v.id == c.id; });
            if (it == voices_.end()) {
                if (voices_.size() >= kRetainedVoices) {
                    it = std::min_element(voices_.begin(), voices_.end(), [](const Voice& a, const Voice& b) {
                        return a.targetGain == b.targetGain ? a.gain < b.gain : a.targetGain < b.targetGain;
                    });
                    *it = Voice{};
                } else {
                    voices_.emplace_back();
                    it = std::prev(voices_.end());
                }
                it->id = c.id;
                it->phase = static_cast<double>((static_cast<unsigned>(c.id) * 2654435761u) & 0xFFFFu) / 65536.0;
                it->frequency = c.frequency;
                it->pan = c.pan;
            }
            it->targetGain = c.gain;
            it->targetPan = c.pan;
            it->targetFrequency = c.frequency;
        }
    }

    void TrafficAudio::Render(float* stereo, const int frames)
    {
        if (!stereo || frames <= 0) return;
        const float gainRate = 1.0f / (0.09f * static_cast<float>(sampleRate_));
        const float motionRate = 1.0f / (0.12f * static_cast<float>(sampleRate_));
        for (auto& voice : voices_) {
            for (int i = 0; i < frames; ++i) {
                voice.gain += (voice.targetGain - voice.gain) * gainRate;
                voice.pan += (voice.targetPan - voice.pan) * motionRate;
                voice.frequency += (voice.targetFrequency - voice.frequency) * motionRate;
                voice.phase += static_cast<double>(voice.frequency) / static_cast<double>(sampleRate_);
                if (voice.phase >= 1.0) voice.phase -= 1.0;
                const float angle = static_cast<float>(voice.phase) * kTwoPi;
                const float pulse = 0.72f * std::sin(angle) + 0.22f * std::sin(angle * 2.0f);
                const float level = voice.gain * pulse;
                stereo[static_cast<std::size_t>(i) * 2] += level * std::sqrt(0.5f * (1.0f - voice.pan));
                stereo[static_cast<std::size_t>(i) * 2 + 1] += level * std::sqrt(0.5f * (1.0f + voice.pan));
            }
        }
        voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice& v) {
            return v.targetGain == 0.0f && v.gain < 0.0001f;
        }), voices_.end());
    }
}
