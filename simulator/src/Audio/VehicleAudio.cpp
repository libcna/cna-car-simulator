#include "CarSim/Audio/VehicleAudio.hpp"

#include "Microsoft/Xna/Framework/Audio/AudioChannels.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>

namespace CarSim::Audio
{
    using Microsoft::Xna::Framework::Audio::AudioChannels;
    using Microsoft::Xna::Framework::Audio::DynamicSoundEffectInstance;

    VehicleAudio::VehicleAudio(const bool enabled)
    {
        tick_ = Clips::IndicatorTick(kSampleRate);
        tock_ = Clips::IndicatorTock(kSampleRate);
        clunk_ = Clips::GearClunk(kSampleRate);
        catch_ = Clips::StarterCatch(kSampleRate);
        for (int i = 0; i < 4; ++i) {
            impacts_.push_back(Clips::Impact(kSampleRate, static_cast<float>(i) / 3.0f));
        }
        cabinLeft_.SetCutoff(levels.cockpitLowPassHz, kSampleRate);
        cabinRight_.SetCutoff(levels.cockpitLowPassHz, kSampleRate);
        mono_.assign(kBlockFrames, 0.0f);
        stereo_.assign(kBlockFrames * 2, 0.0f);
        pcm_.assign(static_cast<std::size_t>(kBlockFrames) * 4, 0);
        if (!enabled) {
            return;
        }
        try {
            stream_ = std::make_unique<DynamicSoundEffectInstance>(kSampleRate, AudioChannels::Stereo);
            stream_->setVolumeProperty(levels.master);
            enabled_ = true;
        } catch (const std::exception& ex) {
            std::cerr << "audio: disabled (" << ex.what() << ")\n";
            stream_.reset();
            enabled_ = false;
        }
    }

    VehicleAudio::~VehicleAudio()
    {
        if (stream_) {
            try {
                stream_->Stop(true);
            } catch (...) {
            }
        }
    }

    void VehicleAudio::Trigger(const Clip& clip, const float gain)
    {
        if (voices_.size() > 16) {
            voices_.erase(voices_.begin());
        }
        voices_.push_back(Voice{&clip, 0, gain});
    }

    void VehicleAudio::DetectEvents(const Sim::VehicleState& state, const std::vector<Collision::ContactEvent>& contacts)
    {
        // Indicator relay: tick on lamp-on, tock on lamp-off.
        const bool left = state.leftIndicatorLit;
        const bool right = state.rightIndicatorLit;
        const bool anyOn = left || right;
        const bool anyWas = prevLeft_ || prevRight_;
        if (anyOn && !anyWas) Trigger(tick_, 0.9f);
        if (!anyOn && anyWas) Trigger(tock_, 0.8f);
        prevLeft_ = left;
        prevRight_ = right;
        // Gear engagement.
        if (state.gear != prevGear_ && state.ignitionOn) {
            Trigger(clunk_, state.transmissionMode == Sim::TransmissionMode::Manual ? 0.8f : 0.4f);
        }
        prevGear_ = state.gear;
        // Starter catch.
        if (prevEngineState_ == Sim::EngineState::Starting && state.engineState == Sim::EngineState::Running) {
            Trigger(catch_, 0.9f);
        }
        prevEngineState_ = state.engineState;
        // Impacts (rate limited).
        for (const auto& c : contacts) {
            if (c.closingSpeed < 0.6f || impactCooldown_ > 0.0f) continue;
            const float strength = std::clamp(c.closingSpeed / 12.0f, 0.0f, 1.0f);
            const std::size_t index = std::min(impacts_.size() - 1, static_cast<std::size_t>(strength * 3.99f));
            Trigger(impacts_[index], 0.5f + 0.5f * strength);
            impactCooldown_ = 0.12f;
        }
        hornPressed_ = state.horn;
    }

