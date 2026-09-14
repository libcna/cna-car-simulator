// The XNA Game subclass that owns the simulator's frame loop.
#pragma once

#include "CarSim/Core/CommandLine.hpp"
#include "CarSim/Input/InputMapper.hpp"
#include "CarSim/Render/BitmapFont.hpp"
#include "CarSim/Render/Camera.hpp"
#include "CarSim/Render/LightingRig.hpp"
#include "CarSim/Render/SkyRenderer.hpp"
#include "CarSim/Map/MapWorld.hpp"
#include "CarSim/Render/TestGround.hpp"
#include "CarSim/Render/WorldRenderer.hpp"
#include "CarSim/Render/VehicleRenderer.hpp"
#include "CarSim/Sim/Ground.hpp"
#include "CarSim/Sim/Vehicle.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"

#include <memory>
#include <string>

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

    private:
        void ResolveContentRoot();
        void LoadMap();
        void LoadVehicle();
        void HandleAppActions();
        void ApplyAutoDrive(Sim::DriverControls& controls);
        void DrawHud();
        void DrawHelp();
        void FinishFrame();

        Core::CommandLineOptions options_;
        Microsoft::Xna::Framework::GraphicsDeviceManager graphics_;
        std::string contentRoot_;

        // Simulation
        Sim::VehicleDefinition definition_;
        std::unique_ptr<Sim::Vehicle> vehicle_;
        Sim::FlatGround ground_{0.0f};              // fallback when no map is loaded
        std::unique_ptr<Map::MapWorld> map_;
        Input::InputMapper input_;

        // Rendering
        Render::LightingRig rig_;
        std::unique_ptr<Render::SkyRenderer> sky_;
        std::unique_ptr<Render::TestGround> testGround_;
        std::unique_ptr<Render::WorldRenderer> worldRenderer_;
        std::unique_ptr<Render::VehicleMaterials> vehicleMaterials_;
        std::unique_ptr<Render::VehicleRenderer> vehicleRenderer_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
        std::unique_ptr<Render::BitmapFont> font_;
        std::unique_ptr<Render::BitmapFont> fontBold_;
        Render::ChaseCamera chaseCamera_;
        Render::CockpitCamera cockpitCamera_;
        Render::CameraMode cameraMode_ = Render::CameraMode::Chase;

        bool showHelp_ = false;
        bool showDebug_ = false;
        double elapsedSeconds_ = 0.0;
        float frameMs_ = 0.0f;
        int framesDrawn_ = 0;
        bool exitRequested_ = false;
        bool screenshotRequested_ = false;
        bool autoDriveStarted_ = false;
    };
}
