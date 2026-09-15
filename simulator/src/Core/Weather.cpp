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
        return false;
    }

    float WeatherState::TargetCloudCover(const WeatherKind kind)
    {
        switch (kind) {
            case WeatherKind::Clear: return 0.04f;
            case WeatherKind::FewClouds: return 0.35f;
            case WeatherKind::Overcast: return 0.95f;
            case WeatherKind::Rain: return 1.0f;
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
            default: break;
        }
        return 3.0f;
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
        // The road soaks quickly and dries slowly: about a minute to get wet, ten to dry out.
        if (rain > wetness) {
            Approach(wetness, rain, 60.0f, dt);
        } else {
            Approach(wetness, rain, 600.0f, dt);
        }
        cloudCover = std::clamp(cloudCover, 0.0f, 1.0f);
        rain = std::clamp(rain, 0.0f, 1.0f);
        wetness = std::clamp(wetness, 0.0f, 1.0f);
    }

    WeatherKind WeatherState::Next() const
    {
        return static_cast<WeatherKind>((static_cast<int>(kind) + 1) % static_cast<int>(WeatherKind::Count));
    }
}
