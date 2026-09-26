// HUD, map, help and diagnostic overlays owned by SimulatorGame. All draw through the
// XNA-compatible public surface; this translation unit separates presentation from the
// simulation frame loop without changing overlay state or ordering.
#include "CarSim/App/SimulatorGame.hpp"
#include "CarSim/App/WalkingMode.hpp"
#include "CarSim/Sim/Units.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace CarSim::App
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    using Input::GameAction;

    void SimulatorGame::DrawHud()
    {
        auto& device = getGraphicsDeviceProperty();
        const auto s = vehicle_->Snapshot();
        const auto& vp = device.getViewportProperty();
        const float w = static_cast<float>(vp.getWidthProperty());
        const float h = static_cast<float>(vp.getHeightProperty());

        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &SamplerState::LinearClamp, &DepthStencilState::None,
                            &RasterizerState::CullNone);

        char buffer[128];
        std::snprintf(buffer, sizeof(buffer), "%3.0f km/h", static_cast<double>(walking_ ? (walkingMoving_ ? (running_ ? kRunningSpeedKmh : kWalkingSpeedKmh) : 0.0f) : s.speedKmh));
        fontBold_->DrawShadowed(*spriteBatch_, buffer, Vector2(w - 24.0f, h - 120.0f), Color(255, 255, 255, 235), 1.0f, Render::TextAlign::Right);
        if (walking_) std::snprintf(buffer, sizeof(buffer), "%s", running_ ? "RUNNING" : "WALKING");
        else std::snprintf(buffer, sizeof(buffer), "%4.0f rpm   %s   %s", static_cast<double>(s.engineRpm), s.gearLabel.c_str(),
                           s.transmissionMode == Sim::TransmissionMode::Automatic ? "AUTO" : "MANUAL");
        font_->DrawShadowed(*spriteBatch_, buffer, Vector2(w - 24.0f, h - 70.0f), Color(235, 235, 235, 220), 1.0f, Render::TextAlign::Right);
        std::string status = walking_ ? "On foot  Arrows move/turn  A/D sidestep" :
                            s.flightMode ? "Helicopter  Space climb  Q descend  J car" :
                            std::string("Engine: ") + Sim::ToString(s.engineState);
        if (grindHintSeconds_ > 0.0f && startRefusedHintSeconds_ <= 0.0f) {
            status += "  (press the clutch " + input_.KeysFor(GameAction::Clutch) + " to change gear, or " +
                      input_.KeysFor(GameAction::ToggleAutoClutch) + " for the automatic clutch)";
        }
        if (startRefusedHintSeconds_ > 0.0f) {
            status += s.transmissionMode == Sim::TransmissionMode::Automatic
                          ? "  (select P or N with the P / N key, then press E)"
                          : "  (hold the clutch Q or select neutral N, then press E)";
        }
        font_->DrawShadowed(*spriteBatch_, status, Vector2(w - 24.0f, h - 42.0f), Color(220, 220, 220, 200), 0.8f, Render::TextAlign::Right);
        std::string clock = FormatClock(timeOfDayHours_);
        if (timeScale_ <= 0.0f) {
            clock += " (frozen)";
        }
        clock += "   ";
        clock += Core::Describe(weather_.kind);
        font_->DrawShadowed(*spriteBatch_, clock, Vector2(w - 24.0f, 18.0f), Color(235, 235, 235, 190), 0.8f, Render::TextAlign::Right);

        std::string lamps;
        if (s.leftIndicatorLit) lamps += "<  ";
        if (s.lowBeam) lamps += s.highBeam ? "HIGH BEAM  " : "LIGHTS  ";
        if (s.limitedSlip) lamps += "LSD  ";
        if (s.autoClutch && s.transmissionMode == Sim::TransmissionMode::Manual) lamps += "AUTO CLUTCH  ";
        if (s.wiperMode == Sim::WiperMode::Intermittent) lamps += "WIPERS INT  ";
        if (s.wiperMode == Sim::WiperMode::Slow) lamps += "WIPERS  ";
        if (s.wiperMode == Sim::WiperMode::Fast) lamps += "WIPERS FAST  ";
        if (s.turboMode == Sim::TurboMode::Turbo) lamps += "TURBO  ";
        if (s.turboMode == Sim::TurboMode::Ultra) lamps += "ULTRA TURBO  ";
        if (s.turboMode == Sim::TurboMode::UltraUltra) lamps += "ULTRA ULTRA TURBO  ";
        if (s.flightMode) lamps += "FLIGHT  ";
        if (!exhaustSmokeEnabled_ && !s.flightMode) lamps += "SMOKE OFF  ";
        if (s.reserveWarning) lamps += "FUEL  ";
        if (s.handbrake) lamps += "(P)  ";
        if (s.rightIndicatorLit) lamps += "  >";
        if (!lamps.empty()) {
            fontBold_->DrawShadowed(*spriteBatch_, lamps, Vector2(w * 0.5f, 18.0f), Color(255, 200, 60, 230), 0.7f, Render::TextAlign::Center);
        }

        if (!showHelp_) {
            const std::string hint = walking_ ? input_.KeysFor(GameAction::ToggleWalk) + " return to car (nearby)   Shift run   F1 help" :
                                                "F1 help   C camera   E engine   M map   X smoke   L lights   K low/high";
            font_->DrawShadowed(*spriteBatch_, hint,
                                Vector2(20.0f, h - 34.0f), Color(230, 230, 230, 150), 0.7f);
        }

        spriteBatch_->End();
    }

    void SimulatorGame::DrawMap()
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int side = std::max(80, std::min({vp.getWidthProperty() - 48, vp.getHeightProperty() - 100, 650}));
        const int left = (vp.getWidthProperty() - side - 32) / 2;
        const int top = (vp.getHeightProperty() - side - 68) / 2;
        const int mapX = left + 16, mapY = top + 48;
        const auto& white = vehicleMaterials_->White();

        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &SamplerState::LinearClamp, &DepthStencilState::None,
                            &RasterizerState::CullNone);
        spriteBatch_->Draw(white, Rectangle(left, top, side + 32, side + 68), Color(7, 14, 20, 235));
        spriteBatch_->Draw(white, Rectangle(mapX - 2, mapY - 2, side + 4, side + 4), Color(185, 201, 194, 255));
        if (mapTexture_ && map_) {
            spriteBatch_->Draw(*mapTexture_, Rectangle(mapX, mapY, side, side), Color(255, 255, 255, 255));
            const auto& terrain = map_->Terrain();
            const auto position = walking_ ? walkingPosition_ : vehicle_->Snapshot().originPosition;
            const float u = std::clamp((position.X - terrain.MinX()) / (terrain.MaxX() - terrain.MinX()), 0.0f, 1.0f);
            const float v = std::clamp((position.Z - terrain.MinZ()) / (terrain.MaxZ() - terrain.MinZ()), 0.0f, 1.0f);
            const int x = mapX + static_cast<int>(u * side);
            const int y = mapY + static_cast<int>(v * side);
            spriteBatch_->Draw(white, Rectangle(x - 8, y - 8, 16, 16), Color(9, 19, 25, 255));
            spriteBatch_->Draw(white, Rectangle(x - 5, y - 5, 10, 10), Color(255, 203, 62, 255));
            Vector3 forward = walking_ ? Vector3(std::sin(walkingYaw_), 0.0f, -std::cos(walkingYaw_)) :
                                         vehicle_->Snapshot().worldMatrix.getForwardProperty();
            const float angle = std::atan2(forward.Z, forward.X);
            spriteBatch_->Draw(white, Rectangle(x, y, 22, 4), std::nullopt, Color(255, 203, 62, 255),
                                angle, Vector2(0.0f, 0.5f), SpriteEffects::None, 0.0f);
            fontBold_->Draw(*spriteBatch_, "MAP", Vector2(static_cast<float>(left + 16), static_cast<float>(top + 8)),
                            Color(245, 245, 235, 255), 0.58f);
            font_->Draw(*spriteBatch_, map_->Data().info.displayName,
                        Vector2(static_cast<float>(left + 110), static_cast<float>(top + 15)), Color(205, 220, 210, 255), 0.7f);
            font_->Draw(*spriteBatch_, "N", Vector2(static_cast<float>(mapX + side - 25), static_cast<float>(mapY + 8)),
                        Color(255, 255, 255, 240), 0.75f);
        } else {
            fontBold_->Draw(*spriteBatch_, "No map loaded", Vector2(static_cast<float>(mapX + 20), static_cast<float>(mapY + 20)),
                            Color(240, 240, 240, 255), 0.5f);
        }
        font_->Draw(*spriteBatch_, "M close", Vector2(static_cast<float>(left + side - 70), static_cast<float>(top + 15)),
                    Color(215, 225, 215, 255), 0.7f);
        spriteBatch_->End();
    }

    void SimulatorGame::DrawDebugOverlay()
    {
        // Everything here comes from project-owned instrumentation: our own timers, our own
        // renderers' batch counters and the vehicle snapshot. No renderer internals are read.
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const float w = static_cast<float>(vp.getWidthProperty());
        const float h = static_cast<float>(vp.getHeightProperty());
        const auto s = vehicle_->Snapshot();

        // Frame pacing over the rolling window: the mean is the frame rate you feel, the worst
        // 1 % is the stutter you notice.
        float pacingMean = 0.0f;
        float pacingWorst = 0.0f;
        if (framePacingCount_ > 0) {
            std::vector<float> window(framePacingMs_.begin(), framePacingMs_.begin() + framePacingCount_);
            for (const float v : window) pacingMean += v;
            pacingMean /= static_cast<float>(window.size());
            const std::size_t rank = window.size() - 1u - window.size() / 100u;
            std::nth_element(window.begin(), window.begin() + static_cast<std::ptrdiff_t>(rank), window.end());
            pacingWorst = window[rank];
        }
        const float fps = pacingMean > 0.0001f ? 1000.0f / pacingMean : 0.0f;

        const Render::WorldRenderStats world = worldRenderer_ ? worldRenderer_->Stats() : Render::WorldRenderStats{};
        const Render::TrafficRenderStats traffic = trafficRenderer_ ? trafficRenderer_->Stats() : Render::TrafficRenderStats{};
        const int vehicleDraws = vehicleRenderer_ ? vehicleRenderer_->DrawCallsLastFrame() : 0;
        const int vehicleTris = vehicleRenderer_ ? vehicleRenderer_->TriangleCount() : 0;
        const int mirrorEvery = std::max(1, save_.settings.mirrorUpdateEvery);
        const bool cockpit = cameraMode_ == Render::CameraMode::Cockpit && !options_.freeView;

        const auto text = [](const char* format, auto... args) {
            char buffer[192];
            std::snprintf(buffer, sizeof(buffer), format, args...);
            return std::string(buffer);
        };

        std::vector<std::pair<std::string, std::string>> rows;
        const auto section = [&rows](const char* title) { rows.emplace_back(title, std::string()); };
        const auto row = [&rows](std::string label, std::string value) { rows.emplace_back(std::move(label), std::move(value)); };

        section("frame");
        row("fps / frame", text("%.1f  (%.2f ms mean, %.2f ms worst 1%%)", static_cast<double>(fps),
                                static_cast<double>(pacingMean), static_cast<double>(pacingWorst)));
        row("cpu update / draw", text("%.2f / %.2f ms", static_cast<double>(frameMs_), static_cast<double>(drawMs_)));
        row("update split", text("vehicle %.2f  collision %.2f  traffic %.2f  audio %.2f ms",
                                 static_cast<double>(vehicleMs_), static_cast<double>(collisionMs_),
                                 static_cast<double>(trafficMs_), static_cast<double>(audioMs_)));
        if (audio_ && audio_->Enabled()) {
            row("audio stream", text("%d blocks queued ahead, %d underruns, %d traffic voices",
                                     Audio::VehicleAudio::kTargetPendingBlocks, audio_->Underruns(), audio_->TrafficVoices()));
            const auto& layers = audio_->EngineLevels();
            row("engine input", text("%.0f rpm  %.0f Hz firing  load %.2f  pedal %.2f",
                                     static_cast<double>(layers.rpm), static_cast<double>(layers.firingHz),
                                     static_cast<double>(layers.load), static_cast<double>(layers.throttle)));
            row("engine layers", text("low %.2f  upper %.2f  exhaust %.2f  intake %.2f  master %.2f",
                                      static_cast<double>(layers.low), static_cast<double>(layers.upper),
                                      static_cast<double>(layers.exhaust), static_cast<double>(layers.intake),
                                      static_cast<double>(layers.master)));
            const auto& environment = audio_->EnvironmentLevels();
            row("outside mix", text("road %.2f  wind %.2f  rain %.2f  spray %.2f  traffic %.2f",
                                     static_cast<double>(environment.roadGain), static_cast<double>(environment.windGain),
                                     static_cast<double>(environment.rainGain), static_cast<double>(environment.sprayGain),
                                     static_cast<double>(environment.trafficGain)));
            row("acoustics", text("cabin %.2f  snow %.2f", static_cast<double>(environment.cabinBlend),
                                   static_cast<double>(environment.snowCover)));
        }
        row("draw split", text("sky %.2f  world %.2f  traffic %.2f  car %.2f  hud %.2f ms",
                               static_cast<double>(passMs_[kPassSky]), static_cast<double>(passMs_[kPassWorld]),
                               static_cast<double>(passMs_[kPassTraffic]), static_cast<double>(passMs_[kPassVehicle]),
                               static_cast<double>(passMs_[kPassHud])));
        row("quality", text("%s   draw distance x%.2f   vegetation x%.2f   mirror %.0f m",
                            Render::ToString(quality_), static_cast<double>(qualitySettings_.drawDistanceScale),
                            static_cast<double>(qualitySettings_.vegetationScale),
                            static_cast<double>(qualitySettings_.mirrorDistanceM)));
        row("cluster / mirror", text("%.2f / %.2f ms   mirror %s, %d x %d, every %d frame(s)",
                                     static_cast<double>(passMs_[kPassCluster]), static_cast<double>(passMs_[kPassMirror]),
                                     (cockpit && mirrorEnabled_) ? "on" : "off",
                                     mirror_ ? mirror_->Width() : 0, mirror_ ? mirror_->Height() : 0, mirrorEvery));
        row("simulated", text("%.1f s   %d frames drawn", elapsedSeconds_, framesDrawn_));

        section(walking_ ? "walking / parked car" : "driving");
        row("speed / rpm / gear", text("%.1f km/h   %.0f rpm   %s (%s)", static_cast<double>(s.speedKmh),
                                       static_cast<double>(s.engineRpm), s.gearLabel.c_str(),
                                       s.transmissionMode == Sim::TransmissionMode::Automatic ? "auto" : "manual"));
        row("pedals", text("throttle %.2f  brake %.2f  clutch %.2f (%s)%s", static_cast<double>(s.throttlePedal),
                           static_cast<double>(s.brakePedal), static_cast<double>(s.clutchPedal),
                           s.clutchLocked ? "locked" : "slipping", s.handbrake ? "  handbrake" : ""));
        row("steering", text("%.1f deg wheel   front slip %+.1f deg   camera %s",
                             static_cast<double>(Sim::Units::RadToDeg(s.steeringWheelAngle)),
                             static_cast<double>(Sim::Units::RadToDeg(s.wheels.empty() ? 0.0f : s.wheels[0].slipAngle)),
                             options_.freeView ? "free" : (cockpit ? "cockpit" : "chase")));
        {
            // FL FR RL RR in the order the vehicle defines them; slip ratio and vertical load are
            // the two numbers that explain most handling complaints.
            static const char* const names[] = {"FL", "FR", "RL", "RR"};
            std::string wheels;
            for (std::size_t i = 0; i < s.wheels.size(); ++i) {
                const auto& wh = s.wheels[i];
                wheels += text("%s%s%+.2f/%.1fkN  ", i < 4 ? names[i] : "?",
                               wh.grounded ? " " : "^", static_cast<double>(wh.slipRatio),
                               static_cast<double>(wh.load) / 1000.0);
            }
            row("wheels slip/load", wheels);
        }
        row("driveline", text("fuel %.1f L (%.1f L/h)   coolant %.0f C   odo %.1f km   trip %.2f km",
                              static_cast<double>(s.fuelLiters), static_cast<double>(s.instantConsumptionLPerH),
                              static_cast<double>(s.coolantC), static_cast<double>(s.odometerKm), static_cast<double>(s.tripKm)));
        row("position", text("%.1f, %.1f, %.1f   collisions %d (worst %.0f km/h)", static_cast<double>(s.originPosition.X),
                             static_cast<double>(s.originPosition.Y), static_cast<double>(s.originPosition.Z), collisionCount_,
                             static_cast<double>(lastImpactSpeed_ * 3.6f)));
        if (walking_) {
            const float distance = std::hypot(walkingPosition_.X - s.originPosition.X, walkingPosition_.Z - s.originPosition.Z);
            row("on foot", text("%.1f, %.1f, %.1f   %.1f m from car   %s",
                                static_cast<double>(walkingPosition_.X), static_cast<double>(walkingPosition_.Y),
                                static_cast<double>(walkingPosition_.Z), static_cast<double>(distance),
                                walkingMoving_ ? "moving" : "stopped"));
        }

        section("environment");
        row("clock / weather", text("%s%s   %s   cloud %.2f  rain %.2f  wet %.2f", FormatClock(timeOfDayHours_).c_str(),
                                    timeScale_ <= 0.0f ? " (frozen)" : "", Core::Describe(weather_.kind),
                                    static_cast<double>(weather_.cloudCover), static_cast<double>(weather_.rain),
                                    static_cast<double>(weather_.wetness)));
        row("sun / lamps", text("elevation %.1f deg   lamp factor %.2f   headlamps %s",
                                static_cast<double>(rig_.sunElevationDeg), static_cast<double>(rig_.LampFactor()),
                                s.lowBeam ? (s.highBeam ? "high" : "low") : "off"));

        section("world");
        row("terrain / roads", text("%d/%d chunks   %d/%d road batches (%d culled)", world.terrainChunksDrawn,
                                    world.terrainChunksTotal, world.roadBatchesDrawn, world.roadBatchesTotal,
                                    world.roadBatchesTotal - world.roadBatchesDrawn));
        row("objects / trees", text("%d/%d object batches   %d/%d tree batches (%d culled)", world.objectBatchesDrawn,
                                    world.objectBatchesTotal, world.treeBatchesDrawn, world.treeBatchesTotal,
                                    (world.objectBatchesTotal - world.objectBatchesDrawn) +
                                        (world.treeBatchesTotal - world.treeBatchesDrawn)));
        row("traffic", text("%d alive, %d drawn (lod %d/%d/%d), %d parked drawn",
                            traffic_ ? static_cast<int>(traffic_->Vehicles().size()) : 0, traffic.drawn,
                            traffic.lod0, traffic.lod1, traffic.lod2, traffic.parkedDrawn));
        row("submitted", text("%d draw calls, %.2f M triangles",
                              world.drawCalls + traffic.drawCalls + vehicleDraws,
                              static_cast<double>(world.triangles + traffic.triangles + vehicleTris) / 1.0e6));

        // Layout: two columns of label/value, over a panel dark enough to read against snow-bright
        // sky and night alike.
        const float scale = h >= 700.0f ? 0.62f : 0.55f;
        const float rowHeight = 19.0f * scale / 0.62f;
        const float labelWidth = 165.0f * scale / 0.62f;
        const float panelWidth = std::min(w - 24.0f, 620.0f + labelWidth);
        const float panelHeight = rowHeight * static_cast<float>(rows.size()) + 20.0f;
        const float left = 12.0f;
        const float top = 12.0f;

        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &SamplerState::LinearClamp, &DepthStencilState::None,
                            &RasterizerState::CullNone);
        spriteBatch_->Draw(vehicleMaterials_->White(),
                           Rectangle(static_cast<int>(left), static_cast<int>(top), static_cast<int>(panelWidth),
                                     static_cast<int>(panelHeight)),
                           Color(0, 0, 0, 185));
        float y = top + 10.0f;
        for (const auto& [label, value] : rows) {
            if (value.empty()) {
                fontBold_->Draw(*spriteBatch_, label, Vector2(left + 12.0f, y), Color(150, 210, 255, 255), scale * 0.85f);
            } else {
                font_->Draw(*spriteBatch_, label, Vector2(left + 12.0f, y), Color(170, 175, 185, 245), scale);
                font_->Draw(*spriteBatch_, value, Vector2(left + 12.0f + labelWidth, y), Color(245, 245, 245, 250), scale);
            }
            y += rowHeight;
        }
        spriteBatch_->End();
    }

    void SimulatorGame::DrawHelp()
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const float w = static_cast<float>(vp.getWidthProperty());
        const float h = static_cast<float>(vp.getHeightProperty());
        const GameAction rows[] = {
            GameAction::Throttle, GameAction::Brake, GameAction::SteerLeft, GameAction::SteerRight, GameAction::Clutch,
            GameAction::ShiftUp, GameAction::ShiftDown, GameAction::GearNeutral, GameAction::GearReverse, GameAction::Gear1,
            GameAction::SelectorPark, GameAction::SelectorDrive, GameAction::ToggleTransmission, GameAction::ToggleDifferential, GameAction::ToggleAutoClutch,
            GameAction::ToggleEngine,
            GameAction::ToggleTurbo,
            GameAction::ToggleWalk, GameAction::ToggleRun,
            GameAction::Handbrake, GameAction::IndicatorLeft, GameAction::IndicatorRight, GameAction::Hazard,
            GameAction::Headlights, GameAction::HighBeam, GameAction::CycleWipers, GameAction::Horn, GameAction::ToggleCamera,
            GameAction::ToggleFullscreen, GameAction::ToggleMap, GameAction::ToggleExhaustSmoke, GameAction::ToggleFlight, GameAction::ToggleMirror, GameAction::ToggleHud, GameAction::ToggleHelp,
            GameAction::ToggleDebug, GameAction::Screenshot, GameAction::ResetVehicle,
            GameAction::ResetTrip, GameAction::VolumeUp, GameAction::VolumeDown,
            GameAction::TimeBackward, GameAction::TimeForward, GameAction::ToggleTimeFlow,
            GameAction::CycleWeather,
#ifndef __EMSCRIPTEN__
            GameAction::Quit,
#endif
        };
        const int count = static_cast<int>(sizeof(rows) / sizeof(rows[0]));
        const int perColumn = (count + 1) / 2;
        const float scale = h >= 700.0f ? 0.7f : 0.6f;
        const float rowHeight = 22.0f * scale / 0.7f;
        const float keyWidth = 190.0f * scale / 0.7f;
        const float columnWidth = 540.0f * scale / 0.7f;
        const float panelWidth = columnWidth * 2.0f + 40.0f;
        const float panelHeight = 70.0f + rowHeight * static_cast<float>(perColumn) + 16.0f;
        const float left = w * 0.5f - panelWidth * 0.5f;
        const float top = std::max(20.0f, h * 0.5f - panelHeight * 0.5f - 40.0f);

        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &SamplerState::LinearClamp, &DepthStencilState::None,
                            &RasterizerState::CullNone);
        const Rectangle box(static_cast<int>(left), static_cast<int>(top), static_cast<int>(panelWidth), static_cast<int>(panelHeight));
        spriteBatch_->Draw(vehicleMaterials_->White(), box, Color(0, 0, 0, 175));
        fontBold_->Draw(*spriteBatch_, "Controls", Vector2(left + 20.0f, top + 12.0f), Color(255, 255, 255, 255), 0.6f);
        font_->Draw(*spriteBatch_, "F1 closes this overlay. Bindings can be changed in the save file (see README).",
                    Vector2(left + 20.0f, top + panelHeight - 30.0f), Color(200, 200, 200, 220), 0.6f);
        for (int i = 0; i < count; ++i) {
            const GameAction a = rows[i];
            const int column = i / perColumn;
            const int row = i % perColumn;
            const float x = left + 20.0f + static_cast<float>(column) * columnWidth;
            const float y = top + 52.0f + static_cast<float>(row) * rowHeight;
            std::string keys = input_.KeysFor(a);
            std::string label = Input::Describe(a);
            if (input_.Pad().connected) {
                // With a controller connected, show its control where it has one.
                std::string pad = input_.PadButtonsFor(a);
                if (a == GameAction::Throttle) pad = input_.Pad().wheel ? "Accelerator" : "RT";
                if (a == GameAction::Brake) pad = input_.Pad().wheel ? "Brake pedal" : "LT";
                if (a == GameAction::SteerLeft || a == GameAction::SteerRight) pad = input_.Pad().wheel ? "Wheel" : "Left stick";
                if (!pad.empty()) keys = pad;
            }
            if (a == GameAction::Gear1) {
                keys = "1 - 6";
                label = "Select a gear (manual)";
            }
            // The key column fits about 22 characters; shorten the modifier names rather than
            // cutting them off ("Left Shift / Right Sh...").
            for (const auto& pair : {std::pair<const char*, const char*>{"Left Shift", "L Shift"},
                                     {"Right Shift", "R Shift"}, {"Left Ctrl", "L Ctrl"}, {"Right Ctrl", "R Ctrl"}}) {
                for (std::size_t at = keys.find(pair.first); at != std::string::npos; at = keys.find(pair.first, at)) {
                    keys.replace(at, std::string(pair.first).size(), pair.second);
                }
            }
            if (keys.size() > 22) {
                keys = keys.substr(0, 21) + "…";
            }
            font_->Draw(*spriteBatch_, keys, Vector2(x, y), Color(255, 220, 120, 255), scale);
            font_->Draw(*spriteBatch_, label, Vector2(x + keyWidth, y), Color(240, 240, 240, 255), scale);
        }
        spriteBatch_->End();
    }

}
