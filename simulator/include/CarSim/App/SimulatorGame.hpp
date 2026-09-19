// The XNA Game subclass that owns the simulator's frame loop.
#pragma once

#include "CarSim/Audio/VehicleAudio.hpp"
#include "CarSim/Collision/CollisionWorld.hpp"
#include "CarSim/Core/CommandLine.hpp"
#include "CarSim/Core/SaveData.hpp"
#include "CarSim/Core/Weather.hpp"
#include "CarSim/Input/InputMapper.hpp"
#include "CarSim/Render/BitmapFont.hpp"
#include "CarSim/Render/Camera.hpp"
#include "CarSim/Render/ExhaustSmoke.hpp"
#include "CarSim/Render/InstrumentCluster.hpp"
#include "CarSim/Render/MirrorView.hpp"
#include "CarSim/Render/QualityTier.hpp"
#include "CarSim/Render/LightingRig.hpp"
#include "CarSim/Render/RainRenderer.hpp"
#include "CarSim/Render/SignalRenderer.hpp"
#include "CarSim/Render/SkyRenderer.hpp"
#include "CarSim/Map/MapWorld.hpp"
#include "CarSim/Render/TestGround.hpp"
#include "CarSim/Render/TrafficRenderer.hpp"
#include "CarSim/Render/WorldRenderer.hpp"
#include "CarSim/Render/VehicleRenderer.hpp"
#include "CarSim/Sim/Ground.hpp"
#include "CarSim/Sim/Vehicle.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"
#include "CarSim/Traffic/RouteDriver.hpp"
#include "CarSim/Traffic/TrafficSystem.hpp"

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"

