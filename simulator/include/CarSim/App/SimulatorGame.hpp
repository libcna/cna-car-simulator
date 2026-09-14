// The XNA Game subclass that owns the simulator's frame loop.
#pragma once

#include "CarSim/Core/CommandLine.hpp"

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <memory>

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
        void BuildTestScene();
        void FinishFrame();

        Core::CommandLineOptions options_;
        Microsoft::Xna::Framework::GraphicsDeviceManager graphics_;

        // Temporary bootstrap scene (a lit ground plane and a box) used to prove
        // the renderer path end to end; replaced by the world renderer.
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> effect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::VertexBuffer> vertices_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer> indices_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> checker_;
        int primitiveCount_ = 0;
        int vertexCount_ = 0;

        double elapsedSeconds_ = 0.0;
        int framesDrawn_ = 0;
        bool exitRequested_ = false;
    };
}