    void VehicleAudio::RenderBlock(std::vector<float>& stereo, const Sim::VehicleState& state, const bool cockpit)
    {
        stereo.assign(static_cast<std::size_t>(kBlockFrames) * 2, 0.0f);
        std::fill(mono_.begin(), mono_.end(), 0.0f);

        EngineSoundInput engineInput;
        engineInput.rpm = state.engineRpm;
        engineInput.throttle = state.throttlePedal;
        // Delivered torque fraction from the engine model (0 on overrun); the throttle adds a
        // little presence so a blipped pedal is audible before the load builds up.
        engineInput.load = std::clamp(0.85f * state.engineLoad + 0.15f * state.throttlePedal, 0.0f, 1.0f);
        switch (state.engineState) {
            case Sim::EngineState::Off: engineInput.state = EngineSoundState::Off; break;
            case Sim::EngineState::Starting: engineInput.state = EngineSoundState::Starting; break;
            case Sim::EngineState::Running: engineInput.state = EngineSoundState::Running; break;
            case Sim::EngineState::Stalled: engineInput.state = EngineSoundState::Stalled; break;
        }
        engine_.Render(mono_.data(), kBlockFrames, engineInput);
        for (float& s : mono_) s *= levels.engine;

        RollingNoise::Input rolling;
        rolling.speedKmh = state.speedKmh;
        bool grounded = false;
        for (const auto& w : state.wheels) grounded = grounded || w.grounded;
        rolling.grounded = grounded;
        rolling.surfaceRoughness = 1.0f;
        std::vector<float> effects(static_cast<std::size_t>(kBlockFrames), 0.0f);
        rolling_.Render(effects.data(), kBlockFrames, rolling);
        horn_.Render(effects.data(), kBlockFrames, hornPressed_);
        for (auto& v : voices_) {
            for (int i = 0; i < kBlockFrames && v.position < v.clip->samples.size(); ++i, ++v.position) {
                effects[static_cast<std::size_t>(i)] += v.clip->samples[v.position] * v.gain;
            }
        }
        voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.position >= v.clip->samples.size(); }), voices_.end());

        // Cockpit: attenuate and low-pass; blend smoothly when the camera switches.
        const float target = cockpit ? 1.0f : 0.0f;
        const float blendStep = static_cast<float>(kBlockFrames) / static_cast<float>(kSampleRate) / 0.25f;
        cockpitBlend_ += std::clamp(target - cockpitBlend_, -blendStep, blendStep);
        const float insideGain = 1.0f - cockpitBlend_ * (1.0f - levels.cockpitAttenuation);
        for (int i = 0; i < kBlockFrames; ++i) {
            const float dry = mono_[static_cast<std::size_t>(i)] + effects[static_cast<std::size_t>(i)] * levels.effects;
            const float l = cabinLeft_.Process(dry);
            const float r = cabinRight_.Process(dry);
            const float outL = (dry + (l - dry) * cockpitBlend_) * insideGain;
            const float outR = (dry + (r - dry) * cockpitBlend_) * insideGain;
            stereo[static_cast<std::size_t>(i) * 2] = std::clamp(outL, -1.0f, 1.0f);
            stereo[static_cast<std::size_t>(i) * 2 + 1] = std::clamp(outR, -1.0f, 1.0f);
        }
    }

    void VehicleAudio::Update(const Sim::VehicleState& state, const bool cockpit, const std::vector<Collision::ContactEvent>& contacts, const float dt)
    {
        impactCooldown_ = std::max(0.0f, impactCooldown_ - dt);
        DetectEvents(state, contacts);
        if (!enabled_ || !stream_) {
            return;
        }
        try {
            int pending = stream_->getPendingBufferCountProperty();
            if (pending == 0 && blocksSubmitted_ > kTargetPendingBlocks) {
                ++underruns_;
            }
            int guard = 0;
            while (pending < kTargetPendingBlocks && guard++ < kTargetPendingBlocks + 1) {
                RenderBlock(stereo_, state, cockpit);
                for (std::size_t i = 0; i < stereo_.size(); ++i) {
                    const auto v = static_cast<std::int16_t>(std::lround(stereo_[i] * 32767.0f));
                    pcm_[i * 2] = static_cast<SharpRuntime::bytecs>(v & 0xFF);
                    pcm_[i * 2 + 1] = static_cast<SharpRuntime::bytecs>((v >> 8) & 0xFF);
                }
                stream_->SubmitBuffer(pcm_);
                ++blocksSubmitted_;
                ++pending;
            }
            if (blocksSubmitted_ >= kTargetPendingBlocks && stream_->getStateProperty() != Microsoft::Xna::Framework::Audio::SoundState::Playing) {
                stream_->Play();
            }
            stream_->setVolumeProperty(levels.master);
        } catch (const std::exception& ex) {
            std::cerr << "audio: stream error, disabling (" << ex.what() << ")\n";
            enabled_ = false;
        }
    }
}
