#include "CarSim/Render/InstrumentCluster.hpp"

#include "CarSim/Render/Image.hpp"
#include "CarSim/Render/ProceduralTextures.hpp"

#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace CarSim::Render
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    namespace
    {
        constexpr float kDeg = std::numbers::pi_v<float> / 180.0f;
        constexpr int kIconSize = 64;

        const Color kFaceDark(22, 23, 26, 255);
        const Color kFaceRim(48, 50, 56, 255);
        const Color kTickWhite(236, 236, 232, 255);
        const Color kTickDim(150, 150, 148, 255);
        const Color kRedZone(200, 40, 30, 255);
        const Color kNeedle(232, 70, 40, 255);
        const Color kLampOff(58, 58, 60, 255);
        const Color kGreen(70, 220, 90, 255);
        const Color kBlue(70, 140, 255, 255);
        const Color kAmber(255, 170, 40, 255);
        const Color kRed(240, 60, 50, 255);

        Vector2 Polar(const Vector2& centre, const float angleDeg, const float radius)
        {
            const float a = (angleDeg - 90.0f) * kDeg;   // 0 deg = up
            return Vector2(centre.X + std::cos(a) * radius, centre.Y + std::sin(a) * radius);
        }

        /// Draws the icon atlas: one 64x64 cell per lamp, white shapes on transparent background.
        Image BuildIcons()
        {
            const int count = static_cast<int>(InstrumentCluster::Lamp::Count);
            Image atlas(kIconSize * count, kIconSize, Color(255, 255, 255, 0));
            const Color on(255, 255, 255, 255);
            auto cell = [&](const int index) { return index * kIconSize; };
            const int c = kIconSize / 2;
            // Indicator arrows.
            for (const int which : {0, 3}) {
                const int x0 = cell(which);
                const bool left = which == 0;
                for (int y = 8; y < kIconSize - 8; ++y) {
                    const int dy = std::abs(y - c);
                    const int tipHalf = (kIconSize / 2 - 8) - dy;   // triangle head
                    if (tipHalf < 0) continue;
                    for (int x = 0; x < tipHalf; ++x) {
                        atlas.Set(x0 + (left ? 8 + x : kIconSize - 9 - x), y, Rgb{1, 1, 1}, 1.0f);
                    }
                }
                // Shaft.
                atlas.FillRect(x0 + (left ? 30 : 14), c - 6, x0 + (left ? 50 : 34), c + 6, on);
            }
            // Headlight symbols (low beam index 2, high beam index 1): half-disc lamp with rays.
            for (const int which : {1, 2}) {
                const int x0 = cell(which);
                for (int y = 10; y < kIconSize - 10; ++y) {
                    for (int x = 34; x < 54; ++x) {
                        const float dx = static_cast<float>(x - 34);
                        const float dy = static_cast<float>(y - c);
                        if (dx * dx / (20.0f * 20.0f) + dy * dy / (22.0f * 22.0f) <= 1.0f && dx >= 0.0f) {
                            atlas.Set(x0 + x, y, Rgb{1, 1, 1}, 1.0f);
                        }
                    }
                }
                atlas.FillRect(x0 + 30, 10, x0 + 34, kIconSize - 10, on);
                for (int ray = 0; ray < 4; ++ray) {
                    const int y = 14 + ray * 12;
                    for (int x = 6; x < 26; ++x) {
                        const int yy = which == 2 ? y + (x - 6) / 4 : y;   // low beam rays slope down
                        atlas.FillRect(x0 + x, yy - 1, x0 + x + 1, yy + 2, on);
                    }
                }
            }
            // Handbrake: (P) in a circle.
            {
                const int x0 = cell(4);
                for (int y = 0; y < kIconSize; ++y) {
                    for (int x = 0; x < kIconSize; ++x) {
                        const float d = std::hypot(static_cast<float>(x - c), static_cast<float>(y - c));
                        if (d < 27.0f && d > 22.0f) atlas.Set(x0 + x, y, Rgb{1, 1, 1}, 1.0f);
                    }
                }
                atlas.FillRect(x0 + 24, 18, x0 + 29, 46, on);          // P stem
                atlas.FillRect(x0 + 24, 18, x0 + 40, 23, on);          // P top
                atlas.FillRect(x0 + 36, 18, x0 + 41, 34, on);          // P bowl right
                atlas.FillRect(x0 + 24, 30, x0 + 40, 35, on);          // P bowl bottom
            }
            // Coolant: thermometer in waves.
            {
                const int x0 = cell(5);
                atlas.FillRect(x0 + 29, 10, x0 + 35, 40, on);
                atlas.FillCircle(static_cast<float>(x0 + 32), 42.0f, 7.0f, on);
                for (int wave = 0; wave < 2; ++wave) {
                    const int y = 50 + wave * 7;
                    for (int x = 10; x < 54; ++x) {
                        const int yy = y + static_cast<int>(2.5f * std::sin(static_cast<float>(x) * 0.6f));
                        atlas.FillRect(x0 + x, yy, x0 + x + 1, yy + 3, on);
                    }
                }
                atlas.FillRect(x0 + 38, 16, x0 + 44, 19, on);
                atlas.FillRect(x0 + 38, 24, x0 + 44, 27, on);
                atlas.FillRect(x0 + 38, 32, x0 + 44, 35, on);
            }
            // Fuel: pump body with nozzle.
            {
                const int x0 = cell(6);
                atlas.FillRect(x0 + 14, 12, x0 + 40, 54, on);
                atlas.FillRect(x0 + 18, 16, x0 + 36, 28, Color(255, 255, 255, 0));
                atlas.FillRect(x0 + 10, 52, x0 + 44, 56, on);
                atlas.FillRect(x0 + 44, 20, x0 + 48, 46, on);
                atlas.FillRect(x0 + 44, 44, x0 + 54, 48, on);
                atlas.FillRect(x0 + 50, 14, x0 + 54, 48, on);
            }
            // Battery: box with terminals and +/-.
            {
                const int x0 = cell(7);
                atlas.FillRect(x0 + 8, 20, x0 + 56, 52, on);
                atlas.FillRect(x0 + 12, 24, x0 + 52, 48, Color(255, 255, 255, 0));
                atlas.FillRect(x0 + 14, 14, x0 + 24, 20, on);
                atlas.FillRect(x0 + 40, 14, x0 + 50, 20, on);
                atlas.FillRect(x0 + 16, 34, x0 + 26, 38, on);      // minus
                atlas.FillRect(x0 + 38, 34, x0 + 48, 38, on);      // plus
                atlas.FillRect(x0 + 41, 31, x0 + 45, 41, on);
            }
            // Engine: block silhouette.
            {
                const int x0 = cell(8);
                atlas.FillRect(x0 + 14, 22, x0 + 50, 48, on);
                atlas.FillRect(x0 + 22, 14, x0 + 40, 22, on);
                atlas.FillRect(x0 + 6, 28, x0 + 14, 42, on);
                atlas.FillRect(x0 + 50, 30, x0 + 58, 40, on);
                atlas.FillRect(x0 + 20, 48, x0 + 28, 54, on);
                atlas.FillRect(x0 + 36, 48, x0 + 44, 54, on);
            }
            // Premultiply for SpriteBatch's default AlphaBlend state.
            for (int y = 0; y < atlas.Height(); ++y) {
                for (int x = 0; x < atlas.Width(); ++x) {
                    const Color p = atlas.At(x, y);
                    const int a = static_cast<int>(p.getAProperty());
                    atlas.FillRect(x, y, x + 1, y + 1, Color(a, a, a, a));
                }
            }
            return atlas;
        }

        Image BuildNeedle()
        {
            // 24 x 160: tip at the top, pivot 24 px above the bottom; tapered, premultiplied alpha.
            Image img(24, 160, Color(0, 0, 0, 0));
            for (int y = 0; y < 160; ++y) {
                const float t = static_cast<float>(y) / 159.0f;          // 0 = tip
                const float half = 1.2f + 4.8f * t;
                for (int x = 0; x < 24; ++x) {
                    const float d = std::fabs(static_cast<float>(x) + 0.5f - 12.0f);
                    const float a = std::clamp(half - d + 0.5f, 0.0f, 1.0f);
                    const int v = static_cast<int>(a * 255.0f);
                    img.FillRect(x, y, x + 1, y + 1, Color(v, v, v, v));
                }
            }
            return img;
        }

        Image BuildDisc()
        {
            Image img(64, 64, Color(0, 0, 0, 0));
            for (int y = 0; y < 64; ++y) {
                for (int x = 0; x < 64; ++x) {
                    const float d = std::hypot(static_cast<float>(x) + 0.5f - 32.0f, static_cast<float>(y) + 0.5f - 32.0f);
                    const float a = std::clamp(31.5f - d, 0.0f, 1.0f);
                    const int v = static_cast<int>(a * 255.0f);
                    img.FillRect(x, y, x + 1, y + 1, Color(v, v, v, v));
                }
            }
            return img;
        }
    }

    InstrumentCluster::InstrumentCluster(GraphicsDevice& device, const Sim::VehicleDefinition& definition, const BitmapFont& gaugeFont,
                                         const BitmapFont& textFont, const BitmapFont& boldFont)
        : definition_(definition), gaugeFont_(gaugeFont), textFont_(textFont), boldFont_(boldFont)
    {
        target_ = std::make_unique<RenderTarget2D>(device, kWidth, kHeight, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                                   RenderTargetUsage::PreserveContents);
        background_ = std::make_unique<RenderTarget2D>(device, kWidth, kHeight, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                                       RenderTargetUsage::PreserveContents);
        white_ = UploadTexture(device, Textures::Solid(4, Color(255, 255, 255, 255)), false);
        disc_ = UploadTexture(device, BuildDisc(), true);
        needle_ = UploadTexture(device, BuildNeedle(), true);
        icons_ = UploadTexture(device, BuildIcons(), true);

        speedo_ = Dial{Vector2(262.0f, 232.0f), 186.0f, -135.0f, 270.0f};
        tacho_ = Dial{Vector2(762.0f, 232.0f), 186.0f, -135.0f, 270.0f};
        fuel_ = Dial{Vector2(262.0f, 318.0f), 56.0f, -50.0f, 100.0f};
        temp_ = Dial{Vector2(762.0f, 318.0f), 56.0f, -50.0f, 100.0f};
    }

    Texture2D* InstrumentCluster::Texture() const
    {
        return target_.get();
    }

    bool InstrumentCluster::LampLit(const Lamp lamp, const Sim::VehicleState& state)
    {
        const bool running = state.engineState == Sim::EngineState::Running;
        switch (lamp) {
            case Lamp::IndicatorLeft: return state.leftIndicatorLit;
            case Lamp::IndicatorRight: return state.rightIndicatorLit;
            case Lamp::HighBeam: return state.highBeam;
            case Lamp::LowBeam: return state.lowBeam && !state.highBeam;
            case Lamp::Handbrake: return state.ignitionOn && state.handbrake;
            case Lamp::Coolant: return state.ignitionOn && state.temperatureWarning;
            case Lamp::Fuel: return state.ignitionOn && state.reserveWarning;
            case Lamp::Battery: return state.ignitionOn && !running;
            case Lamp::Engine: return state.ignitionOn && (!running || state.engineState == Sim::EngineState::Starting);
            case Lamp::Count: break;
        }
        return false;
    }

    void InstrumentCluster::DrawTick(SpriteBatch& batch, const Dial& dial, const float angleDeg, const float innerRadius, const float length,
                                     const float thickness, const Color& color) const
    {
        const Vector2 p = Polar(dial.centre, angleDeg, innerRadius + length * 0.5f);
        batch.Draw(*white_, p, std::nullopt, color, angleDeg * kDeg, Vector2(2.0f, 2.0f), Vector2(thickness / 4.0f, length / 4.0f),
                   SpriteEffects::None, 0.0f);
    }

    void InstrumentCluster::DrawDialFace(SpriteBatch& batch, const Dial& dial, const float maxValue, const float majorStep, const float minorStep,
                                         const float labelScale, const float labelDivisor, const float redFrom) const
    {
        // Face: rim disc then dark face.
        const float rimScale = (dial.radius + 10.0f) * 2.0f / 64.0f;
        batch.Draw(*disc_, dial.centre, std::nullopt, kFaceRim, 0.0f, Vector2(32.0f, 32.0f), Vector2(rimScale, rimScale), SpriteEffects::None, 0.0f);
        const float faceScale = dial.radius * 2.0f / 64.0f;
        batch.Draw(*disc_, dial.centre, std::nullopt, kFaceDark, 0.0f, Vector2(32.0f, 32.0f), Vector2(faceScale, faceScale), SpriteEffects::None, 0.0f);
        // Red zone arc as dense short ticks.
        if (redFrom < maxValue) {
            for (float v = redFrom; v <= maxValue; v += maxValue / 400.0f) {
                const float a = dial.startDeg + dial.sweepDeg * (v / maxValue);
                DrawTick(batch, dial, a, dial.radius - 24.0f, 10.0f, 3.0f, kRedZone);
            }
        }
        // Minor and major ticks.
        for (float v = 0.0f; v <= maxValue + 1e-3f; v += minorStep) {
            const float a = dial.startDeg + dial.sweepDeg * (v / maxValue);
            const bool major = std::fabs(std::fmod(v + 1e-3f, majorStep)) < 1e-2f;
            DrawTick(batch, dial, a, dial.radius - (major ? 26.0f : 20.0f), major ? 18.0f : 10.0f, major ? 4.0f : 2.0f, major ? kTickWhite : kTickDim);
        }
        // Labels.
        for (float v = 0.0f; v <= maxValue + 1e-3f; v += majorStep) {
            const float a = dial.startDeg + dial.sweepDeg * (v / maxValue);
            const Vector2 p = Polar(dial.centre, a, dial.radius - 52.0f);
            char text[16];
            std::snprintf(text, sizeof(text), "%g", static_cast<double>(v / labelDivisor));
            const Vector2 size = gaugeFont_.Measure(text) * labelScale;
            gaugeFont_.Draw(batch, text, Vector2(p.X, p.Y - size.Y * 0.5f), kTickWhite, labelScale, TextAlign::Center);
        }
    }

    void InstrumentCluster::DrawNeedle(SpriteBatch& batch, const Dial& dial, const float fraction, const float length, const Color& color) const
    {
        const float f = std::clamp(fraction, -0.02f, 1.02f);
        const float angle = (dial.startDeg + dial.sweepDeg * f) * kDeg;
        const float scale = length / 136.0f;   // 136 px from pivot to tip in the needle image
        batch.Draw(*needle_, dial.centre, std::nullopt, color, angle, Vector2(12.0f, 136.0f), Vector2(scale, scale), SpriteEffects::None, 0.0f);
        const float hub = length * 0.16f * 2.0f / 64.0f;
        batch.Draw(*disc_, dial.centre, std::nullopt, Color(30, 30, 32, 255), 0.0f, Vector2(32.0f, 32.0f), Vector2(hub, hub), SpriteEffects::None, 0.0f);
    }

    void InstrumentCluster::DrawLamp(SpriteBatch& batch, const Lamp lamp, const Vector2 centre, const bool lit, const float scale) const
    {
        Color color = kLampOff;
        if (lit) {
            switch (lamp) {
                case Lamp::IndicatorLeft:
                case Lamp::IndicatorRight:
                case Lamp::LowBeam: color = kGreen; break;
                case Lamp::HighBeam: color = kBlue; break;
                case Lamp::Fuel:
                case Lamp::Engine: color = kAmber; break;
                default: color = kRed; break;
            }
        }
        const Rectangle source(static_cast<int>(lamp) * kIconSize, 0, kIconSize, kIconSize);
        batch.Draw(*icons_, centre, source, color, 0.0f, Vector2(kIconSize * 0.5f, kIconSize * 0.5f), Vector2(scale, scale), SpriteEffects::None, 0.0f);
    }

    void InstrumentCluster::BuildStatic(GraphicsDevice& device, SpriteBatch& batch)
    {
        device.SetRenderTarget(background_.get());
        device.Clear(Color(12, 12, 14, 255));
        batch.Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &SamplerState::LinearClamp, &DepthStencilState::None, &RasterizerState::CullNone);
        const auto& dash = definition_.dashboard;
        DrawDialFace(batch, speedo_, dash.speedometerMaxKmh, 20.0f, 10.0f, 0.34f, 1.0f, dash.speedometerMaxKmh + 1.0f);
        DrawDialFace(batch, tacho_, dash.tachometerMaxRpm, 1000.0f, 250.0f, 0.42f, 1000.0f, definition_.engine.redlineRpm);
        // Small gauges: three ticks each (E / half / F, cold / mid / hot).
        for (const Dial* d : {&fuel_, &temp_}) {
            const float rim = (d->radius + 4.0f) * 2.0f / 64.0f;
            batch.Draw(*disc_, d->centre, std::nullopt, kFaceRim, 0.0f, Vector2(32.0f, 32.0f), Vector2(rim, rim), SpriteEffects::None, 0.0f);
            const float face = d->radius * 2.0f / 64.0f;
            batch.Draw(*disc_, d->centre, std::nullopt, kFaceDark, 0.0f, Vector2(32.0f, 32.0f), Vector2(face, face), SpriteEffects::None, 0.0f);
            for (int i = 0; i <= 4; ++i) {
                const float a = d->startDeg + d->sweepDeg * static_cast<float>(i) / 4.0f;
                DrawTick(batch, *d, a, d->radius - 14.0f, i % 2 == 0 ? 10.0f : 6.0f, i % 2 == 0 ? 3.0f : 2.0f, kTickWhite);
            }
        }
        // Red band at the hot end of the temperature gauge.
        for (float f = 0.85f; f <= 1.0f; f += 0.01f) {
            DrawTick(batch, temp_, temp_.startDeg + temp_.sweepDeg * f, temp_.radius - 14.0f, 8.0f, 2.0f, kRedZone);
        }
        textFont_.Draw(batch, "km/h", Vector2(speedo_.centre.X, speedo_.centre.Y - 96.0f), kTickDim, 0.8f, TextAlign::Center);
        textFont_.Draw(batch, "1/min x1000", Vector2(tacho_.centre.X, tacho_.centre.Y - 96.0f), kTickDim, 0.7f, TextAlign::Center);
        textFont_.Draw(batch, "E", Vector2(fuel_.centre.X - 44.0f, fuel_.centre.Y - 26.0f), kTickDim, 0.7f, TextAlign::Center);
        textFont_.Draw(batch, "F", Vector2(fuel_.centre.X + 44.0f, fuel_.centre.Y - 26.0f), kTickDim, 0.7f, TextAlign::Center);
        textFont_.Draw(batch, "C", Vector2(temp_.centre.X - 44.0f, temp_.centre.Y - 26.0f), kTickDim, 0.7f, TextAlign::Center);
        textFont_.Draw(batch, "H", Vector2(temp_.centre.X + 44.0f, temp_.centre.Y - 26.0f), kTickDim, 0.7f, TextAlign::Center);
        // Central display bezel.
        batch.Draw(*white_, Vector2(452.0f, 150.0f), std::nullopt, Color(34, 35, 40, 255), 0.0f, Vector2(0.0f, 0.0f), Vector2(30.0f, 40.0f), SpriteEffects::None, 0.0f);
        batch.End();
        device.SetRenderTarget(nullptr);
        staticBuilt_ = true;
    }

    void InstrumentCluster::Render(GraphicsDevice& device, SpriteBatch& batch, const Sim::VehicleState& state, const double timeSeconds)
    {
        if (!staticBuilt_) {
            BuildStatic(device, batch);
        }
        device.SetRenderTarget(target_.get());
        device.Clear(Color(0, 0, 0, 255));
        batch.Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &SamplerState::LinearClamp, &DepthStencilState::None, &RasterizerState::CullNone);
        const bool ignition = state.ignitionOn;
        const Color backlight = ignition ? Color(255, 255, 255, 255) : Color(120, 120, 120, 255);
        batch.Draw(*background_, Vector2(0.0f, 0.0f), backlight);

        // Lamps: indicators and beams across the top centre, warnings along the bottom centre.
        const float topY = 52.0f;
        DrawLamp(batch, Lamp::IndicatorLeft, Vector2(462.0f, topY), LampLit(Lamp::IndicatorLeft, state), 0.9f);
        DrawLamp(batch, Lamp::HighBeam, Vector2(512.0f - 14.0f, topY), LampLit(Lamp::HighBeam, state), 0.75f);
        DrawLamp(batch, Lamp::LowBeam, Vector2(512.0f + 30.0f, topY), LampLit(Lamp::LowBeam, state), 0.75f);
        DrawLamp(batch, Lamp::IndicatorRight, Vector2(562.0f, topY), LampLit(Lamp::IndicatorRight, state), 0.9f);
        const float bottomY = 404.0f;
        const Lamp bottom[] = {Lamp::Handbrake, Lamp::Coolant, Lamp::Fuel, Lamp::Battery, Lamp::Engine};
        for (int i = 0; i < 5; ++i) {
            DrawLamp(batch, bottom[i], Vector2(408.0f + 52.0f * static_cast<float>(i), bottomY), LampLit(bottom[i], state), 0.7f);
        }

        // Central display: odometer, trip, gear and mode.
        char text[48];
        std::snprintf(text, sizeof(text), "%08.1f km", state.odometerKm);
        textFont_.Draw(batch, text, Vector2(512.0f, 176.0f), Color(220, 225, 210, 255), 0.95f, TextAlign::Center);
        std::snprintf(text, sizeof(text), "TRIP %6.1f km", state.tripKm);
        textFont_.Draw(batch, text, Vector2(512.0f, 208.0f), Color(180, 185, 170, 255), 0.75f, TextAlign::Center);
        boldFont_.Draw(batch, state.gearLabel, Vector2(512.0f, 236.0f), ignition ? Color(240, 240, 235, 255) : Color(120, 120, 120, 255), 1.15f, TextAlign::Center);
        textFont_.Draw(batch, state.transmissionMode == Sim::TransmissionMode::Automatic ? "AUTO" : "MANUAL", Vector2(512.0f, 292.0f),
                       Color(180, 185, 170, 255), 0.75f, TextAlign::Center);
        std::snprintf(text, sizeof(text), "%.1f l/h", static_cast<double>(state.instantConsumptionLPerH));
        textFont_.Draw(batch, text, Vector2(512.0f, 322.0f), Color(150, 155, 145, 255), 0.65f, TextAlign::Center);
        if (state.limiterActive || state.absActive) {
            boldFont_.Draw(batch, state.absActive ? "ABS" : "LIMIT", Vector2(512.0f, 106.0f), kAmber, 0.55f, TextAlign::Center);
        }
        (void)timeSeconds;

        // Needles: gauges fall to zero without ignition; the fuel gauge stays.
        const auto& dash = definition_.dashboard;
        const float speedFraction = ignition ? state.speedKmh / dash.speedometerMaxKmh : 0.0f;
        const float rpmFraction = ignition ? state.engineRpm / dash.tachometerMaxRpm : 0.0f;
        const float tempFraction = ignition ? (state.coolantC - dash.temperatureMinC) / std::max(1.0f, dash.temperatureMaxC - dash.temperatureMinC) : 0.0f;
        DrawNeedle(batch, fuel_, ignition ? state.fuelFraction : 0.0f, 44.0f, kNeedle);
        DrawNeedle(batch, temp_, std::clamp(tempFraction, 0.0f, 1.0f), 44.0f, kNeedle);
        DrawNeedle(batch, speedo_, speedFraction, 150.0f, kNeedle);
        DrawNeedle(batch, tacho_, rpmFraction, 150.0f, kNeedle);
        batch.End();
        device.SetRenderTarget(nullptr);
    }
}
