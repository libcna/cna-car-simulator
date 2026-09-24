#include "CarSim/App/SimulatorGame.hpp"
#include "CarSim/App/WalkingMode.hpp"

#include "CarSim/Map/MapDocument.hpp"

#include "CarSim/Core/Version.hpp"
#include "CarSim/Render/Image.hpp"
#include "CarSim/Render/Screenshot.hpp"
#include "CarSim/Sim/Units.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include "System/TimeSpan.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <sstream>

namespace CarSim::App
{
    namespace
    {
        /// "13:42" from decimal hours; used by the HUD and the start-up log.
        std::string FormatClock(const float hours)
        {
            const float wrapped = hours - 24.0f * std::floor(hours / 24.0f);
            int minutes = static_cast<int>(wrapped * 60.0f + 0.5f);
            minutes %= 24 * 60;
            char buffer[8];
            std::snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes / 60, minutes % 60);
            return buffer;
        }
    }

    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    using Input::GameAction;

    namespace
    {
        std::unique_ptr<Texture2D> BuildMapTexture(GraphicsDevice& device, const Map::MapWorld& world)
        {
            constexpr int size = 768;
            const auto& terrain = world.Terrain();
            const float minX = terrain.MinX(), minZ = terrain.MinZ();
            const float sizeX = terrain.MaxX() - minX, sizeZ = terrain.MaxZ() - minZ;
            Render::Image image(size, size);
            image.Generate([&](int, int, const float u, const float v) {
                const auto region = terrain.RegionAt(minX + u * sizeX, minZ + v * sizeZ);
                switch (region) {
                    case Map::RegionType::Forest: return Color(32, 54, 49, 255);
                    case Map::RegionType::Field: return Color(69, 72, 49, 255);
                    case Map::RegionType::Town: return Color(65, 69, 74, 255);
                    case Map::RegionType::Square: return Color(88, 86, 77, 255);
                    case Map::RegionType::Yard: return Color(71, 75, 75, 255);
                    case Map::RegionType::Orchard: return Color(48, 68, 48, 255);
                    case Map::RegionType::Meadow: return Color(51, 70, 52, 255);
                }
                return Color(51, 70, 52, 255);
            });
            const auto pixel = [&](const Vector3& p) {
                return Vector2((p.X - minX) / sizeX * size, (p.Z - minZ) / sizeZ * size);
            };
            for (const auto& road : world.Roads().Roads()) {
                const auto& samples = road.curve.Samples();
                const bool track = road.spec && road.spec->roadClass == Map::RoadClass::Track;
                const bool main = road.spec && (road.spec->roadClass == Map::RoadClass::ClassI ||
                                                road.spec->roadClass == Map::RoadClass::ClassII);
                const Color surface = track ? Color(171, 145, 98, 255) :
                                      main ? Color(232, 218, 173, 255) : Color(202, 211, 199, 255);
                const float width = track ? 2.0f : main ? 5.0f : 3.3f;
                for (std::size_t i = 1; i < samples.size(); ++i) {
                    const Vector2 a = pixel(samples[i - 1].position);
                    const Vector2 b = pixel(samples[i].position);
                    image.DrawLine(a.X, a.Y, b.X, b.Y, width + 2.0f, Color(25, 35, 36, 255));
                    image.DrawLine(a.X, a.Y, b.X, b.Y, width, surface);
                }
            }
            return Render::UploadTexture(device, image, true);
        }
    }

    SimulatorGame::SimulatorGame(Core::CommandLineOptions options)
        : options_(std::move(options)),
          graphics_(this)
    {
        // HiDef: the simulator needs render targets, 32-bit indices, back-buffer reads and instancing.
        graphics_.setGraphicsProfileProperty(GraphicsProfile::HiDef);
        graphics_.setPreferredBackBufferWidthProperty(options_.width);
        graphics_.setPreferredBackBufferHeightProperty(options_.height);
        graphics_.setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
        graphics_.setIsFullScreenProperty(options_.fullscreen);
        graphics_.setSynchronizeWithVerticalRetraceProperty(true);
#ifdef __EMSCRIPTEN__
        // GraphicsDeviceManager requests 8x whenever PreferMultiSampling is true, with no
        // per-platform cap. The web build skips MSAA rather than pay 8x resolve and fill through
        // the browser's WebGL layer on whatever GPU the visitor has.
        graphics_.setPreferMultiSamplingProperty(false);
#else
        graphics_.setPreferMultiSamplingProperty(true);
#endif

        setIsFixedTimeStepProperty(true);
        setTargetElapsedTimeProperty(System::TimeSpan::FromSeconds(1.0 / 60.0));
        setIsMouseVisibleProperty(true);

        getWindowProperty().setTitleProperty(Core::ProductName() + " " + Core::VersionString());
        ResolveContentRoot();
    }

    SimulatorGame::~SimulatorGame() = default;

    void SimulatorGame::ResolveContentRoot()
    {
        namespace fs = std::filesystem;
        std::vector<fs::path> candidates;
        if (!options_.contentDirectory.empty()) {
            candidates.emplace_back(options_.contentDirectory);
        }
        candidates.emplace_back("content");
        std::error_code ec;
        const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
        if (!ec) {
            candidates.push_back(exe.parent_path() / "content");
        }
#ifdef CARSIM_SOURCE_CONTENT_DIR
        candidates.emplace_back(CARSIM_SOURCE_CONTENT_DIR);
#endif
        for (const auto& c : candidates) {
            if (fs::exists(c / "vehicles", ec)) {
                contentRoot_ = fs::absolute(c, ec).string();
                break;
            }
        }
        if (contentRoot_.empty()) {
            contentRoot_ = "content";
        }
        getContentProperty().setRootDirectoryProperty(contentRoot_);
        std::cout << "content root: " << contentRoot_ << "\n";
    }

    void SimulatorGame::LoadSave()
    {
        saveEnabled_ = !options_.noSave;
        if (!saveEnabled_) {
            std::cout << "save: disabled\n";
            return;
        }
        savePath_ = options_.savePath.empty() ? Core::DefaultSavePath() : options_.savePath;
        const Core::SaveLoadResult result = Core::LoadSaveData(savePath_);
        for (const auto& w : result.warnings) {
            std::cerr << "save: " << w << "\n";
        }
        saveReadOnly_ = result.readOnly;
        if (result.loaded) {
            save_ = result.data;
            std::cout << "save: loaded " << savePath_ << " (odometer " << save_.odometerKm << " km)\n";
        } else {
            std::cout << "save: new profile at " << savePath_ << "\n";
        }
        hudVisible_ = save_.settings.hudVisible;
        mirrorEnabled_ = save_.settings.mirrorEnabled;
        exhaustSmokeEnabled_ = save_.settings.exhaustSmokeEnabled;
        if (!options_.cockpit && save_.settings.startInCockpit) {
            options_.cockpit = true;
        }
        // M now opens the map. Restore F for Drive and J for flight in profiles saved with
        // the previous F-flight/G-Drive layout.
        for (auto& binding : save_.bindings) {
            if (binding.first == "ToggleMirror" && binding.second == "M") {
                binding.second = "V";
            } else if (binding.first == "SelectorDrive" && binding.second == "G") {
                binding.second = "F";
            } else if (binding.first == "ToggleFlight" && binding.second == "F") {
                binding.second = "J";
            } else if (binding.first == "ToggleWalk" && binding.second == "W") {
                // W is the accelerator. Old profiles made the first press of the throttle
                // exit a parked car and then suppressed all of its driving controls.
                binding.second = "G";
            } else if (binding.first == "CycleWeather" && binding.second == "F9") {
                // F9 also fires the framework's simulated context loss, which broke the
                // textures every time the weather was changed.
                binding.second = "F4";
            }
        }
        std::vector<std::string> bindingWarnings;
        input_.ApplyOverrides(save_.bindings, bindingWarnings);
        for (const auto& w : bindingWarnings) {
            std::cerr << "save: " << w << "\n";
        }
    }

    void SimulatorGame::ApplySaveToVehicle()
    {
        if (!vehicle_ || !saveEnabled_) {
            return;
        }
        vehicle_->GetOdometer().SetTotalKm(save_.odometerKm);
        vehicle_->GetOdometer().SetTripKm(save_.tripKm);
        if (save_.transmissionMode == "automatic") {
            vehicle_->SetTransmissionMode(Sim::TransmissionMode::Automatic);
        } else if (save_.transmissionMode == "manual") {
            vehicle_->SetTransmissionMode(Sim::TransmissionMode::Manual);
        }
        vehicle_->SetAutoClutch(save_.settings.autoClutch);
        if (save_.settings.differential == "lsd") vehicle_->SetLimitedSlip(true);
        if (save_.settings.differential == "open") vehicle_->SetLimitedSlip(false);
        for (const Sim::WiperMode mode : {Sim::WiperMode::Off, Sim::WiperMode::Intermittent, Sim::WiperMode::Slow, Sim::WiperMode::Fast}) {
            if (save_.settings.wipers == Sim::ToString(mode)) vehicle_->GetElectrics().SetWipers(mode);
        }
    }

    void SimulatorGame::WriteSave()
    {
        if (!saveEnabled_ || saveReadOnly_ || !vehicle_) {
            return;
        }
        save_.odometerKm = vehicle_->GetOdometer().TotalKm();
        save_.tripKm = vehicle_->GetOdometer().TripKm();
        save_.transmissionMode = vehicle_->GetTransmission().Mode() == Sim::TransmissionMode::Automatic ? "automatic" : "manual";
        save_.vehicleId = definition_.id;
        if (map_) {
            save_.mapId = map_->Data().info.id;
        }
        if (audio_) {
            save_.settings.masterVolume = audio_->levels.master;
            save_.settings.engineVolume = audio_->levels.engine;
            save_.settings.effectsVolume = audio_->levels.effects;
        }
        save_.settings.hudVisible = hudVisible_;
        save_.settings.mirrorEnabled = mirrorEnabled_;
        save_.settings.exhaustSmokeEnabled = exhaustSmokeEnabled_;
        save_.settings.startInCockpit = cameraMode_ == Render::CameraMode::Cockpit;
        save_.settings.autoClutch = vehicle_->AutoClutch();
        save_.settings.differential = vehicle_->LimitedSlip() ? "lsd" : "open";
        save_.settings.wipers = Sim::ToString(vehicle_->GetElectrics().Wipers());
        save_.settings.timeOfDayHours = timeOfDayHours_;
        save_.settings.timeScale = timeScale_;
        save_.settings.weather = Core::ToString(weather_.kind);
        save_.bindings = input_.NamedBindings();
        std::string error;
        if (!Core::WriteSaveData(savePath_, save_, error)) {
            std::cerr << "save: " << error << "\n";
        }
    }

    void SimulatorGame::OnExiting(System::Object* sender, const System::EventArgs& args)
    {
        WriteSave();
        Game::OnExiting(sender, args);
    }

    void SimulatorGame::LoadMap()
    {
        const std::string name = options_.map.value_or(save_.mapId.empty() ? std::string("lipova") : save_.mapId);
        if (name == "none") {
            std::cout << "map: none (flat proving ground)\n";
            return;
        }
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
        const auto start = std::chrono::steady_clock::now();
        map_ = Map::MapWorld::Load(Map::MapDirectory(contentRoot_, name), errors, &warnings);
        for (const auto& w : warnings) {
            std::cerr << "map: warning: " << w << "\n";
        }
        if (!map_) {
            for (const auto& e : errors) {
                std::cerr << "map: " << e << "\n";
            }
            std::cerr << "map: '" << name << "' failed to load; using the flat proving ground\n";
            return;
        }
        collision_.Build(*map_);
        traffic_ = std::make_unique<Traffic::TrafficSystem>(*map_, 7u);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << "collision: " << collision_.StaticCount() << " static colliders\n";
        std::cout << "map: " << map_->Data().info.displayName << " (" << name << "), " << map_->Roads().Roads().size() << " roads, "
                  << map_->Lanes().Lanes().size() << " lanes, built in " << seconds << " s\n";
    }

    void SimulatorGame::LoadVehicle()
    {
        const std::string id = options_.vehicle.value_or(save_.vehicleId.empty() ? std::string("lipan_12") : save_.vehicleId);
        const auto loaded = Sim::LoadVehicleDefinitionFile(contentRoot_ + "/vehicles/" + id + ".json");
        if (loaded.ok()) {
            definition_ = loaded.definition;
        } else {
            for (const auto& e : loaded.errors) {
                std::cerr << "vehicle: " << e << "\n";
            }
            std::cerr << "vehicle: falling back to the built-in reference vehicle\n";
            definition_ = Sim::MakeReferenceVehicle();
        }
        vehicle_ = std::make_unique<Sim::Vehicle>(definition_, definition_.gearbox.defaultMode);
        if (map_) {
            // A route names the spawn it starts from; an explicit --spawn still wins, so a route
            // can be driven from somewhere else on purpose.
            std::string spawnName = options_.spawn.value_or(std::string());
            if (spawnName.empty() && options_.route) {
                for (const auto& r : map_->Data().traffic.routes) {
                    if (r.name == *options_.route) {
                        spawnName = r.spawn;
                        break;
                    }
                }
            }
            const Map::SpawnSpec spawn = map_->PlayerSpawn(spawnName);
            // PlaceAt takes the rotation about +Y (counter-clockwise from above); map headings are clockwise from north.
            vehicle_->PlaceAt(map_->SpawnPosition(spawn), -spawn.headingDeg * (std::numbers::pi_v<float> / 180.0f));
            std::cout << "spawn: " << spawn.name << " at (" << spawn.position.X << ", " << spawn.position.Y << "), heading " << spawn.headingDeg << " deg\n";
        } else {
            vehicle_->PlaceAt(Vector3(0.0f, 0.0f, 0.0f), 0.0f);
        }
        std::cout << "vehicle: " << definition_.displayName << " (" << definition_.id << ")\n";
    }

    void SimulatorGame::Initialize()
    {
        Game::Initialize();
        cameraMode_ = options_.cockpit ? Render::CameraMode::Cockpit : Render::CameraMode::Chase;
        showHelp_ = options_.showHelpOverlay;
        showDebug_ = options_.showDebugOverlay;
        showMap_ = options_.showMapOverlay;
        if (options_.chaseYawDeg) {
            chaseCamera_.yawOffset = *options_.chaseYawDeg * (std::numbers::pi_v<float> / 180.0f);
        }
        if (options_.chaseDistanceM) {
            chaseCamera_.distance = *options_.chaseDistanceM;
        }
        if (options_.eyeOffset) {
            cockpitCamera_.eyeOffset = Vector3(options_.eyeOffset->x, options_.eyeOffset->y, options_.eyeOffset->z);
            cockpitCamera_.yawOffsetDeg = options_.eyeOffset->headingDeg;
            cockpitCamera_.pitchOffsetDeg = options_.eyeOffset->pitchDeg;
        }
    }

    bool SimulatorGame::PlanRoute()
    {
        if (!options_.route || !map_) {
            return false;
        }
        const Map::RouteSpec* spec = nullptr;
        for (const auto& r : map_->Data().traffic.routes) {
            if (r.name == *options_.route) {
                spec = &r;
                break;
            }
        }
        if (spec == nullptr) {
            std::cerr << "route: '" << *options_.route << "' is not defined in this map; known routes:";
            for (const auto& r : map_->Data().traffic.routes) std::cerr << " " << r.name;
            std::cerr << "\n";
            return false;
        }
        routeName_ = spec->name;
        routeDriver_ = std::make_unique<Traffic::RouteDriver>(map_->Lanes());
        const auto state = vehicle_->Snapshot();
        // The vehicle's yaw is counter-clockwise about +Y; the lane graph wants a compass heading.
        const Vector3 forward = state.worldMatrix.getForwardProperty();
        const float heading = std::atan2(forward.X, -forward.Z);
        if (!routeDriver_->Plan(state.originPosition, heading, spec->waypoints)) {
            std::cerr << "route: cannot drive '" << routeName_ << "': " << routeDriver_->Progress().note << "\n";
            routeDriver_.reset();
            return false;
        }
        const auto& progress = routeDriver_->Progress();
        std::cout << "route: " << routeName_ << " -- " << spec->description << "\n"
                  << "route: " << progress.steps << " steps, " << progress.routeLengthM << " m\n";
        routeStartSeconds_ = elapsedSeconds_;
        return true;
    }

    void SimulatorGame::ReportRoute() const
    {
        if (!routeDriver_) {
            return;
        }
        const auto& progress = routeDriver_->Progress();
        std::cout << "route: " << routeName_ << (progress.finished ? " finished" : " stopped short") << " after "
                  << progress.distanceM << " of " << progress.routeLengthM << " m in "
                  << (elapsedSeconds_ - routeStartSeconds_) << " s, worst lateral error "
                  << progress.offRouteM << " m\n";
    }

    void SimulatorGame::ApplyAutoDrive(Sim::DriverControls& controls)
    {
        if (options_.lights && !lightsApplied_) {
            // The electrical system gates every lamp except the hazards on the ignition, so a
            // capture that asks for lights starts the engine first when the scripted drive does
            // not (otherwise --lights alone silently produced a dark car).
            const Sim::VehicleState state = vehicle_->Snapshot();
            if (state.ignitionOn) {
                if (elapsedSeconds_ > 0.3) {
                    lightsApplied_ = true;
                    controls.toggleHeadlights = true;
                }
            } else if (!options_.autoDriveSeconds && !lightsEngineRequested_ && elapsedSeconds_ > 0.1) {
                lightsEngineRequested_ = true;
                controls.toggleEngine = true;
            }
        }
        if (options_.wiperSteps > 0 && wiperStepsApplied_ < options_.wiperSteps) {
            // Like --lights: the wipers need the ignition, so a capture that asks for them
            // starts the engine first when nothing else does.
            const Sim::VehicleState state = vehicle_->Snapshot();
            if (state.ignitionOn && elapsedSeconds_ > 0.3) {
                ++wiperStepsApplied_;
                controls.cycleWipers = true;
            } else if (!state.ignitionOn && !options_.autoDriveSeconds && !lightsEngineRequested_ && elapsedSeconds_ > 0.1) {
                lightsEngineRequested_ = true;
                controls.toggleEngine = true;
            }
        }
        if (!options_.autoDriveSeconds) {
            return;
        }
        // Scripted drive for headless captures: automatic gearbox, engine start, gentle
        // acceleration with a slow steering weave so wheels, needles and lamps are exercised.
        const float t = static_cast<float>(elapsedSeconds_);
        if (t > *options_.autoDriveSeconds) {
            return;
        }
        if (!autoDriveStarted_) {
            autoDriveStarted_ = true;
            vehicle_->SetTransmissionMode(Sim::TransmissionMode::Automatic);
            controls.toggleEngine = true;
        }
        if (t > 1.2f) {
            controls.selector = Sim::AutomaticSelector::Drive;
        }
        controls.throttle = t > 1.6f ? 0.55f : 0.0f;
        controls.brake = t <= 1.6f ? 1.0f : 0.0f;
        controls.steering = (!map_ && t > 3.0f) ? 0.18f * std::sin((t - 3.0f) * 0.9f) : 0.0f;
        if (t > 2.0f && t < 2.05f) {
            controls.indicator = Sim::IndicatorRequest::ToggleRight;
        }
    }

    void SimulatorGame::LoadContent()
    {
        auto& device = getGraphicsDeviceProperty();
        LoadSave();
        ApplyClockSettings();
        ApplyWeatherSettings();
        LoadMap();
        LoadVehicle();
        ApplySaveToVehicle();
        if (options_.startFlight) {
            Sim::DriverControls controls;
            controls.toggleFlight = true;
            const Sim::GroundSurface& ground = map_ ? static_cast<const Sim::GroundSurface&>(map_->Ground()) : ground_;
            vehicle_->Update(controls, 1.0f / 60.0f, ground);
            cameraMode_ = Render::CameraMode::Chase;
        }
        if (options_.route && !PlanRoute()) {
            // A run that was told to drive a route and cannot is a failed run, not a free drive.
            std::cerr << "route: refusing to continue without the requested route\n";
            exitRequested_ = true;
            Exit();
            return;
        }

        sky_ = std::make_unique<Render::SkyRenderer>(device, rig_);
        rain_ = std::make_unique<Render::RainRenderer>(device);
        font_ = Render::BitmapFont::Load(getContentProperty(), contentRoot_, "fonts/ui_regular_28");
        fontBold_ = Render::BitmapFont::Load(getContentProperty(), contentRoot_, "fonts/ui_bold_44");
        if (!font_) {
            std::cerr << "font: using the built-in fallback font\n";
            font_ = Render::BitmapFont::CreateBuiltin(device);
        }
        if (!fontBold_) {
            fontBold_ = Render::BitmapFont::CreateBuiltin(device);
        }
        if (map_) {
            const auto start = std::chrono::steady_clock::now();
            worldRenderer_ = std::make_unique<Render::WorldRenderer>(device, rig_, *map_, fontBold_.get());
            signalRenderer_ = std::make_unique<Render::SignalRenderer>(device, *map_);
            std::cout << "world: " << worldRenderer_->Stats().terrainChunksTotal << " terrain chunks, "
                      << worldRenderer_->Stats().roadBatchesTotal << " road batches, built in "
                      << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << " s\n";
        } else {
            testGround_ = std::make_unique<Render::TestGround>(device, rig_);
        }
        vehicleMaterials_ = std::make_unique<Render::VehicleMaterials>(device, rig_);
        vehicleRenderer_ = std::make_unique<Render::VehicleRenderer>(device, *vehicleMaterials_, definition_);
        exhaustSmoke_ = std::make_unique<Render::ExhaustSmokeRenderer>(device, vehicleRenderer_->Model().style);
        exhaustSmoke_->Smoke().SetEnabled(exhaustSmokeEnabled_);
        wheelSpray_ = std::make_unique<Render::WheelSprayRenderer>(device);
        windscreenRain_ = std::make_unique<Render::WindscreenRainRenderer>(device);
        wetReflections_ = std::make_unique<Render::WetReflections>(device);
        plateFont_ = Render::BitmapFont::Load(getContentProperty(), contentRoot_, "fonts/plate_bold_128");
        trafficRenderer_ = std::make_unique<Render::TrafficRenderer>(device, *vehicleMaterials_, plateFont_.get());
        {
            std::string plate = definition_.visual.plate;
            if (plate.empty()) {
                plate = Traffic::PlateGenerator(1u).Next();
            }
            playerPlate_ = trafficRenderer_->PlateTexture(device, plate);
        }
        if (map_) {
            // One plate per parked car, deterministic for a map so captures stay comparable.
            Traffic::PlateGenerator plates(7331u);
            parkedPlates_.clear();
            parkedPlates_.reserve(map_->Objects().Vehicles().size());
            for (std::size_t i = 0; i < map_->Objects().Vehicles().size(); ++i) {
                parkedPlates_.push_back(plates.Next());
            }
        }
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        if (map_) {
            mapTexture_ = BuildMapTexture(device, *map_);
        }

        gaugeFont_ = Render::BitmapFont::Load(getContentProperty(), contentRoot_, "fonts/gauge_condensed_96");
        if (!gaugeFont_) {
            gaugeFont_ = Render::BitmapFont::CreateBuiltin(device);
        }
        cluster_ = std::make_unique<Render::InstrumentCluster>(device, definition_, *gaugeFont_, *font_, *fontBold_);
        mirror_ = std::make_unique<Render::MirrorView>(device);
        for (auto& wing : wingMirrors_) wing = std::make_unique<Render::MirrorView>(device, 256, 160);
        chaseCamera_.groundHeight = [this](const float x, const float z) { return map_ ? map_->Ground().HeightAt(x, z) : 0.0f; };
        if (vehicle_->FlightMode()) {
            chaseCamera_.distance = options_.chaseDistanceM.value_or(14.0f);
            chaseCamera_.height = 5.0f;
            chaseCamera_.targetHeight = 0.6f;
        }
        chaseCamera_.Snap(vehicle_->Snapshot());
        // The clock and the weather were read before the renderers existed, so hand the finished
        // palette to them now that they do.
        if (vehicle_) {
            vehicle_->SetRoadWetness(weather_.wetness);
        }
        if (worldRenderer_) {
            worldRenderer_->SetWetness(weather_.wetness);
        }
        RefreshLighting(true, true);
        ApplyQualitySettings();
        if (options_.mirrorEvery) {
            save_.settings.mirrorUpdateEvery = *options_.mirrorEvery;   // an explicit rate wins over the tier
        }
        if (traffic_ && options_.trafficWarmupSeconds > 0.0f) {
            const int steps = static_cast<int>(options_.trafficWarmupSeconds * 60.0f);
            for (int i = 0; i < steps; ++i) {
                traffic_->Update(1.0f / 60.0f, PlayerProbe());
            }
            std::cout << "traffic: warmed up " << options_.trafficWarmupSeconds << " s, " << traffic_->Vehicles().size() << " cars\n";
        }
        audio_ = std::make_unique<Audio::VehicleAudio>(!options_.noAudio);
        audio_->levels.master = save_.settings.masterVolume;
        audio_->levels.engine = save_.settings.engineVolume;
        audio_->levels.effects = save_.settings.effectsVolume;
        std::cout << "audio: " << (audio_->Enabled() ? "stereo stream at 44.1 kHz" : "disabled") << "\n";
    }

    void SimulatorGame::UnloadContent()
    {
        audio_.reset();
        trafficRenderer_.reset();
        plateFont_.reset();
        playerPlate_ = nullptr;
        cluster_.reset();
        mirror_.reset();
        for (auto& wing : wingMirrors_) wing.reset();
        gaugeFont_.reset();
        vehicleRenderer_.reset();
        vehicleMaterials_.reset();
        worldRenderer_.reset();
        testGround_.reset();
        sky_.reset();
        font_.reset();
        fontBold_.reset();
        spriteBatch_.reset();
    }

    void SimulatorGame::HandleAppActions()
    {
#ifndef __EMSCRIPTEN__
        // A browser page has nothing to quit to: there Escape leaves fullscreen or pointer lock,
        // and ending the game loop would only freeze the canvas.
        if (input_.Pressed(GameAction::Quit)) {
            exitRequested_ = true;
            Exit();
        }
#endif
        if (input_.Pressed(GameAction::ToggleCamera)) {
            cameraMode_ = cameraMode_ == Render::CameraMode::Chase ? Render::CameraMode::Cockpit : Render::CameraMode::Chase;
        }
        if (input_.Pressed(GameAction::ToggleFullscreen)) {
            graphics_.ToggleFullScreen();
        }
        if (input_.Pressed(GameAction::ToggleHelp)) {
            showHelp_ = !showHelp_;
        }
        if (input_.Pressed(GameAction::ToggleDebug)) {
            showDebug_ = !showDebug_;
        }
        if (input_.Pressed(GameAction::Screenshot)) {
            screenshotRequested_ = true;
        }
        if (input_.Pressed(GameAction::ToggleHud)) {
            hudVisible_ = !hudVisible_;
        }
        if (input_.Pressed(GameAction::ToggleMirror)) {
            mirrorEnabled_ = !mirrorEnabled_;
        }
        if (input_.Pressed(GameAction::ToggleMap)) {
            showMap_ = !showMap_;
        }
        if (input_.Pressed(GameAction::ToggleExhaustSmoke)) {
            exhaustSmokeEnabled_ = !exhaustSmokeEnabled_;
            if (exhaustSmoke_) exhaustSmoke_->Smoke().SetEnabled(exhaustSmokeEnabled_);
            std::cout << "exhaust smoke: " << (exhaustSmokeEnabled_ ? "on" : "off") << "\n";
        }
        if (input_.Pressed(GameAction::TimeForward) || input_.Pressed(GameAction::TimeBackward)) {
            const float step = input_.Pressed(GameAction::TimeForward) ? 1.0f : -1.0f;
            timeOfDayHours_ = std::fmod(timeOfDayHours_ + step + 24.0f, 24.0f);
            rig_.SetTimeOfDay(timeOfDayHours_);
            RefreshLighting(true, true);
            std::cout << "clock: " << FormatClock(timeOfDayHours_) << "\n";
        }
        if (input_.Pressed(GameAction::CycleWeather)) {
            weather_.Set(weather_.Next());
            std::cout << "weather: " << Core::Describe(weather_.kind) << " moving in\n";
        }
        if (input_.Pressed(GameAction::ToggleTimeFlow)) {
            if (timeScale_ > 0.0f) {
                frozenTimeScale_ = timeScale_;
                timeScale_ = 0.0f;
            } else {
                timeScale_ = frozenTimeScale_ > 0.0f ? frozenTimeScale_ : 60.0f;
            }
            std::cout << "clock: " << (timeScale_ > 0.0f ? "running" : "frozen") << "\n";
        }
        if (audio_ && (input_.Pressed(GameAction::VolumeUp) || input_.Pressed(GameAction::VolumeDown))) {
            const float step = input_.Pressed(GameAction::VolumeUp) ? 0.1f : -0.1f;
            audio_->levels.master = std::clamp(audio_->levels.master + step, 0.0f, 1.0f);
            std::cout << "audio: master volume " << static_cast<int>(audio_->levels.master * 100.0f + 0.5f) << " %\n";
        }
        if (input_.Pressed(GameAction::ResetVehicle)) {
            const auto s = vehicle_->Snapshot();
            const Vector3 forward = s.worldMatrix.getForwardProperty();
            const float y = map_ ? map_->Ground().HeightAt(s.originPosition.X, s.originPosition.Z) : 0.0f;
            vehicle_->PlaceAt(Vector3(s.originPosition.X, y, s.originPosition.Z), std::atan2(-forward.X, -forward.Z));
        }
    }

    Traffic::PlayerProbe SimulatorGame::PlayerProbe() const
    {
        Traffic::PlayerProbe probe;
        if (!vehicle_) {
            return probe;
        }
        probe.valid = true;
        probe.position = vehicle_->OriginPosition();
        probe.forward = vehicle_->Body().Forward();
        probe.speed = vehicle_->ForwardSpeedMs();
        probe.lengthM = definition_.chassis.lengthM;
        probe.blocksTraffic = !vehicle_->FlightMode();
        return probe;
    }

    Traffic::PlayerProbe SimulatorGame::PedestrianProbe() const
    {
        Traffic::PlayerProbe probe;
        if (!walking_) return probe;
        probe.valid = true;
        probe.position = walkingPosition_;
        probe.forward = Vector3(std::sin(walkingYaw_), 0.0f, -std::cos(walkingYaw_));
        probe.lengthM = 0.5f;
        return probe;
    }

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

    void SimulatorGame::ApplyQualitySettings()
    {
        // The save file holds the tier and the command line overrides it; an unreadable name says
        // so and falls back rather than failing the run.
        quality_ = Render::QualityTier::High;
        if (!save_.settings.graphicsQuality.empty() && !Render::QualityFromName(save_.settings.graphicsQuality, quality_)) {
            std::cerr << "save: unknown graphicsQuality '" << save_.settings.graphicsQuality << "', using "
                      << Render::ToString(quality_) << "\n";
        }
        if (options_.quality && !Render::QualityFromName(*options_.quality, quality_)) {
            std::cerr << "--quality: unknown tier '" << *options_.quality << "', using "
                      << Render::ToString(quality_) << "\n";
        }
        save_.settings.graphicsQuality = Render::ToString(quality_);
        qualitySettings_ = Render::SettingsFor(quality_);
        save_.settings.mirrorUpdateEvery = qualitySettings_.mirrorUpdateEvery;
        if (worldRenderer_) {
            worldRenderer_->SetDrawDistanceScale(qualitySettings_.drawDistanceScale);
            worldRenderer_->SetVegetationScale(qualitySettings_.vegetationScale);
        }
    }

    void SimulatorGame::DrawWetReflections(GraphicsDevice& device, const Sim::VehicleState& state, const Matrix& view,
                                           const Matrix& projection, const Vector3& camera)
    {
        const float lamps = rig_.LampFactor();
        if (!wetReflections_ || weather_.wetness < 0.1f || lamps < 0.05f) return;
        reflectedLights_.clear();
        const float range2 = Render::WetReflections::kRangeM * Render::WetReflections::kRangeM;
        // Street lanterns: warm sodium-ish white, the brightest and longest streaks.
        if (worldRenderer_) {
            for (const auto& lantern : worldRenderer_->Lanterns()) {
                if (Vector3::DistanceSquared(lantern, camera) > range2) continue;
                reflectedLights_.push_back({lantern, Vector3(1.0f, 0.86f, 0.62f), lamps});
            }
        }
        // Traffic: headlamps white in front, tail lamps red behind (brighter when braking).
        if (traffic_) {
            for (const auto& car : traffic_->Vehicles()) {
                if (Vector3::DistanceSquared(car.position, camera) > range2) continue;
                const Vector3 right(-car.forward.Z, 0.0f, car.forward.X);
                for (const float side : {-1.0f, 1.0f}) {
                    const Vector3 lateral = right * (side * (0.5f * car.widthM - 0.3f));
                    reflectedLights_.push_back({car.position + car.forward * (0.5f * car.lengthM) + lateral + Vector3(0.0f, 0.65f, 0.0f),
                                                Vector3(1.0f, 0.95f, 0.85f), 0.8f * lamps});
                    reflectedLights_.push_back({car.position - car.forward * (0.5f * car.lengthM) + lateral + Vector3(0.0f, 0.8f, 0.0f),
                                                Vector3(1.0f, 0.08f, 0.04f), (car.brakeLights ? 0.8f : 0.35f) * lamps});
                }
            }
        }
        // The player's own lamps, while they are on.
        if (state.lowBeam && !state.flightMode) {
            const Vector3 forward = Vector3::TransformNormal(Vector3(0.0f, 0.0f, -1.0f), state.worldMatrix);
            const Vector3 right = Vector3::TransformNormal(Vector3(1.0f, 0.0f, 0.0f), state.worldMatrix);
            for (const float side : {-1.0f, 1.0f}) {
                reflectedLights_.push_back({state.originPosition + forward * 2.0f + right * (side * 0.6f) + Vector3(0.0f, 0.65f, 0.0f),
                                            Vector3(1.0f, 0.95f, 0.85f), state.highBeam ? 1.0f : 0.8f});
                reflectedLights_.push_back({state.originPosition - forward * 2.0f + right * (side * 0.6f) + Vector3(0.0f, 0.8f, 0.0f),
                                            Vector3(1.0f, 0.08f, 0.04f), state.brakeLights ? 0.8f : 0.35f});
            }
        }
        // Nearest first, so the cap drops the far ones.
        std::sort(reflectedLights_.begin(), reflectedLights_.end(), [&](const Render::ReflectedLight& a, const Render::ReflectedLight& b) {
            return Vector3::DistanceSquared(a.position, camera) < Vector3::DistanceSquared(b.position, camera);
        });
        const auto height = [this](const float x, const float z) { return map_ ? map_->Ground().HeightAt(x, z) : 0.0f; };
        wetReflections_->Draw(device, view, projection, camera, reflectedLights_, std::clamp((weather_.wetness - 0.1f) / 0.6f, 0.0f, 1.0f), height);
    }

    void SimulatorGame::UpdateSpray(const Sim::VehicleState& state, const float dt)
    {
        if (!wheelSpray_) return;
        sprayEmitters_.clear();
        if (weather_.wetness > 0.15f) {
            const auto surfaceShare = [](const Sim::SurfaceType s) {
                switch (s) {
                    case Sim::SurfaceType::Grass: return 0.0f;
                    case Sim::SurfaceType::Gravel:
                    case Sim::SurfaceType::Dirt: return 0.4f;
                    case Sim::SurfaceType::Cobbles: return 0.7f;
                    default: return 1.0f;
                }
            };
            if (!state.flightMode && !walking_) {
                for (std::size_t i = 0; i < state.wheels.size() && i < definition_.wheels.size(); ++i) {
                    const auto& w = state.wheels[i];
                    if (!w.grounded) continue;
                    Render::SprayEmitter e;
                    e.contact = w.worldCenter - Vector3(0.0f, definition_.wheels[i].radiusM, 0.0f);
                    e.velocity = state.velocity;
                    e.share = surfaceShare(w.surface);
                    sprayEmitters_.push_back(e);
                }
            }
            if (traffic_) {
                // Only cars the camera could see the spray of; the rear wheels throw the plume.
                const Vector3 eye = cameraMode_ == Render::CameraMode::Cockpit ? state.originPosition : chaseCamera_.Pose().position;
                for (const auto& car : traffic_->Vehicles()) {
                    if (Vector3::DistanceSquared(car.position, eye) > 90.0f * 90.0f) continue;
                    const Vector3 right(-car.forward.Z, 0.0f, car.forward.X);
                    for (const float side : {-1.0f, 1.0f}) {
                        Render::SprayEmitter e;
                        e.contact = car.position - car.forward * (car.lengthM * 0.32f) + right * (side * car.widthM * 0.4f);
                        e.velocity = car.forward * car.speed;
                        sprayEmitters_.push_back(e);
                    }
                }
            }
        }
        wheelSpray_->Spray().Update(dt, sprayEmitters_, weather_.wetness);
    }

    void SimulatorGame::UpdateRumble(const Sim::VehicleState& state, const float dt)
    {
        using Microsoft::Xna::Framework::PlayerIndex;
        using Microsoft::Xna::Framework::Input::GamePad;
        if (!input_.Pad().connected) {
            rumbleLow_ = rumbleHigh_ = 0.0f;
            return;
        }
        // Low motor: wheelspin or a locked wheel, and a knock on impact that dies away; high
        // motor: the ABS pump and a rough surface under the tyres.
        float slip = 0.0f;
        bool rough = false;
        for (const auto& w : state.wheels) {
            if (!w.grounded) continue;
            slip = std::max(slip, std::fabs(w.slipRatio));
            rough = rough || w.surface == Sim::SurfaceType::Gravel || w.surface == Sim::SurfaceType::Grass;
        }
        impactRumble_ = std::max(0.0f, impactRumble_ - 2.5f * dt);
        for (const auto& e : contactEvents_) {
            impactRumble_ = std::max(impactRumble_, std::min(1.0f, e.closingSpeed / 8.0f));
        }
        const bool driving = !walking_ && !state.flightMode;
        const float speedShare = std::clamp(state.speedKmh / 60.0f, 0.0f, 1.0f);
        float low = driving ? std::clamp((slip - 0.15f) * 1.5f, 0.0f, 0.6f) : 0.0f;
        float high = driving ? (state.absActive ? 0.35f : 0.0f) + (rough ? 0.18f * speedShare : 0.0f) : 0.0f;
        low = std::clamp(low + impactRumble_, 0.0f, 1.0f);
        high = std::clamp(high, 0.0f, 1.0f);
        if (std::fabs(low - rumbleLow_) > 0.02f || std::fabs(high - rumbleHigh_) > 0.02f) {
            rumbleLow_ = low;
            rumbleHigh_ = high;
            GamePad::SetVibration(PlayerIndex::One, low, high);
        }
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
        if (std::fabs(weather_.cloudCover - lastWeatherCover_) > 0.01f || std::fabs(weather_.rain - lastWeatherRain_) > 0.01f ||
            std::fabs(weather_.fog - lastWeatherFog_) > 0.01f || std::fabs(weather_.snowCover - lastWeatherSnow_) > 0.01f) {
            ApplyWeatherToWorld();
        }
        if (rain_) {
            rain_->Update(dt, weather_);
        }
        if (audio_) {
            audio_->SetWeather(weather_.rain, weather_.wetness);
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

    void SimulatorGame::UpdateTraffic(const float dt, const Vector3& previousVehicleOrigin)
    {
        if (!traffic_) {
            return;
        }
        traffic_->Update(dt, PlayerProbe(), PedestrianProbe());
        // Resolve actual contact with traffic cars after they move. In flight the entire
        // swept helicopter body is checked, including when it crosses a car in one frame.
        const Vector3 playerPosition = vehicle_->OriginPosition();
        const bool flight = vehicle_->FlightMode();
        const Vector3 travelled = playerPosition - previousVehicleOrigin;
        for (auto& car : traffic_->Vehicles()) {
            float along = 1.0f;
            if (flight && travelled.LengthSquared() > 1e-6f) {
                along = std::clamp(Vector3::Dot(car.position - previousVehicleOrigin, travelled) /
                                       travelled.LengthSquared(), 0.0f, 1.0f);
            }
            const Vector3 nearest = previousVehicleOrigin + travelled * along;
            if (Vector3::DistanceSquared(car.position, nearest) > 15.0f * 15.0f) {
                continue;
            }
            const Collision::Obb box = Collision::Obb::FromHeading(car.position + Vector3(0.0f, car.heightM * 0.5f, 0.0f),
                                                                    Vector3(car.widthM * 0.5f, car.heightM * 0.5f, car.lengthM * 0.5f), car.headingRad);
            if (flight) {
                if (collision_.ResolveFlightAgainstBox(*vehicle_, previousVehicleOrigin, box, car.Velocity(), contactEvents_)) {
                    traffic_->NotifyCollision(car.id, 4.0f);
                }
                continue;
            }
            const Vector3 impulse = collision_.ResolveVehicleAgainstBox(*vehicle_, box, car.massKg, car.Velocity(), contactEvents_);
            if (impulse.LengthSquared() > 1.0f) {
                traffic_->NotifyCollision(car.id, 4.0f);
                ++collisionCount_;
                lastImpactSpeed_ = std::max(lastImpactSpeed_, impulse.Length() / vehicle_->Body().Mass());
            }
        }
    }

    bool SimulatorGame::WalkingCanOccupy(const Vector3& position) const
    {
        const Collision::Obb walker = Collision::Obb::FromHeading(
            position + Vector3(0.0f, 0.9f, 0.0f), Vector3(0.24f, 0.85f, 0.24f), 0.0f);
        Collision::Contact contact;
        if (collision_.Overlaps(walker) ||
            Collision::IntersectObbObb(walker, Collision::CollisionWorld::VehicleBox(*vehicle_), contact)) {
            return false;
        }
        if (traffic_) {
            for (const auto& car : traffic_->Vehicles()) {
                if (Vector3::DistanceSquared(car.position, position) > 5.0f * 5.0f) continue;
                const Collision::Obb body = Collision::Obb::FromHeading(
                    car.position + Vector3(0.0f, car.heightM * 0.5f, 0.0f),
                    Vector3(car.widthM * 0.5f, car.heightM * 0.5f, car.lengthM * 0.5f), car.headingRad);
                if (Collision::IntersectObbObb(walker, body, contact)) return false;
            }
        }
        return true;
    }

    void SimulatorGame::ToggleWalking()
    {
        if (walking_) {
            walking_ = false;
            running_ = false;
            walkingMoving_ = false;
            std::cout << "walking: returned to car\n";
            return;
        }

        const Sim::VehicleState state = vehicle_->Snapshot();
        if (!CanEnterWalking(state)) {
            return;
        }

        const Vector3 forward = state.worldMatrix.getForwardProperty();
        const Vector3 right = state.worldMatrix.getRightProperty();
        const float offset = definition_.chassis.widthM * 0.5f + 0.66f;
        const auto groundHeight = [this](const float x, const float z) {
            return map_ ? map_->Ground().HeightAt(x, z) : 0.0f;
        };
        // Prefer the driver's side; use the passenger side if a wall blocks the door.
        for (const float side : {-1.0f, 1.0f}) {
            const Vector3 candidate = state.originPosition + right * (side * offset);
            const Vector3 foot(candidate.X, groundHeight(candidate.X, candidate.Z), candidate.Z);
            if (!WalkingCanOccupy(foot)) continue;
            walking_ = true;
            running_ = false;
            walkingMoving_ = false;
            walkingPosition_ = foot;
            walkingYaw_ = std::atan2(-forward.X, -forward.Z);
            walkingStepDistance_ = 0.5f;
            walkingBobPhase_ = 0.0f;
            std::cout << "walking: entered on foot\n";
            return;
        }
        std::cout << "walking: no space next to car\n";
    }

    void SimulatorGame::UpdateWalking(const float dt)
    {
        using Microsoft::Xna::Framework::Input::Keyboard;
        using Microsoft::Xna::Framework::Input::Keys;
        const auto keys = Keyboard::GetState();
        const float turn = (keys.IsKeyDown(Keys::Right) ? 1.0f : 0.0f) -
                           (keys.IsKeyDown(Keys::Left) ? 1.0f : 0.0f);
        walkingYaw_ += turn * 2.1f * dt;
        const float forward = (keys.IsKeyDown(Keys::Up) ? 1.0f : 0.0f) -
                              (keys.IsKeyDown(Keys::Down) ? 1.0f : 0.0f);
        const float sideways = (keys.IsKeyDown(Keys::D) ? 1.0f : 0.0f) -
                               (keys.IsKeyDown(Keys::A) ? 1.0f : 0.0f);
        Vector3 direction(std::sin(walkingYaw_) * forward + std::cos(walkingYaw_) * sideways,
                          0.0f,
                          -std::cos(walkingYaw_) * forward + std::sin(walkingYaw_) * sideways);
        walkingMoving_ = false;
        if (direction.LengthSquared() < 0.001f) return;
        direction.Normalize();
        const float distance = (running_ ? kRunningSpeedKmh : kWalkingSpeedKmh) / 3.6f * std::clamp(dt, 0.0f, 0.25f);
        const int steps = std::max(1, static_cast<int>(std::ceil(distance / 0.08f)));
        const Vector3 delta = direction * (distance / static_cast<float>(steps));
        const auto groundHeight = [this](const float x, const float z) {
            return map_ ? map_->Ground().HeightAt(x, z) : 0.0f;
        };
        float travelled = 0.0f;
        for (int i = 0; i < steps; ++i) {
            const Vector3 previous = walkingPosition_;
            Vector3 next(walkingPosition_.X + delta.X, 0.0f, walkingPosition_.Z);
            next.Y = groundHeight(next.X, next.Z);
            if (WalkingCanOccupy(next)) walkingPosition_ = next;
            next = Vector3(walkingPosition_.X, 0.0f, walkingPosition_.Z + delta.Z);
            next.Y = groundHeight(next.X, next.Z);
            if (WalkingCanOccupy(next)) walkingPosition_ = next;
            const Vector3 actual = walkingPosition_ - previous;
            travelled += std::hypot(actual.X, actual.Z);
        }
        walkingMoving_ = travelled > 0.001f;
        walkingBobPhase_ += travelled * (2.0f * std::numbers::pi_v<float> / 1.5f);
        walkingStepDistance_ += travelled;
        const float stride = running_ ? 0.95f : 0.72f;
        while (walkingStepDistance_ >= stride) {
            walkingStepDistance_ -= stride;
            if (audio_) audio_->TriggerFootstep();
        }
    }

    void SimulatorGame::Update(GameTime& gameTime)
    {
        if (exitRequested_) {
            return;
        }
        // Lockstep captures: the fixed-step game loop calls Update several times per Draw when
        // rendering is slow (software renderers); keep exactly one step per drawn frame so a
        // capture at frame N is the same scene whatever the renderer speed.
        if (options_.lockstep && updatesSinceDraw_ > 0) {
            return;
        }
        ++updatesSinceDraw_;
        const auto frameStart = std::chrono::steady_clock::now();
        const float dt = static_cast<float>(gameTime.getElapsedGameTimeProperty().getTotalSecondsProperty());
        elapsedSeconds_ += static_cast<double>(dt);

        input_.Update();
        HandleAppActions();
        if (exitRequested_) {
            return;
        }
        UpdateTimeOfDay(dt);
        UpdateWeather(dt);

        const bool wasWalking = walking_;
        if (input_.Pressed(GameAction::ToggleWalk)) ToggleWalking();
        const bool walkingTransition = walking_ != wasWalking;
        if (walking_ && input_.Pressed(GameAction::ToggleRun)) running_ = !running_;

        Sim::DriverControls controls = input_.BuildDriverControls(vehicle_->GetTransmission().Mode());
        if (walking_) {
            // Hold the parked vehicle still even on a slope or with a gear selected.
            controls = {};
            controls.brake = 1.0f;
            controls.handbrake = true;
        } else {
            if (walkingTransition) controls.throttle = 0.0f;
            ApplyAutoDrive(controls);
        }
        if (routeDriver_ && !vehicle_->FlightMode() && !walking_) {
            // The autopilot owns the pedals and the wheel; everything else (camera, overlays,
            // quit) still answers to the keyboard.
            Sim::DriverControls driven = routeDriver_->Update(vehicle_->Snapshot(), dt);
            driven.toggleHeadlights = controls.toggleHeadlights;
            driven.toggleHighBeam = controls.toggleHighBeam;
            driven.toggleTurbo = controls.toggleTurbo;
            driven.toggleFlight = controls.toggleFlight;
            driven.flightClimb = controls.flightClimb;
            driven.flightDescend = controls.flightDescend;
            controls = driven;
            const auto& progress = routeDriver_->Progress();
            if (progress.finished && !routeReported_) {
                routeReported_ = true;
                ReportRoute();
            }
        }
        const Sim::GroundSurface& ground = map_ ? static_cast<const Sim::GroundSurface&>(map_->Ground()) : ground_;
        // Each stage of the update is timed separately so the overlay and the benchmark can say
        // where the update half of a frame goes, rather than reporting one opaque number.
        auto stageClock = std::chrono::steady_clock::now();
        const auto stage = [&stageClock](float& out) {
            const auto now = std::chrono::steady_clock::now();
            out = std::chrono::duration<float, std::milli>(now - stageClock).count();
            stageClock = now;
        };
        const Vector3 previousOrigin = vehicle_->OriginPosition();
        vehicle_->Update(controls, dt, ground);
        if (vehicle_->FlightMode()) cameraMode_ = Render::CameraMode::Chase;
        startRefusedHintSeconds_ = vehicle_->StartRefused() ? 4.0f : std::max(0.0f, startRefusedHintSeconds_ - dt);
        // The lever was moved without the clutch: say so, instead of a silent grind.
        grindHintSeconds_ = vehicle_->GrindCount() != lastGrindCount_ ? 3.0f : std::max(0.0f, grindHintSeconds_ - dt);
        lastGrindCount_ = vehicle_->GrindCount();
        stage(vehicleMs_);
        contactEvents_.clear();
        if (vehicle_->FlightMode()) collision_.ResolveFlight(*vehicle_, previousOrigin, contactEvents_);
        else collision_.ResolveVehicle(*vehicle_, contactEvents_);
        stage(collisionMs_);
        UpdateTraffic(dt, previousOrigin);
        if (walking_) UpdateWalking(dt);
        stage(trafficMs_);
        for (const auto& e : contactEvents_) {
            if (e.closingSpeed > 0.5f) {
                ++collisionCount_;
                lastImpactSpeed_ = std::max(lastImpactSpeed_, e.closingSpeed);
            }
        }

        const auto state = vehicle_->Snapshot();
        UpdateRumble(state, dt);
        UpdateSpray(state, dt);
        if (windscreenRain_) {
            // The screen keeps its drops whichever camera is in use; under the helicopter or
            // with the driver out walking nothing new lands on it that matters.
            windscreenRain_->Rain().Update(dt, state.flightMode ? 0.0f : std::max(weather_.rain, 0.6f * weather_.snow), state.speedKmh / 3.6f,
                                           state.wiperPosition);
        }
        if (exhaustSmoke_) {
            const float bearing = weather_.windFromDeg * std::numbers::pi_v<float> / 180.0f;
            const Vector3 wind(-std::sin(bearing) * weather_.windSpeedMs, 0.0f,
                               std::cos(bearing) * weather_.windSpeedMs);
            exhaustSmoke_->Smoke().Update(dt, state, wind);
        }
        chaseCamera_.distance = options_.chaseDistanceM.value_or(state.flightMode ? 14.0f : 6.2f);
        chaseCamera_.height = state.flightMode ? 5.0f : 2.0f;
        chaseCamera_.targetHeight = state.flightMode ? 0.6f : 0.9f;
        if (controls.toggleFlight) chaseCamera_.Snap(state);
        chaseCamera_.Update(state, dt);
        cockpitCamera_.Update(state, definition_, dt);
        stageClock = std::chrono::steady_clock::now();
        if (audio_) {
            audio_->Update(state, !walking_ && cameraMode_ == Render::CameraMode::Cockpit, contactEvents_, dt);
        }
        stage(audioMs_);
        saveTimer_ += static_cast<double>(dt);
        if (saveTimer_ > 30.0) {
            saveTimer_ = 0.0;
            WriteSave();
        }

        Game::Update(gameTime);
        frameMs_ = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - frameStart).count();
    }

    void SimulatorGame::Draw(const GameTime& gameTime)
    {
        updatesSinceDraw_ = 0;
        const auto drawStart = std::chrono::steady_clock::now();
        auto& device = getGraphicsDeviceProperty();
        const auto& viewport = device.getViewportProperty();
        const float aspect = static_cast<float>(viewport.getWidthProperty()) / static_cast<float>(std::max(1, viewport.getHeightProperty()));
        viewportWidth_ = viewport.getWidthProperty();
        viewportHeight_ = viewport.getHeightProperty();

        const auto state = vehicle_->Snapshot();
        if (worldRenderer_) {
            worldRenderer_->SetHeadlights(state.originPosition, state.worldMatrix.getForwardProperty(),
                                           state.lowBeam || state.flightMode ? rig_.LampFactor() : 0.0f, state.highBeam);
        }
        Render::GaugePose gauges;
        gauges.speed = state.speedKmh / definition_.dashboard.speedometerMaxKmh;
        gauges.rpm = state.engineRpm / definition_.dashboard.tachometerMaxRpm;
        gauges.fuel = state.fuelFraction;
        gauges.temperature = (state.coolantC - definition_.dashboard.temperatureMinC) /
                             std::max(1.0f, definition_.dashboard.temperatureMaxC - definition_.dashboard.temperatureMinC);
        const bool cockpit = !walking_ && cameraMode_ == Render::CameraMode::Cockpit && !options_.freeView;
        const int mirrorEvery = std::max(1, save_.settings.mirrorUpdateEvery);
        const bool mirrorPass = cockpit && mirrorEnabled_ && (framesDrawn_ % mirrorEvery == 0 || !mirror_->Texture());
        auto passClock = std::chrono::steady_clock::now();
        const auto lap = [&](const int pass) {
            const auto now = std::chrono::steady_clock::now();
            passMs_[pass] = std::chrono::duration<float, std::milli>(now - passClock).count();
            passClock = now;
        };
        for (float& v : passMs_) v = 0.0f;

        // Ground queries for draping car shadows on roads, kerbs and terrain.
        Render::GroundQuery groundQuery;
        groundQuery.height = [this](const float x, const float z) { return map_ ? map_->Ground().HeightAt(x, z) : 0.0f; };
        groundQuery.normal = [this](const float x, const float z) { return map_ ? map_->Ground().NormalAt(x, z) : Vector3(0.0f, 1.0f, 0.0f); };

        // Off-screen passes first: the instrument cluster and, in the cockpit, the rear-view mirror.
        cluster_->Render(device, *spriteBatch_, state, elapsedSeconds_);
        vehicleRenderer_->SetClusterTexture(cluster_->Texture());
        lap(kPassCluster);
        // Everything a mirror shows, into the target `m` has bound.
        const auto drawMirrorScene = [&](Render::MirrorView& m, const float distance) {
            sky_->Draw(device, m.View(), m.Projection(), m.Pose().position, true);
            if (worldRenderer_) {
                worldRenderer_->Draw(device, m.View(), m.Projection(), m.Frustum(), true,
                                     distance);
            }
            vehicleRenderer_->SetPlateTexture(playerPlate_);
            vehicleRenderer_->DrawOpaque(device, state, m.View(), m.Projection(), false, gauges, true);
            vehicleRenderer_->DrawTransparent(device, state, m.View(), m.Projection(), true);
            if (exhaustSmoke_) exhaustSmoke_->Draw(device, m.View(), m.Projection(), m.Pose().position);
            if (wheelSpray_) wheelSpray_->Draw(device, m.View(), m.Projection(), m.Pose().position, rig_.fogColor);
            if (traffic_ && trafficRenderer_) {
                trafficRenderer_->Draw(device, *traffic_, m.View(), m.Projection(), m.Frustum(), m.Pose().position, rig_,
                                       groundQuery, true);
            }
            if (map_ && trafficRenderer_) {
                trafficRenderer_->DrawParked(device, map_->Objects().Vehicles(), parkedPlates_, m.View(), m.Projection(),
                                             m.Frustum(), m.Pose().position, rig_, groundQuery, true);
            }
        };
        if (mirrorPass) {
            mirror_->Update(state, definition_);
            mirror_->Begin(device);
            drawMirrorScene(*mirror_, qualitySettings_.mirrorDistanceM);
            mirror_->End(device);
            vehicleRenderer_->SetMirrorTexture(mirror_->Texture());
        } else if (cockpit && mirrorEnabled_) {
            vehicleRenderer_->SetMirrorTexture(mirror_->Texture());   // half-rate: keep the previous image
        } else {
            vehicleRenderer_->SetMirrorTexture(nullptr);
        }
        // Wing mirrors: smaller images, one side per frame, not on the low tier.
        const bool wings = cockpit && mirrorEnabled_ && quality_ != Render::QualityTier::Low && wingMirrors_[0];
        if (wings) {
            const int side = static_cast<int>(framesDrawn_ % 2);
            for (int s = 0; s < 2; ++s) {
                if (s != side && wingMirrorsDrawn_[static_cast<std::size_t>(s)]) continue;
                auto& wing = *wingMirrors_[static_cast<std::size_t>(s)];
                const auto& glass = vehicleRenderer_->Model().wingMirrors[static_cast<std::size_t>(s)];
                wing.UpdateWing(state, glass.centre, glass.yaw);
                wing.Begin(device);
                drawMirrorScene(wing, std::min(150.0f, qualitySettings_.mirrorDistanceM));
                wing.End(device);
                wingMirrorsDrawn_[static_cast<std::size_t>(s)] = true;
            }
            vehicleRenderer_->SetWingMirrorTextures(wingMirrors_[0]->Texture(), wingMirrors_[1]->Texture());
        } else {
            vehicleRenderer_->SetWingMirrorTextures(nullptr, nullptr);
        }
        lap(kPassMirror);

        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil, Color(120, 160, 210, 255), 1.0f, 0);

        Render::CameraPose camera = cameraMode_ == Render::CameraMode::Chase ? chaseCamera_.Pose() : cockpitCamera_.Pose();
        if (walking_) {
            const float bob = walkingMoving_ ? 0.025f * std::sin(walkingBobPhase_) : 0.0f;
            camera.position = walkingPosition_ + Vector3(0.0f, 1.68f + bob, 0.0f);
            camera.target = camera.position + Vector3(std::sin(walkingYaw_), 0.0f, -std::cos(walkingYaw_));
            camera.up = Vector3(0.0f, 1.0f, 0.0f);
            camera.fieldOfViewDeg = 68.0f;
            camera.nearPlane = 0.08f;
        } else if (options_.freeView) {
            const auto& fv = *options_.freeView;
            const float heading = fv.headingDeg * (std::numbers::pi_v<float> / 180.0f);
            const float pitch = fv.pitchDeg * (std::numbers::pi_v<float> / 180.0f);
            camera.position = Vector3(fv.x, fv.y, fv.z);
            camera.target = camera.position + Vector3(std::sin(heading) * std::cos(pitch), std::sin(pitch), -std::cos(heading) * std::cos(pitch));
            camera.up = Vector3(0.0f, 1.0f, 0.0f);
            camera.fieldOfViewDeg = 60.0f;
            camera.nearPlane = 0.3f;
        }
        const Matrix view = camera.View();
        const Matrix projection = camera.Projection(aspect);

        sky_->Draw(device, camera, aspect);
        lap(kPassSky);
        if (worldRenderer_) {
            worldRenderer_->Draw(device, view, projection, camera.Frustum(aspect));
        } else if (testGround_) {
            testGround_->Draw(device, view, projection);
        }
        if (signalRenderer_ && traffic_) {
            signalRenderer_->Draw(device, view, projection, camera.Frustum(aspect), camera.position, rig_,
                                  [this](const int intersection, const int group) { return traffic_->AspectOf(intersection, group); });
        }
        lap(kPassWorld);

        if (traffic_ && trafficRenderer_) {
            trafficRenderer_->Draw(device, *traffic_, view, projection, camera.Frustum(aspect), camera.position, rig_, groundQuery, false);
        }
        if (map_ && trafficRenderer_) {
            trafficRenderer_->DrawParked(device, map_->Objects().Vehicles(), parkedPlates_, view, projection, camera.Frustum(aspect),
                                         camera.position, rig_, groundQuery, false);
        }
        lap(kPassTraffic);
        vehicleRenderer_->SetPlateTexture(playerPlate_);
        vehicleRenderer_->DrawOpaque(device, state, view, projection, cockpit, gauges);
        vehicleRenderer_->DrawShadow(device, state, view, projection, rig_.sunDirection, groundQuery);
        vehicleRenderer_->DrawHeadlightPool(device, state, view, projection, groundQuery, rig_.LampFactor());
        vehicleRenderer_->DrawTransparent(device, state, view, projection, false, cockpit);
        if (windscreenRain_ && !state.flightMode) {
            const Vector3 light = rig_.fogColor * 0.85f + Vector3(0.08f, 0.08f, 0.08f);
            windscreenRain_->Draw(device, view, projection, state.worldMatrix, vehicleRenderer_->Model().windscreen, state.wiperPosition, light);
        }
        if (exhaustSmoke_) exhaustSmoke_->Draw(device, view, projection, camera.position);
        if (wheelSpray_) wheelSpray_->Draw(device, view, projection, camera.position, rig_.fogColor);
        if (!cockpit) {
            vehicleRenderer_->DrawLampGlows(device, state, view, projection);
        }
        DrawWetReflections(device, state, view, projection, camera.position);
        if (rain_) {
            rain_->Draw(device, view, projection, camera.position, rig_.fogColor);
        }
        lap(kPassVehicle);

        if (hudVisible_ || showHelp_) {
            DrawHud();
        }
        if (showMap_) {
            DrawMap();
        }
        if (showDebug_) {
            DrawDebugOverlay();
        }
        if (showHelp_) {
            DrawHelp();
        }
        lap(kPassHud);

        Game::Draw(gameTime);
        const auto drawEnd = std::chrono::steady_clock::now();
        drawMs_ = std::chrono::duration<float, std::milli>(drawEnd - drawStart).count();
        // Frame pacing: the wall-clock gap between drawn frames, which includes whatever the
        // driver and the present do outside our own timers. The first sample has no predecessor.
        if (lastPacingSample_.time_since_epoch().count() != 0) {
            framePacingMs_[static_cast<std::size_t>(framePacingNext_)] =
                std::chrono::duration<float, std::milli>(drawEnd - lastPacingSample_).count();
            framePacingNext_ = (framePacingNext_ + 1) % kFramePacingWindow;
            framePacingCount_ = std::min(framePacingCount_ + 1, kFramePacingWindow);
        }
        lastPacingSample_ = drawEnd;
        if (options_.benchmark) {
            if (framesDrawn_ >= bench_.warmupFrames) {
                ++bench_.frames;
                bench_.updateSum += frameMs_;
                bench_.updateMax = std::max(bench_.updateMax, static_cast<double>(frameMs_));
                bench_.drawSum += drawMs_;
                bench_.drawMax = std::max(bench_.drawMax, static_cast<double>(drawMs_));
                if (lastFrameEnd_.time_since_epoch().count() != 0) {
                    const double gap = std::chrono::duration<double, std::milli>(drawEnd - lastFrameEnd_).count();
                    bench_.wallSum += gap;
                    bench_.wallSamples.push_back(static_cast<float>(gap));
                }
                bench_.vehicleSum += vehicleMs_;
                bench_.collisionSum += collisionMs_;
                bench_.trafficMsSum += trafficMs_;
                bench_.audioSum += audioMs_;
                if (worldRenderer_) {
                    const auto& ws = worldRenderer_->Stats();
                    bench_.drawCalls += ws.drawCalls + vehicleRenderer_->DrawCallsLastFrame();
                    bench_.triangles += ws.triangles + vehicleRenderer_->TriangleCount();
                    bench_.terrainChunks += ws.terrainChunksDrawn;
                    bench_.roadBatches += ws.roadBatchesDrawn;
                    bench_.objectBatches += ws.objectBatchesDrawn;
                    bench_.treeBatches += ws.treeBatchesDrawn;
                }
                if (trafficRenderer_) {
                    const auto& ts = trafficRenderer_->Stats();
                    bench_.drawCalls += ts.drawCalls;
                    bench_.trafficDrawn += ts.drawn;
                    bench_.parkedDrawn += ts.parkedDrawn;
                    bench_.trafficLod0 += ts.lod0;
                    bench_.trafficLod1 += ts.lod1;
                    bench_.trafficLod2 += ts.lod2;
                }
                bench_.trafficCount += traffic_ ? static_cast<long long>(traffic_->Vehicles().size()) : 0;
                for (int i = 0; i < kPassCount; ++i) bench_.passSum[i] += passMs_[i];
            }
            lastFrameEnd_ = drawEnd;
        }
        FinishFrame();
    }

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
            font_->DrawShadowed(*spriteBatch_, walking_ ? "W car   Shift run   F1 help" :
                                "F1 help   C camera   E engine   M map   X smoke   L lights   K low/high",
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
            row("audio stream", text("%d blocks queued ahead, %d underruns so far",
                                     Audio::VehicleAudio::kTargetPendingBlocks, audio_->Underruns()));
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

        section("driving");
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

    void SimulatorGame::FinishFrame()
    {
        ++framesDrawn_;
        auto& device = getGraphicsDeviceProperty();
        if (screenshotRequested_) {
            screenshotRequested_ = false;
            char name[64];
            std::snprintf(name, sizeof(name), "screenshot_%04d.png", framesDrawn_);
            if (Render::SaveBackBufferPng(device, name)) {
                std::cout << "screenshot saved to " << name << "\n";
            }
        }
        const bool routeDone = routeReported_ && !options_.routeLoopStay;
        const bool framesDone = options_.frames && framesDrawn_ >= *options_.frames;
        if ((!framesDone && !routeDone) || exitRequested_) {
            return;
        }
        if (options_.clusterScreenshotPath && cluster_ && cluster_->Texture()) {
            if (Render::SaveTexturePng(*cluster_->Texture(), *options_.clusterScreenshotPath)) {
                std::cout << "cluster screenshot saved to " << *options_.clusterScreenshotPath << "\n";
            }
        }
        if (options_.screenshotPath) {
            if (Render::SaveBackBufferPng(device, *options_.screenshotPath)) {
                std::cout << "screenshot saved to " << *options_.screenshotPath << "\n";
            }
        }
        if (options_.benchmark && bench_.frames > 0) {
            const double n = static_cast<double>(bench_.frames);
            static const char* const passNames[kPassCount] = {"cluster", "mirror", "sky", "world", "traffic", "vehicle", "hud"};
            // Worst 1 % of the wall-clock frame gaps: the number that tells you whether a run was
            // smooth, which an average never does.
            double wallWorst = 0.0;
            if (!bench_.wallSamples.empty()) {
                auto samples = bench_.wallSamples;
                const std::size_t rank = samples.size() - 1u - samples.size() / 100u;
                std::nth_element(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(rank), samples.end());
                wallWorst = samples[rank];
            }
            const std::string scene = FormatClock(timeOfDayHours_) + " " + Core::ToString(weather_.kind) + " " +
                                      (options_.freeView ? "free" : (cameraMode_ == Render::CameraMode::Cockpit ? "cockpit" : "chase")) +
                                      (options_.spawn ? " spawn=" + *options_.spawn : std::string());
            std::cout << "benchmark: " << bench_.frames << " frames after " << bench_.warmupFrames << " warm-up frames, "
                      << viewportWidth_ << "x" << viewportHeight_ << ", scene " << scene << "\n"
                      << "  update  avg " << bench_.updateSum / n << " ms, max " << bench_.updateMax << " ms"
                      << " (vehicle " << bench_.vehicleSum / n << ", collision " << bench_.collisionSum / n
                      << ", traffic " << bench_.trafficMsSum / n << ", audio " << bench_.audioSum / n << ")\n"
                      << "  draw    avg " << bench_.drawSum / n << " ms, max " << bench_.drawMax << " ms (CPU submission)\n"
                      << "  frame   avg " << bench_.wallSum / std::max(1.0, n - 1.0) << " ms wall clock, worst 1% "
                      << wallWorst << " ms\n"
                      << "  scene   avg " << static_cast<double>(bench_.drawCalls) / n << " draw calls, "
                      << static_cast<double>(bench_.triangles) / n / 1000.0 << "k triangles\n"
                      << "  passes  avg ms:";
            for (int i = 0; i < kPassCount; ++i) std::cout << " " << passNames[i] << " " << bench_.passSum[i] / n;
            std::cout << "\n  visible avg: terrain chunks " << static_cast<double>(bench_.terrainChunks) / n << ", road batches "
                      << static_cast<double>(bench_.roadBatches) / n << ", object batches " << static_cast<double>(bench_.objectBatches) / n
                      << ", tree batches " << static_cast<double>(bench_.treeBatches) / n << "\n"
                      << "  traffic avg: " << static_cast<double>(bench_.trafficCount) / n << " cars, drawn " << static_cast<double>(bench_.trafficDrawn) / n
                      << " (parked drawn " << static_cast<double>(bench_.parkedDrawn) / n << ")"
                      << " (lod0 " << static_cast<double>(bench_.trafficLod0) / n << ", lod1 " << static_cast<double>(bench_.trafficLod1) / n << ", lod2 "
                      << static_cast<double>(bench_.trafficLod2) / n << ")\n";
            if (options_.benchmarkJsonPath) {
                std::ofstream json(*options_.benchmarkJsonPath);
                if (json) {
                    json.setf(std::ios::fixed);
                    json.precision(3);
                    json << "{\n  \"frames\": " << bench_.frames << ",\n  \"warmupFrames\": " << bench_.warmupFrames << ",\n  \"width\": " << viewportWidth_
                         << ",\n  \"height\": " << viewportHeight_ << ",\n  \"updateMsAvg\": " << bench_.updateSum / n << ",\n  \"updateMsMax\": " << bench_.updateMax
                         << ",\n  \"drawMsAvg\": " << bench_.drawSum / n << ",\n  \"drawMsMax\": " << bench_.drawMax << ",\n  \"frameMsAvg\": "
                         << bench_.wallSum / std::max(1.0, n - 1.0) << ",\n  \"frameMsWorst1pc\": " << wallWorst
                         << ",\n  \"scene\": \"" << scene << "\""
                         << ",\n  \"updateMsAvgSplit\": {\"vehicle\": " << bench_.vehicleSum / n << ", \"collision\": "
                         << bench_.collisionSum / n << ", \"traffic\": " << bench_.trafficMsSum / n << ", \"audio\": "
                         << bench_.audioSum / n << "}"
                         << ",\n  \"drawCallsAvg\": " << static_cast<double>(bench_.drawCalls) / n
                         << ",\n  \"trianglesAvg\": " << static_cast<double>(bench_.triangles) / n << ",\n  \"passesMsAvg\": {";
                    for (int i = 0; i < kPassCount; ++i) json << (i ? ", " : "") << "\"" << passNames[i] << "\": " << bench_.passSum[i] / n;
                    json << "},\n  \"visibleAvg\": {\"terrainChunks\": " << static_cast<double>(bench_.terrainChunks) / n << ", \"roadBatches\": "
                         << static_cast<double>(bench_.roadBatches) / n << ", \"objectBatches\": " << static_cast<double>(bench_.objectBatches) / n
                         << ", \"treeBatches\": " << static_cast<double>(bench_.treeBatches) / n << "},\n  \"trafficAvg\": {\"cars\": "
                         << static_cast<double>(bench_.trafficCount) / n << ", \"drawn\": " << static_cast<double>(bench_.trafficDrawn) / n
                         << ", \"parkedDrawn\": " << static_cast<double>(bench_.parkedDrawn) / n << ", \"lod0\": "
                         << static_cast<double>(bench_.trafficLod0) / n << ", \"lod1\": " << static_cast<double>(bench_.trafficLod1) / n << ", \"lod2\": "
                         << static_cast<double>(bench_.trafficLod2) / n << "},\n  \"mirrorUpdateEvery\": " << std::max(1, save_.settings.mirrorUpdateEvery)
                         << "\n}\n";
                    std::cout << "benchmark JSON written to " << *options_.benchmarkJsonPath << "\n";
                }
            }
        }
        std::cout << (routeDone ? "route finished (" : "frame limit reached (") << framesDrawn_ << " frames); exiting\n";
        exitRequested_ = true;
        Exit();
    }
}
