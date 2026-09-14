// The XNA Game subclass that owns the simulator's frame loop.
#pragma once

#include "CarSim/Audio/VehicleAudio.hpp"
#include "CarSim/Collision/CollisionWorld.hpp"
#include "CarSim/Core/CommandLine.hpp"
#include "CarSim/Core/SaveData.hpp"
#include "CarSim/Input/InputMapper.hpp"
#include "CarSim/Render/BitmapFont.hpp"
#include "CarSim/Render/Camera.hpp"
#include "CarSim/Render/InstrumentCluster.hpp"
#include "CarSim/Render/MirrorView.hpp"
#include "CarSim/Render/LightingRig.hpp"
#include "CarSim/Render/SkyRenderer.hpp"
#include "CarSim/Map/MapWorld.hpp"
#include "CarSim/Render/TestGround.hpp"
#include "CarSim/Render/TrafficRenderer.hpp"
#include "CarSim/Render/WorldRenderer.hpp"
#include "CarSim/Render/VehicleRenderer.hpp"
#include "CarSim/Sim/Ground.hpp"
#include "CarSim/Sim/Vehicle.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"
#include "CarSim/Traffic/TrafficSystem.hpp"

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"

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
        void ApplyAutoDrive(Sim::DriverControls& controls);
        void UpdateTraffic(float dt);
        [[nodiscard]] Traffic::PlayerProbe PlayerProbe() const;
        void DrawHud();
        void DrawHelp();
        void FinishFrame();

        Core::CommandLineOptions options_;
        Core::SaveData save_;
        std::string savePath_;
        bool saveReadOnly_ = false;
        bool saveEnabled_ = true;
        double saveTimer_ = 0.0;
        bool hudVisible_ = true;
        bool mirrorEnabled_ = true;
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

        // Rendering
        Render::LightingRig rig_;
        std::unique_ptr<Render::SkyRenderer> sky_;
        std::unique_ptr<Render::TestGround> testGround_;
        std::unique_ptr<Render::WorldRenderer> worldRenderer_;
        std::unique_ptr<Render::TrafficRenderer> trafficRenderer_;
        std::unique_ptr<Render::BitmapFont> plateFont_;
        Microsoft::Xna::Framework::Graphics::Texture2D* playerPlate_ = nullptr;
        std::unique_ptr<Render::VehicleMaterials> vehicleMaterials_;
        std::unique_ptr<Render::VehicleRenderer> vehicleRenderer_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
        std::unique_ptr<Render::BitmapFont> font_;
        std::unique_ptr<Render::BitmapFont> fontBold_;
        std::unique_ptr<Render::BitmapFont> gaugeFont_;
        std::unique_ptr<Render::InstrumentCluster> cluster_;
        std::unique_ptr<Render::MirrorView> mirror_;
        Render::ChaseCamera chaseCamera_;
        Render::CockpitCamera cockpitCamera_;
        Render::CameraMode cameraMode_ = Render::CameraMode::Chase;

        bool showHelp_ = false;
        bool showDebug_ = false;
        double elapsedSeconds_ = 0.0;
        float frameMs_ = 0.0f;
        float drawMs_ = 0.0f;
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
            long long trafficCount = 0, trafficDrawn = 0, trafficLod0 = 0, trafficLod1 = 0, trafficLod2 = 0;
            double passSum[kPassCount] = {};
            int warmupFrames = 30;
        } bench_;
        std::chrono::steady_clock::time_point lastFrameEnd_{};
        int framesDrawn_ = 0;
        int updatesSinceDraw_ = 0;
        bool exitRequested_ = false;
        bool screenshotRequested_ = false;
        bool autoDriveStarted_ = false;
        bool lightsApplied_ = false;
    };
}
