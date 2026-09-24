// Clock, weather and lighting orchestration for the game frame loop. The methods remain
// SimulatorGame members because they coordinate the existing world, vehicle, audio and render
// owners; this file keeps that responsibility together without altering their algorithms.
#include "CarSim/App/SimulatorGame.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace CarSim::App
{
    void SimulatorGame::ApplyClockSettings()
    {
        // The save file remembers where the clock stood; the command line wins over it so that
        // captures are reproducible.
        timeOfDayHours_ = save_.settings.timeOfDayHours;
        timeScale_ = save_.settings.timeScale;
        if (options_.timeOfDay) {
            timeOfDayHours_ = *options_.timeOfDay;
        }
        if (options_.timeScale) {
            timeScale_ = *options_.timeScale;
        }
        timeOfDayHours_ = std::fmod(timeOfDayHours_, 24.0f);
        if (timeOfDayHours_ < 0.0f) {
            timeOfDayHours_ += 24.0f;
        }
        timeScale_ = std::clamp(timeScale_, 0.0f, 3600.0f);
        rig_.SetTimeOfDay(timeOfDayHours_);
        std::cout << "clock: " << FormatClock(timeOfDayHours_) << ", " << timeScale_
                  << "x (sun " << rig_.SunElevationDeg() << " deg)\n";
    }

    void SimulatorGame::ApplyWeatherSettings()
    {
        // The save file wins over the default and the command line over both; an unreadable name
        // falls back rather than failing the run, but it says so.
        Core::WeatherKind kind = Core::WeatherKind::FewClouds;
        if (!save_.settings.weather.empty() && !Core::WeatherFromName(save_.settings.weather, kind)) {
            std::cerr << "save: unknown weather '" << save_.settings.weather << "', using "
                      << Core::ToString(kind) << "\n";
        }
        if (options_.weather && !Core::WeatherFromName(*options_.weather, kind)) {
            std::cerr << "--weather: unknown weather '" << *options_.weather << "', using "
                      << Core::ToString(kind) << "\n";
        }
        weather_.Snap(kind);   // the weather is already settled when the world appears
        ApplyWeatherToWorld();
        if (vehicle_) {
            vehicle_->SetRoadWetness(weather_.wetness);
        }
        std::cout << "weather: " << Core::Describe(weather_.kind) << " (cover " << weather_.cloudCover
                  << ", rain " << weather_.rain << ")\n";
    }

    void SimulatorGame::ApplyWeatherToWorld()
    {
        rig_.SetWeather(weather_.cloudCover, weather_.rain);
        rig_.SetAtmosphere(weather_.fog, weather_.snowCover);
        lastWeatherCover_ = weather_.cloudCover;
        lastWeatherRain_ = weather_.rain;
        lastWeatherFog_ = weather_.fog;
        lastWeatherSnow_ = weather_.snowCover;
        if (worldRenderer_) {
            worldRenderer_->SetWetness(weather_.wetness);
        }
        RefreshLighting(true);
    }

    void SimulatorGame::UpdateTimeOfDay(const float dt)
    {
        if (timeScale_ > 0.0f) {
            timeOfDayHours_ += dt * timeScale_ / 3600.0f;
            if (timeOfDayHours_ >= 24.0f) timeOfDayHours_ -= 24.0f;
            rig_.SetTimeOfDay(timeOfDayHours_);
        }
        RefreshLighting(false);
    }

    void SimulatorGame::UpdateWeather(const float dt)
    {
        // The weather runs on the same accelerated clock as the sky, so a front passes in a few
        // minutes of play rather than a few hours.
        weather_.Update(dt * std::max(1.0f, timeScale_ / 60.0f));
        if (worldRenderer_) {
            worldRenderer_->SetWetness(weather_.wetness);
        }
        if (vehicleMaterials_) {
            vehicleMaterials_->SetWetness(weather_.wetness);
        }
        if (vehicle_) {
            vehicle_->SetRoadWetness(weather_.wetness);
        }
        if (worldRenderer_) {
            worldRenderer_->SetSnow(weather_.snowCover);
            worldRenderer_->UpdateSunShadows(getGraphicsDeviceProperty(), rig_.sunDirection);
        }
        if (vehicle_) {
            vehicle_->SetRoadSnow(weather_.snowCover);
        }
        if (traffic_) {
            traffic_->SetOvertakeWeather(weather_.wetness, weather_.fog, weather_.snowCover);
        }
        if (std::fabs(weather_.cloudCover - lastWeatherCover_) > 0.01f || std::fabs(weather_.rain - lastWeatherRain_) > 0.01f ||
            std::fabs(weather_.fog - lastWeatherFog_) > 0.01f || std::fabs(weather_.snowCover - lastWeatherSnow_) > 0.01f) {
            ApplyWeatherToWorld();
        }
        if (rain_) {
            rain_->Update(dt, weather_);
        }
        if (audio_) {
            audio_->SetWeather(weather_.rain, weather_.wetness, weather_.snowCover);
        }
    }

    void SimulatorGame::RefreshLighting(const bool force, const bool forceEnvironment)
    {
        // Re-applying a rig writes a handful of effect properties, so it is done whenever the sun
        // has moved far enough to matter -- which is much less far near the horizon, where the
        // whole sky turns over in twenty minutes, than at noon (LightingRig::RefreshStepDeg). The
        // paint's sky cube map costs a great deal more, so it follows every three degrees of sun
        // or fifteen per cent of cloud -- a weather front eases in over two minutes and would
        // otherwise rebuild it a hundred times on the way.
        const float elevation = rig_.SunElevationDeg();
        if (!force && std::fabs(elevation - lastLightingElevationDeg_) < Render::LightingRig::RefreshStepDeg(elevation)) {
            return;
        }
        lastLightingElevationDeg_ = elevation;
        const bool rebuildEnvironment = forceEnvironment ||
                                        std::fabs(elevation - lastEnvironmentElevationDeg_) > 3.0f ||
                                        std::fabs(weather_.cloudCover - lastEnvironmentCover_) > 0.15f;
        if (rebuildEnvironment) {
            lastEnvironmentElevationDeg_ = elevation;
            lastEnvironmentCover_ = weather_.cloudCover;
        }
        if (vehicleMaterials_) {
            vehicleMaterials_->ApplyLighting(getGraphicsDeviceProperty(), rig_, rebuildEnvironment);
        }
        if (worldRenderer_) {
            worldRenderer_->ApplyLighting();
        }
        if (sky_) {
            sky_->Refresh(getGraphicsDeviceProperty());
        }
    }

}
