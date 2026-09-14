#include "CarSim/App/SimulatorGame.hpp"

#include "CarSim/Map/MapDocument.hpp"

#include "CarSim/Core/Version.hpp"
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
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    using Input::GameAction;

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
        graphics_.setPreferMultiSamplingProperty(true);

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
        if (!options_.cockpit && save_.settings.startInCockpit) {
            options_.cockpit = true;
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
        save_.settings.startInCockpit = cameraMode_ == Render::CameraMode::Cockpit;
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
            const Map::SpawnSpec spawn = map_->PlayerSpawn(options_.spawn.value_or(std::string()));
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
        LoadMap();
        LoadVehicle();
        ApplySaveToVehicle();

        sky_ = std::make_unique<Render::SkyRenderer>(device, rig_);
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
            std::cout << "world: " << worldRenderer_->Stats().terrainChunksTotal << " terrain chunks, "
                      << worldRenderer_->Stats().roadBatchesTotal << " road batches, built in "
                      << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << " s\n";
        } else {
            testGround_ = std::make_unique<Render::TestGround>(device, rig_);
        }
        vehicleMaterials_ = std::make_unique<Render::VehicleMaterials>(device, rig_);
        vehicleRenderer_ = std::make_unique<Render::VehicleRenderer>(device, *vehicleMaterials_, definition_);
        plateFont_ = Render::BitmapFont::Load(getContentProperty(), contentRoot_, "fonts/plate_bold_128");
        trafficRenderer_ = std::make_unique<Render::TrafficRenderer>(device, *vehicleMaterials_, plateFont_.get());
        {
            std::string plate = definition_.visual.plate;
            if (plate.empty()) {
                plate = Traffic::PlateGenerator(1u).Next();
            }
            playerPlate_ = trafficRenderer_->PlateTexture(device, plate);
        }
        spriteBatch_ = std::make_unique<SpriteBatch>(device);

        gaugeFont_ = Render::BitmapFont::Load(getContentProperty(), contentRoot_, "fonts/gauge_condensed_96");
        if (!gaugeFont_) {
            gaugeFont_ = Render::BitmapFont::CreateBuiltin(device);
        }
        cluster_ = std::make_unique<Render::InstrumentCluster>(device, definition_, *gaugeFont_, *font_, *fontBold_);
        mirror_ = std::make_unique<Render::MirrorView>(device);
        chaseCamera_.groundHeight = [this](const float x, const float z) { return map_ ? map_->Ground().HeightAt(x, z) : 0.0f; };
        chaseCamera_.Snap(vehicle_->Snapshot());
        if (options_.mirrorEvery) {
            save_.settings.mirrorUpdateEvery = *options_.mirrorEvery;
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
        if (input_.Pressed(GameAction::Quit)) {
            exitRequested_ = true;
            Exit();
        }
        if (input_.Pressed(GameAction::ToggleCamera)) {
            cameraMode_ = cameraMode_ == Render::CameraMode::Chase ? Render::CameraMode::Cockpit : Render::CameraMode::Chase;
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
        return probe;
    }

    void SimulatorGame::UpdateTraffic(const float dt)
    {
        if (!traffic_) {
            return;
        }
        traffic_->Update(dt, PlayerProbe());
        // Player against traffic cars: the AI car acts as a moving box with mass; it stops for a
        // while after a hit.
        const Vector3 playerPosition = vehicle_->OriginPosition();
        for (auto& car : traffic_->Vehicles()) {
            if (Vector3::DistanceSquared(car.position, playerPosition) > 15.0f * 15.0f) {
                continue;
            }
            const Collision::Obb box = Collision::Obb::FromHeading(car.position + Vector3(0.0f, car.heightM * 0.5f, 0.0f),
                                                                    Vector3(car.widthM * 0.5f, car.heightM * 0.5f, car.lengthM * 0.5f), car.headingRad);
            const Vector3 impulse = collision_.ResolveVehicleAgainstBox(*vehicle_, box, car.massKg, car.Velocity(), contactEvents_);
            if (impulse.LengthSquared() > 1.0f) {
                traffic_->NotifyCollision(car.id, 4.0f);
                ++collisionCount_;
                lastImpactSpeed_ = std::max(lastImpactSpeed_, impulse.Length() / vehicle_->Body().Mass());
            }
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

        Sim::DriverControls controls = input_.BuildDriverControls(vehicle_->GetTransmission().Mode());
        ApplyAutoDrive(controls);
        const Sim::GroundSurface& ground = map_ ? static_cast<const Sim::GroundSurface&>(map_->Ground()) : ground_;
        vehicle_->Update(controls, dt, ground);
        contactEvents_.clear();
        collision_.ResolveVehicle(*vehicle_, contactEvents_);
        UpdateTraffic(dt);
        for (const auto& e : contactEvents_) {
            if (e.closingSpeed > 0.5f) {
                ++collisionCount_;
                lastImpactSpeed_ = std::max(lastImpactSpeed_, e.closingSpeed);
            }
        }

        const auto state = vehicle_->Snapshot();
        chaseCamera_.Update(state, dt);
        cockpitCamera_.Update(state, definition_, dt);
        if (audio_) {
            audio_->Update(state, cameraMode_ == Render::CameraMode::Cockpit, contactEvents_, dt);
        }
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
        Render::GaugePose gauges;
        gauges.speed = state.speedKmh / definition_.dashboard.speedometerMaxKmh;
        gauges.rpm = state.engineRpm / definition_.dashboard.tachometerMaxRpm;
        gauges.fuel = state.fuelFraction;
        gauges.temperature = (state.coolantC - definition_.dashboard.temperatureMinC) /
                             std::max(1.0f, definition_.dashboard.temperatureMaxC - definition_.dashboard.temperatureMinC);
        const bool cockpit = cameraMode_ == Render::CameraMode::Cockpit && !options_.freeView;
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
        if (mirrorPass) {
            mirror_->Update(state, definition_);
            mirror_->Begin(device);
            sky_->Draw(device, mirror_->View(), mirror_->Projection(), mirror_->Pose().position, true);
            if (worldRenderer_) {
                worldRenderer_->Draw(device, mirror_->View(), mirror_->Projection(), mirror_->Frustum(), true);
            }
            vehicleRenderer_->SetPlateTexture(playerPlate_);
            vehicleRenderer_->DrawOpaque(device, state, mirror_->View(), mirror_->Projection(), false, gauges, true);
            vehicleRenderer_->DrawTransparent(device, state, mirror_->View(), mirror_->Projection(), true);
            if (traffic_ && trafficRenderer_) {
                trafficRenderer_->Draw(device, *traffic_, mirror_->View(), mirror_->Projection(), mirror_->Frustum(), mirror_->Pose().position, rig_,
                                       groundQuery, true);
            }
            mirror_->End(device);
            vehicleRenderer_->SetMirrorTexture(mirror_->Texture());
        } else if (cockpit && mirrorEnabled_) {
            vehicleRenderer_->SetMirrorTexture(mirror_->Texture());   // half-rate: keep the previous image
        } else {
            vehicleRenderer_->SetMirrorTexture(nullptr);
        }
        lap(kPassMirror);

        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil, Color(120, 160, 210, 255), 1.0f, 0);

        Render::CameraPose camera = cameraMode_ == Render::CameraMode::Chase ? chaseCamera_.Pose() : cockpitCamera_.Pose();
        if (options_.freeView) {
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
        lap(kPassWorld);

        if (traffic_ && trafficRenderer_) {
            trafficRenderer_->Draw(device, *traffic_, view, projection, camera.Frustum(aspect), camera.position, rig_, groundQuery, false);
        }
        lap(kPassTraffic);
        vehicleRenderer_->SetPlateTexture(playerPlate_);
        vehicleRenderer_->DrawOpaque(device, state, view, projection, cockpit, gauges);
        vehicleRenderer_->DrawShadow(device, state, view, projection, rig_.sunDirection, groundQuery);
        vehicleRenderer_->DrawTransparent(device, state, view, projection, false, cockpit);
        if (!cockpit) {
            vehicleRenderer_->DrawLampGlows(device, state, view, projection);
        }
        lap(kPassVehicle);

        if (hudVisible_ || showHelp_ || showDebug_) {
            DrawHud();
        }
        if (showHelp_) {
            DrawHelp();
        }
        lap(kPassHud);

        Game::Draw(gameTime);
        const auto drawEnd = std::chrono::steady_clock::now();
        drawMs_ = std::chrono::duration<float, std::milli>(drawEnd - drawStart).count();
        if (options_.benchmark) {
            if (framesDrawn_ >= bench_.warmupFrames) {
                ++bench_.frames;
                bench_.updateSum += frameMs_;
                bench_.updateMax = std::max(bench_.updateMax, static_cast<double>(frameMs_));
                bench_.drawSum += drawMs_;
                bench_.drawMax = std::max(bench_.drawMax, static_cast<double>(drawMs_));
                if (lastFrameEnd_.time_since_epoch().count() != 0) {
                    bench_.wallSum += std::chrono::duration<double, std::milli>(drawEnd - lastFrameEnd_).count();
                }
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
        std::snprintf(buffer, sizeof(buffer), "%3.0f km/h", static_cast<double>(s.speedKmh));
        fontBold_->DrawShadowed(*spriteBatch_, buffer, Vector2(w - 24.0f, h - 120.0f), Color(255, 255, 255, 235), 1.0f, Render::TextAlign::Right);
        std::snprintf(buffer, sizeof(buffer), "%4.0f rpm   %s   %s", static_cast<double>(s.engineRpm), s.gearLabel.c_str(),
                      s.transmissionMode == Sim::TransmissionMode::Automatic ? "AUTO" : "MANUAL");
        font_->DrawShadowed(*spriteBatch_, buffer, Vector2(w - 24.0f, h - 70.0f), Color(235, 235, 235, 220), 1.0f, Render::TextAlign::Right);
        std::string status = std::string("Engine: ") + Sim::ToString(s.engineState);
        if (vehicle_->StartRefused()) {
            status += "  (press the clutch or select N/P to start)";
        }
        font_->DrawShadowed(*spriteBatch_, status, Vector2(w - 24.0f, h - 42.0f), Color(220, 220, 220, 200), 0.8f, Render::TextAlign::Right);

        std::string lamps;
        if (s.leftIndicatorLit) lamps += "<  ";
        if (s.lowBeam) lamps += s.highBeam ? "HIGH BEAM  " : "LIGHTS  ";
        if (s.reserveWarning) lamps += "FUEL  ";
        if (s.handbrake) lamps += "(P)  ";
        if (s.rightIndicatorLit) lamps += "  >";
        if (!lamps.empty()) {
            fontBold_->DrawShadowed(*spriteBatch_, lamps, Vector2(w * 0.5f, 18.0f), Color(255, 200, 60, 230), 0.7f, Render::TextAlign::Center);
        }

        if (!showHelp_) {
            font_->DrawShadowed(*spriteBatch_, "F1 help   C camera   E engine", Vector2(20.0f, h - 34.0f), Color(230, 230, 230, 150), 0.7f);
        }

        if (showDebug_) {
            std::ostringstream dbg;
            dbg.setf(std::ios::fixed);
            dbg.precision(2);
            dbg << "frame " << frameMs_ << " ms update, " << drawMs_ << " ms draw, draw calls (vehicle) " << vehicleRenderer_->DrawCallsLastFrame()
                << "\nthrottle " << s.throttlePedal << " brake " << s.brakePedal << " clutch " << s.clutchPedal
                << " steer " << Sim::Units::RadToDeg(s.steeringWheelAngle) << " deg" << (s.clutchLocked ? " locked" : " slipping")
                << "\nfuel " << s.fuelLiters << " L (" << s.instantConsumptionLPerH << " L/h)  coolant " << s.coolantC
                << " C  odo " << s.odometerKm << " km  trip " << s.tripKm << " km"
                << "\npos " << s.originPosition.X << ", " << s.originPosition.Y << ", " << s.originPosition.Z
                << "  collisions " << collisionCount_ << " (last " << lastImpactSpeed_ * 3.6f << " km/h)"
                << "  traffic " << (traffic_ ? traffic_->Vehicles().size() : 0u) << " cars";
            if (worldRenderer_) {
                const auto& ws = worldRenderer_->Stats();
                dbg << "\nworld: terrain " << ws.terrainChunksDrawn << "/" << ws.terrainChunksTotal << " chunks, roads " << ws.roadBatchesDrawn << "/"
                    << ws.roadBatchesTotal << ", objects " << ws.objectBatchesDrawn << "/" << ws.objectBatchesTotal << ", trees " << ws.treeBatchesDrawn
                    << "/" << ws.treeBatchesTotal << ", " << ws.drawCalls << " draws, " << ws.triangles / 1000 << "k tris";
            }
            dbg << "\npasses ms: cluster " << passMs_[kPassCluster] << " mirror " << passMs_[kPassMirror] << " sky " << passMs_[kPassSky] << " world "
                << passMs_[kPassWorld] << " traffic " << passMs_[kPassTraffic] << " vehicle " << passMs_[kPassVehicle] << " hud " << passMs_[kPassHud];
            if (trafficRenderer_) {
                const auto& ts = trafficRenderer_->Stats();
                dbg << "\ntraffic drawn " << ts.drawn << " (lod0 " << ts.lod0 << ", lod1 " << ts.lod1 << ", lod2 " << ts.lod2 << "), " << ts.drawCalls
                    << " draws, mirror every " << std::max(1, save_.settings.mirrorUpdateEvery) << " frame(s)";
            }
            dbg << "\nwheels";
            for (const auto& wh : s.wheels) {
                dbg << " [" << (wh.grounded ? "g" : "-") << " sr " << wh.slipRatio << " load " << static_cast<int>(wh.load) << "]";
            }
            font_->DrawShadowed(*spriteBatch_, dbg.str().substr(0, dbg.str().find('\n')), Vector2(20.0f, 20.0f), Color(255, 255, 255, 220), 0.7f);
            std::string rest = dbg.str();
            float y = 20.0f;
            std::size_t pos = rest.find('\n');
            while (pos != std::string::npos) {
                rest = rest.substr(pos + 1);
                y += 24.0f;
                pos = rest.find('\n');
                font_->DrawShadowed(*spriteBatch_, rest.substr(0, pos), Vector2(20.0f, y), Color(255, 255, 255, 220), 0.7f);
            }
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
            GameAction::SelectorPark, GameAction::SelectorDrive, GameAction::ToggleTransmission, GameAction::ToggleEngine,
            GameAction::Handbrake, GameAction::IndicatorLeft, GameAction::IndicatorRight, GameAction::Hazard,
            GameAction::Headlights, GameAction::HighBeam, GameAction::Horn, GameAction::ToggleCamera, GameAction::ToggleMirror,
            GameAction::ToggleHud, GameAction::ToggleHelp, GameAction::ToggleDebug, GameAction::Screenshot, GameAction::ResetVehicle,
            GameAction::ResetTrip, GameAction::VolumeUp, GameAction::VolumeDown, GameAction::Quit};
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
            if (a == GameAction::Gear1) {
                keys = "1 - 6";
            }
            if (keys.size() > 22) {
                keys = keys.substr(0, 21) + "…";
            }
            font_->Draw(*spriteBatch_, keys, Vector2(x, y), Color(255, 220, 120, 255), scale);
            font_->Draw(*spriteBatch_, Input::Describe(a), Vector2(x + keyWidth, y), Color(240, 240, 240, 255), scale);
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
        if (!options_.frames || framesDrawn_ < *options_.frames || exitRequested_) {
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
            std::cout << "benchmark: " << bench_.frames << " frames after " << bench_.warmupFrames << " warm-up frames, "
                      << viewportWidth_ << "x" << viewportHeight_ << "\n"
                      << "  update  avg " << bench_.updateSum / n << " ms, max " << bench_.updateMax << " ms\n"
                      << "  draw    avg " << bench_.drawSum / n << " ms, max " << bench_.drawMax << " ms (CPU submission)\n"
                      << "  frame   avg " << bench_.wallSum / std::max(1.0, n - 1.0) << " ms wall clock\n"
                      << "  scene   avg " << static_cast<double>(bench_.drawCalls) / n << " draw calls, "
                      << static_cast<double>(bench_.triangles) / n / 1000.0 << "k triangles\n"
                      << "  passes  avg ms:";
            for (int i = 0; i < kPassCount; ++i) std::cout << " " << passNames[i] << " " << bench_.passSum[i] / n;
            std::cout << "\n  visible avg: terrain chunks " << static_cast<double>(bench_.terrainChunks) / n << ", road batches "
                      << static_cast<double>(bench_.roadBatches) / n << ", object batches " << static_cast<double>(bench_.objectBatches) / n
                      << ", tree batches " << static_cast<double>(bench_.treeBatches) / n << "\n"
                      << "  traffic avg: " << static_cast<double>(bench_.trafficCount) / n << " cars, drawn " << static_cast<double>(bench_.trafficDrawn) / n
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
                         << bench_.wallSum / std::max(1.0, n - 1.0) << ",\n  \"drawCallsAvg\": " << static_cast<double>(bench_.drawCalls) / n
                         << ",\n  \"trianglesAvg\": " << static_cast<double>(bench_.triangles) / n << ",\n  \"passesMsAvg\": {";
                    for (int i = 0; i < kPassCount; ++i) json << (i ? ", " : "") << "\"" << passNames[i] << "\": " << bench_.passSum[i] / n;
                    json << "},\n  \"visibleAvg\": {\"terrainChunks\": " << static_cast<double>(bench_.terrainChunks) / n << ", \"roadBatches\": "
                         << static_cast<double>(bench_.roadBatches) / n << ", \"objectBatches\": " << static_cast<double>(bench_.objectBatches) / n
                         << ", \"treeBatches\": " << static_cast<double>(bench_.treeBatches) / n << "},\n  \"trafficAvg\": {\"cars\": "
                         << static_cast<double>(bench_.trafficCount) / n << ", \"drawn\": " << static_cast<double>(bench_.trafficDrawn) / n << ", \"lod0\": "
                         << static_cast<double>(bench_.trafficLod0) / n << ", \"lod1\": " << static_cast<double>(bench_.trafficLod1) / n << ", \"lod2\": "
                         << static_cast<double>(bench_.trafficLod2) / n << "},\n  \"mirrorUpdateEvery\": " << std::max(1, save_.settings.mirrorUpdateEvery)
                         << "\n}\n";
                    std::cout << "benchmark JSON written to " << *options_.benchmarkJsonPath << "\n";
                }
            }
        }
        std::cout << "frame limit reached (" << framesDrawn_ << " frames); exiting\n";
        exitRequested_ = true;
        Exit();
    }
}
