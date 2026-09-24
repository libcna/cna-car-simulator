#include "CarSim/Core/Weather.hpp"

#include <algorithm>
#include <cctype>

namespace CarSim::Core
{
    namespace
    {
        /// Eases `value` towards `target` with a time constant of `seconds`.
        void Approach(float& value, const float target, const float seconds, const float dt)
        {
            if (seconds <= 0.0f) {
                value = target;
                return;
            }
            const float k = std::clamp(dt / seconds, 0.0f, 1.0f);
            value += (target - value) * k;
        }
    }

    const char* ToString(const WeatherKind kind)
    {
        switch (kind) {
            case WeatherKind::Clear: return "clear";
            case WeatherKind::FewClouds: return "few-clouds";
            case WeatherKind::Overcast: return "overcast";
            case WeatherKind::Rain: return "rain";
            case WeatherKind::Fog: return "fog";
            case WeatherKind::Snow: return "snow";
            default: break;
        }
        return "few-clouds";
    }

    const char* Describe(const WeatherKind kind)
    {
        switch (kind) {
            case WeatherKind::Clear: return "Clear";
            case WeatherKind::FewClouds: return "Scattered cloud";
            case WeatherKind::Overcast: return "Overcast";
            case WeatherKind::Rain: return "Rain";
            case WeatherKind::Fog: return "Fog";
            case WeatherKind::Snow: return "Snow";
            default: break;
        }
        return "Scattered cloud";
    }

    bool WeatherFromName(const std::string& name, WeatherKind& out)
    {
        std::string key;
        key.reserve(name.size());
        for (const char c : name) {
            if (c == '-' || c == '_' || c == ' ') continue;
            key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        if (key == "clear" || key == "sunny") { out = WeatherKind::Clear; return true; }
        if (key == "few" || key == "fewclouds" || key == "cloudy" || key == "scattered") { out = WeatherKind::FewClouds; return true; }
        if (key == "overcast" || key == "grey" || key == "gray") { out = WeatherKind::Overcast; return true; }
        if (key == "rain" || key == "rainy" || key == "wet") { out = WeatherKind::Rain; return true; }
        if (key == "fog" || key == "foggy" || key == "mist") { out = WeatherKind::Fog; return true; }
        if (key == "snow" || key == "snowy" || key == "winter") { out = WeatherKind::Snow; return true; }
        return false;
    }

    float WeatherState::TargetCloudCover(const WeatherKind kind)
    {
        switch (kind) {
            case WeatherKind::Clear: return 0.04f;
            case WeatherKind::FewClouds: return 0.35f;
            case WeatherKind::Overcast: return 0.95f;
            case WeatherKind::Rain: return 1.0f;
            case WeatherKind::Fog: return 0.9f;
            case WeatherKind::Snow: return 1.0f;
            default: break;
        }
        return 0.35f;
    }

    float WeatherState::TargetRain(const WeatherKind kind)
    {
        return kind == WeatherKind::Rain ? 1.0f : 0.0f;
    }

    float WeatherState::TargetWindSpeed(const WeatherKind kind)
    {
        switch (kind) {
            case WeatherKind::Clear: return 1.5f;
            case WeatherKind::FewClouds: return 3.0f;
            case WeatherKind::Overcast: return 5.0f;
            case WeatherKind::Rain: return 8.0f;
            case WeatherKind::Fog: return 0.5f;
            case WeatherKind::Snow: return 4.0f;
            default: break;
        }
        return 3.0f;
    }

    float WeatherState::TargetFog(const WeatherKind kind)
    {
        switch (kind) {
            case WeatherKind::Fog: return 1.0f;
            case WeatherKind::Snow: return 0.35f;   // falling snow closes the view too
            case WeatherKind::Rain: return 0.10f;
            default: break;
        }
        return 0.0f;
    }

    float WeatherState::TargetSnow(const WeatherKind kind)
    {
        return kind == WeatherKind::Snow ? 1.0f : 0.0f;
    }

    void WeatherState::Set(const WeatherKind next)
    {
        kind = next;
    }

    void WeatherState::Snap(const WeatherKind next)
    {
        kind = next;
        cloudCover = TargetCloudCover(next);
        rain = TargetRain(next);
        wetness = rain > 0.0f ? 1.0f : 0.0f;
        windSpeedMs = TargetWindSpeed(next);
        fog = TargetFog(next);
        snow = TargetSnow(next);
        snowCover = snow > 0.0f ? 1.0f : 0.0f;
    }

    void WeatherState::Update(const float dt)
    {
        if (dt <= 0.0f) {
            return;
        }
        // The sky changes over a couple of minutes; the rain starts and stops faster than that.
        Approach(cloudCover, TargetCloudCover(kind), 120.0f, dt);
        Approach(rain, TargetRain(kind), 40.0f, dt);
        Approach(windSpeedMs, TargetWindSpeed(kind), 90.0f, dt);
        Approach(fog, TargetFog(kind), 90.0f, dt);
        Approach(snow, TargetSnow(kind), 40.0f, dt);
        // Snow settles over a few minutes of snowfall and melts over twenty once it stops; the
        // melting snow keeps the road wet.
        const bool melting = snow < 0.1f && snowCover > 0.0f;
        if (snow > snowCover) {
            Approach(snowCover, snow, 180.0f, dt);
        } else {
            Approach(snowCover, snow, 1200.0f, dt);
            if (snowCover < 0.02f) snowCover = 0.0f;
        }
        const float wetTarget = std::max(rain, melting ? std::min(1.0f, snowCover * 1.5f) : 0.0f);
        // The road soaks quickly and dries slowly: about a minute to get wet, ten to dry out.
        if (wetTarget > wetness) {
            Approach(wetness, wetTarget, 60.0f, dt);
        } else {
            Approach(wetness, wetTarget, 600.0f, dt);
        }
        fog = std::clamp(fog, 0.0f, 1.0f);
        snow = std::clamp(snow, 0.0f, 1.0f);
        snowCover = std::clamp(snowCover, 0.0f, 1.0f);
        cloudCover = std::clamp(cloudCover, 0.0f, 1.0f);
        rain = std::clamp(rain, 0.0f, 1.0f);
        wetness = std::clamp(wetness, 0.0f, 1.0f);
    }

    WeatherKind WeatherState::Next() const
    {
        return static_cast<WeatherKind>((static_cast<int>(kind) + 1) % static_cast<int>(WeatherKind::Count));
    }
}