#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace CarSim::App
{
    class SimulatorGame final : public Microsoft::Xna::Framework::Game
    {
    public:
        explicit SimulatorGame(Core::CommandLineOptions options);
        ~SimulatorGame() override;

        SimulatorGame(const SimulatorGame&) = delete;
        SimulatorGame& operator=(const SimulatorGame&) = delete;

    protected:
        void Initialize() override;
        void LoadContent() override;
        void UnloadContent() override;
        void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
        void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;
        void OnExiting(System::Object* sender, const System::EventArgs& args) override;

    private:
        void ResolveContentRoot();
        void LoadSave();
        void ApplySaveToVehicle();
        void WriteSave();
        void LoadMap();
        void LoadVehicle();
        void HandleAppActions();
        /// Advances the clock and re-applies the lighting when the sun has moved enough.
        void ApplyClockSettings();
        void ApplyWeatherSettings();
        void ApplyWeatherToWorld();
        /// Reads the graphics tier from the save (or --quality) and applies its draw distances
        /// and mirror rate. Called once the renderers exist.
        void ApplyQualitySettings();
        void UpdateWeather(float dt);
        void UpdateTimeOfDay(float dt);
        void RefreshLighting(bool force = false, bool forceEnvironment = false);
        void ApplyAutoDrive(Sim::DriverControls& controls);
        /// Plans the route named by --route. Returns false (with a message) when it cannot be
        /// driven, which a benchmark or validation run should treat as a failure.
        bool PlanRoute();
        void ReportRoute() const;
        void UpdateTraffic(float dt, const Microsoft::Xna::Framework::Vector3& previousVehicleOrigin);
        void ToggleWalking();
        void UpdateWalking(float dt);
        [[nodiscard]] bool WalkingCanOccupy(const Microsoft::Xna::Framework::Vector3& position) const;
        [[nodiscard]] Traffic::PlayerProbe PlayerProbe() const;
        [[nodiscard]] Traffic::PlayerProbe PedestrianProbe() const;
        void DrawHud();
        void DrawMap();
        void DrawHelp();
        /// The F3 diagnostic overlay: frame budget, simulation state, world and traffic counts.
        void DrawDebugOverlay();
        void FinishFrame();

        Core::CommandLineOptions options_;
        Core::SaveData save_;
        std::string savePath_;
        bool saveReadOnly_ = false;
        bool saveEnabled_ = true;
        double saveTimer_ = 0.0;
        bool hudVisible_ = true;
        bool mirrorEnabled_ = true;
        bool showMap_ = false;
        bool exhaustSmokeEnabled_ = true;
        Microsoft::Xna::Framework::GraphicsDeviceManager graphics_;
        std::string contentRoot_;

        // Simulation
        Sim::VehicleDefinition definition_;
        std::unique_ptr<Sim::Vehicle> vehicle_;
        Sim::FlatGround ground_{0.0f};              // fallback when no map is loaded
        std::unique_ptr<Map::MapWorld> map_;
        Collision::CollisionWorld collision_;
        std::unique_ptr<Traffic::TrafficSystem> traffic_;
        std::unique_ptr<Audio::VehicleAudio> audio_;
        std::vector<Collision::ContactEvent> contactEvents_;
        int collisionCount_ = 0;
        float lastImpactSpeed_ = 0.0f;
        Input::InputMapper input_;
        bool walking_ = false;
        bool running_ = false;
        bool walkingMoving_ = false;
        Microsoft::Xna::Framework::Vector3 walkingPosition_{};
        float walkingYaw_ = 0.0f;
        float walkingStepDistance_ = 0.0f;
        float walkingBobPhase_ = 0.0f;
        // Scripted driving over the lane graph (--route). Present only when a route was asked for.
        std::unique_ptr<Traffic::RouteDriver> routeDriver_;
        std::string routeName_;
        bool routeReported_ = false;
        double routeStartSeconds_ = 0.0;

        // Rendering
        Render::LightingRig rig_;
        // Clock: the sky, the light and the lamps follow it. `timeScale_` is simulated seconds
        // per real second (0 freezes the sky).
        float timeOfDayHours_ = 10.5f;
        float timeScale_ = 60.0f;
        float frozenTimeScale_ = 60.0f;   // remembered while the clock is frozen
        Core::WeatherState weather_;
        float lastWeatherCover_ = -1.0f;   // cover the rig was last rebuilt for
        float lastWeatherRain_ = -1.0f;
        float lastLightingElevationDeg_ = -999.0f;
        float lastEnvironmentElevationDeg_ = -999.0f;
        float lastEnvironmentCover_ = -999.0f;
        std::unique_ptr<Render::SkyRenderer> sky_;
        std::unique_ptr<Render::RainRenderer> rain_;
        std::unique_ptr<Render::ExhaustSmokeRenderer> exhaustSmoke_;
        std::unique_ptr<Render::SignalRenderer> signalRenderer_;
        std::unique_ptr<Render::TestGround> testGround_;
        std::unique_ptr<Render::WorldRenderer> worldRenderer_;
        std::unique_ptr<Render::TrafficRenderer> trafficRenderer_;
        std::unique_ptr<Render::BitmapFont> plateFont_;
        std::vector<std::string> parkedPlates_;   // one per map vehicle, generated at load time
        Microsoft::Xna::Framework::Graphics::Texture2D* playerPlate_ = nullptr;
        std::unique_ptr<Render::VehicleMaterials> vehicleMaterials_;
        std::unique_ptr<Render::VehicleRenderer> vehicleRenderer_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> mapTexture_;
        std::unique_ptr<Render::BitmapFont> font_;
        std::unique_ptr<Render::BitmapFont> fontBold_;
        std::unique_ptr<Render::BitmapFont> gaugeFont_;
        std::unique_ptr<Render::InstrumentCluster> cluster_;
        std::unique_ptr<Render::MirrorView> mirror_;
        Render::ChaseCamera chaseCamera_;
        Render::CockpitCamera cockpitCamera_;
        Render::CameraMode cameraMode_ = Render::CameraMode::Chase;
        Render::QualityTier quality_ = Render::QualityTier::High;
        Render::QualitySettings qualitySettings_;

        bool showHelp_ = false;
        bool showDebug_ = false;
        double elapsedSeconds_ = 0.0;
        float frameMs_ = 0.0f;
        float drawMs_ = 0.0f;
        // Where the update half of the frame goes. Measured every frame, shown by the overlay and
        // summarised by --benchmark; all of it is project-owned instrumentation, no renderer
        // internals are consulted.
        float vehicleMs_ = 0.0f;      // vehicle physics step
        float collisionMs_ = 0.0f;    // collision resolution against the world
        float trafficMs_ = 0.0f;      // traffic AI and its own collision passes
        float audioMs_ = 0.0f;        // procedural audio synthesis
        /// Wall-clock time between the end of consecutive drawn frames, newest last. The overlay
        /// reports the mean and the worst 1 % of this window, which is what a stutter looks like.
        static constexpr int kFramePacingWindow = 180;
        std::array<float, kFramePacingWindow> framePacingMs_{};
        int framePacingCount_ = 0;
        int framePacingNext_ = 0;
        std::chrono::steady_clock::time_point lastPacingSample_{};
        int viewportWidth_ = 0;
        int viewportHeight_ = 0;
        enum Pass { kPassCluster = 0, kPassMirror, kPassSky, kPassWorld, kPassTraffic, kPassVehicle, kPassHud, kPassCount };
        float passMs_[kPassCount] = {};   // CPU submission time of each draw pass in the last frame
        struct BenchmarkStats
        {
            int frames = 0;
            double updateSum = 0.0, updateMax = 0.0;
            double drawSum = 0.0, drawMax = 0.0;
            double wallSum = 0.0;
            long long drawCalls = 0, triangles = 0;
            long long terrainChunks = 0, roadBatches = 0, objectBatches = 0, treeBatches = 0;
            long long trafficCount = 0, trafficDrawn = 0, trafficLod0 = 0, trafficLod1 = 0, trafficLod2 = 0, parkedDrawn = 0;
            double passSum[kPassCount] = {};
            // The update half, split the same way the overlay splits it.
            double vehicleSum = 0.0, collisionSum = 0.0, trafficMsSum = 0.0, audioSum = 0.0;
            std::vector<float> wallSamples;   // every wall-clock frame gap, for the worst 1 %
            int warmupFrames = 30;
        } bench_;
        std::chrono::steady_clock::time_point lastFrameEnd_{};
        int framesDrawn_ = 0;
        int updatesSinceDraw_ = 0;
        bool exitRequested_ = false;
        bool screenshotRequested_ = false;
        bool autoDriveStarted_ = false;
        bool lightsApplied_ = false;
        bool lightsEngineRequested_ = false;
        // The vehicle reports a refused start for the one update the key was pressed in; the HUD
        // keeps the explanation up long enough to be read.
        float startRefusedHintSeconds_ = 0.0f;
    };
}
