#include "CarSim/Audio/VehicleAudio.hpp"

#include "CarSim/Audio/AudioLayers.hpp"

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
        footstep_ = Clips::Footstep(kSampleRate);
        for (int i = 0; i < 4; ++i) {
            impacts_.push_back(Clips::Impact(kSampleRate, static_cast<float>(i) / 3.0f));
        }
        cabinLeft_.SetCutoff(levels.cockpitLowPassHz, kSampleRate);
        cabinRight_.SetCutoff(levels.cockpitLowPassHz, kSampleRate);
        mono_.assign(kBlockFrames, 0.0f);
        stereo_.assign(kBlockFrames * 2, 0.0f);
        trafficStereo_.assign(kBlockFrames * 2, 0.0f);
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

    float VehicleAudio::BurstRandom()
    {
        burstSeed_ = burstSeed_ * 1664525u + 1013904223u;
        return static_cast<float>((burstSeed_ >> 8) & 0xFFFFu) / 65535.0f;
    }

    void VehicleAudio::SetWeather(const float rain, const float wetness, const float snowCover)
    {
        rain_ = std::clamp(rain, 0.0f, 1.0f);
        wetness_ = std::clamp(wetness, 0.0f, 1.0f);
        snowCover_ = std::clamp(snowCover, 0.0f, 1.0f);
    }

    void VehicleAudio::SetTrafficScene(const std::span<const TrafficSoundSource> sources,
                                       const Microsoft::Xna::Framework::Vector3& listener,
                                       const Microsoft::Xna::Framework::Vector3& listenerVelocity,
                                       const Microsoft::Xna::Framework::Vector3& listenerRight)
    {
        traffic_.SetScene(sources, listener, listenerVelocity, listenerRight);
    }

    void VehicleAudio::Trigger(const Clip& clip, const float gain)
    {
        if (voices_.size() > 16) {
            voices_.erase(voices_.begin());
        }
        voices_.push_back(Voice{&clip, 0, gain});
    }

    void VehicleAudio::TriggerFootstep()
    {
        Trigger(footstep_, 0.75f);
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
            if (state.engineState == Sim::EngineState::Running && state.speedKmh > 3.0f) secondsSinceShift_ = 0.0f;
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
        // Layers: the gear-change dip cuts the load for a quarter second after a shift; overrun
        // adds irregular exhaust pops while the wheels drive the engine.
        const float blockSeconds = static_cast<float>(kBlockFrames) / static_cast<float>(kSampleRate);
        engineInput.load *= Layers::ShiftDip(secondsSinceShift_);
        engineInput.load = std::clamp(engineInput.load + Layers::OverrunBurble(blockIndex_, state.engineRpm, state.engineLoad, state.throttlePedal, state.speedKmh), 0.0f, 1.0f);
        secondsSinceShift_ = std::min(1e9f, secondsSinceShift_ + blockSeconds);
        ++blockIndex_;
        switch (state.engineState) {
            case Sim::EngineState::Off: engineInput.state = EngineSoundState::Off; break;
            case Sim::EngineState::Starting: engineInput.state = EngineSoundState::Starting; break;
            case Sim::EngineState::Running: engineInput.state = EngineSoundState::Running; break;
            case Sim::EngineState::Stalled: engineInput.state = EngineSoundState::Stalled; break;
        }
        if (state.flightMode) engineInput.state = EngineSoundState::Off;
        engine_.Render(mono_.data(), kBlockFrames, engineInput);
        for (float& s : mono_) s *= levels.engine;
        // Flight owns its rotor layer; the car engine above fades off when flight starts.
        const float rotorHz = state.turboMode == Sim::TurboMode::UltraUltra ? 8.0f :
                              state.turboMode == Sim::TurboMode::Ultra ? 7.0f :
                              state.turboMode == Sim::TurboMode::Turbo ? 6.0f : 5.0f;
        rotor_.RenderAdd(mono_.data(), kBlockFrames, state.flightMode, rotorHz, levels.engine);

        RollingNoise::Input rolling;
        rolling.speedKmh = state.speedKmh;
        rolling.snowCover = snowCover_;
        bool grounded = false;
        for (const auto& w : state.wheels) grounded = grounded || w.grounded;
        rolling.grounded = grounded;
        // Roughness of the surface under the grounded wheels (average), so gravel and grass
        // roar while asphalt hisses.
        float roughness = 0.0f;
        float slip = 0.0f;
        int groundedWheels = 0;
        for (const auto& w : state.wheels) {
            if (!w.grounded) continue;
            roughness += Layers::SurfaceRoughness(w.surface);
            slip += std::clamp(std::max(std::fabs(w.slipRatio), std::fabs(w.slipAngle) * 2.0f), 0.0f, 1.0f);
            ++groundedWheels;
        }
        rolling.surfaceRoughness = groundedWheels > 0 ? roughness / static_cast<float>(groundedWheels) : 1.0f;
        rolling.slip = groundedWheels > 0 ? slip / static_cast<float>(groundedWheels) : 0.0f;
        std::vector<float> effects(static_cast<std::size_t>(kBlockFrames), 0.0f);
        rolling_.Render(effects.data(), kBlockFrames, rolling);
        // Rain: a broadband hiss on the roof and the screen (louder inside the car, where the
        // drops land on the metal a hand's width above your head), plus the spray a wet road
        // throws up under the wheels, which follows speed rather than the rain itself.
        {
            const float roof = 0.13f * rain_ * (cockpit ? 1.6f : 1.0f);
            const float spray = 0.16f * wetness_ * std::clamp(state.speedKmh / 70.0f, 0.0f, 1.0f) * (grounded ? 1.0f : 0.0f);
            rainLp_.SetCutoff(3200.0f + 2600.0f * rain_, kSampleRate);
            rainHp_.SetCutoff(420.0f, kSampleRate);
            sprayLp_.SetCutoff(1500.0f + 12.0f * state.speedKmh, kSampleRate);
            const float rainStep = (roof - rainGain_) / static_cast<float>(kBlockFrames);
            const float sprayStep = (spray - sprayGain_) / static_cast<float>(kBlockFrames);
            for (int i = 0; i < kBlockFrames; ++i) {
                rainGain_ += rainStep;
                sprayGain_ += sprayStep;
                effects[static_cast<std::size_t>(i)] += rainHp_.Process(rainLp_.Process(rainNoise_.Next())) * rainGain_;
                effects[static_cast<std::size_t>(i)] += sprayLp_.Process(sprayNoise_.Next()) * sprayGain_;
            }
            rainGain_ = roof;
            sprayGain_ = spray;
        }
        // Raindrops on the roof: sparse short ticks on top of the hiss, so light rain patters
        // and heavy rain drums. Inside the car they are what you hear most.
        {
            const float rate = Layers::RainDropRate(rain_, state.speedKmh);
            dropHp_.SetCutoff(1800.0f, kSampleRate);
            const float level = cockpit ? 0.30f : 0.10f;
            for (int i = 0; i < kBlockFrames; ++i) {
                dropCredit_ += rate / static_cast<float>(kSampleRate);
                if (dropCredit_ >= 1.0f && drops_.size() < 24) {
                    dropCredit_ -= 1.0f;
                    Burst b;
                    b.amplitude = level * (0.3f + 0.7f * BurstRandom());
                    b.decay = 1.0f - 1.0f / (kSampleRate * (0.002f + 0.004f * BurstRandom()));
                    drops_.push_back(b);
                }
                float env = 0.0f;
                for (auto& b : drops_) {
                    env += b.amplitude;
                    b.amplitude *= b.decay;
                }
                effects[static_cast<std::size_t>(i)] += dropHp_.Process(dropNoise_.Next()) * env;
                if ((i & 63) == 0) {
                    drops_.erase(std::remove_if(drops_.begin(), drops_.end(), [](const Burst& b) { return b.amplitude < 1e-4f; }), drops_.end());
                }
            }
            dropCredit_ = std::min(dropCredit_, 2.0f);
        }
        // Puddles: the odd low whoosh as a wheel ploughs through standing water.
        {
            const float rate = grounded ? Layers::SplashRate(wetness_, state.speedKmh) : 0.0f;
            splashLp_.SetCutoff(900.0f, kSampleRate);
            for (int i = 0; i < kBlockFrames; ++i) {
                splashCredit_ += rate / static_cast<float>(kSampleRate) * (0.5f + BurstRandom());
                if (splashCredit_ >= 1.0f && splashes_.size() < 4) {
                    splashCredit_ -= 1.0f;
                    Burst b;
                    b.amplitude = (0.10f + 0.12f * BurstRandom()) * std::clamp(state.speedKmh / 70.0f, 0.3f, 1.2f);
                    b.decay = 1.0f - 1.0f / (kSampleRate * (0.12f + 0.10f * BurstRandom()));
                    splashes_.push_back(b);
                }
                float env = 0.0f;
                for (auto& b : splashes_) {
                    env += b.amplitude;
                    b.amplitude *= b.decay;
                }
                effects[static_cast<std::size_t>(i)] += splashLp_.Process(splashNoise_.Next()) * env;
            }
            splashes_.erase(std::remove_if(splashes_.begin(), splashes_.end(), [](const Burst& b) { return b.amplitude < 1e-4f; }), splashes_.end());
        }
        // Wipers: the rubber swishing across the glass with the blade's speed, the motor's hum
        // under it, and a knock where the blades turn round and where they park.
        {
            const float blockSeconds = static_cast<float>(kBlockFrames) / static_cast<float>(kSampleRate);
            const float delta = state.wiperPosition - prevWiper_;
            const float bladeSpeed = std::fabs(delta) / blockSeconds;
            const float direction = delta > 1e-5f ? 1.0f : (delta < -1e-5f ? -1.0f : 0.0f);
            if (direction != 0.0f && wiperDirection_ > 0.0f && direction < 0.0f) Trigger(clunk_, 0.08f);          // far end
            if (prevWiper_ > 0.0f && state.wiperPosition <= 0.0f) Trigger(clunk_, 0.06f);                          // parked
            if (direction != 0.0f) wiperDirection_ = direction;
            prevWiper_ = state.wiperPosition;
            const float target = Layers::WiperSwishGain(bladeSpeed, wetness_) * (cockpit ? 1.0f : 0.3f);
            wiperLp_.SetCutoff(2600.0f, kSampleRate);
            wiperHp_.SetCutoff(500.0f, kSampleRate);
            const float step = (target - wiperGain_) / static_cast<float>(kBlockFrames);
            const double motorStep = 2.0 * 3.14159265358979 * 95.0 / kSampleRate;
            for (int i = 0; i < kBlockFrames; ++i) {
                wiperGain_ += step;
                const float swish = wiperHp_.Process(wiperLp_.Process(wiperNoise_.Next()));
                const float motor = static_cast<float>(std::sin(wiperMotorPhase_)) * 0.35f;
                wiperMotorPhase_ += motorStep;
                effects[static_cast<std::size_t>(i)] += (swish + motor) * wiperGain_;
            }
            if (wiperMotorPhase_ > 1e6) wiperMotorPhase_ = std::fmod(wiperMotorPhase_, 2.0 * 3.14159265358979);
            wiperGain_ = target;
        }
        // Brake hiss: band-limited noise that grows with pedal travel and speed.
        {
            const float target = Layers::BrakeHissGain(state.brakePedal, state.speedKmh) * (grounded ? 1.0f : 0.0f);
            brakeLp_.SetCutoff(1400.0f, kSampleRate);
            const float step = (target - brakeGain_) / static_cast<float>(kBlockFrames);
            for (int i = 0; i < kBlockFrames; ++i) {
                brakeGain_ += step;
                effects[static_cast<std::size_t>(i)] += brakeLp_.Process(brakeNoise_.Next()) * brakeGain_;
            }
            brakeGain_ = target;
        }
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
            stereo[static_cast<std::size_t>(i) * 2] = outL;
            stereo[static_cast<std::size_t>(i) * 2 + 1] = outR;
        }
        std::fill(trafficStereo_.begin(), trafficStereo_.end(), 0.0f);
        traffic_.Render(trafficStereo_.data(), kBlockFrames);
        const float trafficGain = levels.effects * insideGain * (1.0f - 0.55f * cockpitBlend_);
        for (std::size_t i = 0; i < stereo.size(); ++i) {
            const float mixed = stereo[i] + trafficStereo_[i] * trafficGain;
            // Leave ordinary levels untouched; approach the PCM ceiling smoothly only for
            // unusually loud overlaps of horn, collision and several nearby engines.
            const float magnitude = std::fabs(mixed);
            stereo[i] = magnitude <= 0.9f ? mixed : std::copysign(0.9f + 0.1f * std::tanh((magnitude - 0.9f) * 10.0f), mixed);
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
