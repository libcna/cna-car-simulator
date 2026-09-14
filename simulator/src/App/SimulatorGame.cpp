#include "CarSim/App/SimulatorGame.hpp"

#include "CarSim/Core/Version.hpp"
#include "CarSim/Render/Screenshot.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include "System/TimeSpan.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

namespace CarSim::App
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    using namespace Microsoft::Xna::Framework::Input;

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

        // The simulation runs on a fixed 60 Hz step; rendering follows the loop.
        setIsFixedTimeStepProperty(true);
        setTargetElapsedTimeProperty(System::TimeSpan::FromSeconds(1.0 / 60.0));
        setIsMouseVisibleProperty(true);

        getWindowProperty().setTitleProperty(Core::ProductName() + " " + Core::VersionString());
    }

    SimulatorGame::~SimulatorGame() = default;

    void SimulatorGame::Initialize()
    {
        Game::Initialize();
    }

    void SimulatorGame::LoadContent()
    {
        BuildTestScene();
    }

    void SimulatorGame::UnloadContent()
    {
        indices_.reset();
        vertices_.reset();
        checker_.reset();
        effect_.reset();
    }

    void SimulatorGame::BuildTestScene()
    {
        auto& device = getGraphicsDeviceProperty();

        // A 16x16 checker texture so texturing and sampling are visibly exercised.
        constexpr int kTextureSize = 64;
        std::vector<Color> pixels(static_cast<std::size_t>(kTextureSize) * kTextureSize);
        for (int y = 0; y < kTextureSize; ++y) {
            for (int x = 0; x < kTextureSize; ++x) {
                const bool light = ((x / 8) + (y / 8)) % 2 == 0;
                pixels[static_cast<std::size_t>(y) * kTextureSize + static_cast<std::size_t>(x)] =
                    light ? Color(170, 170, 165, 255) : Color(90, 92, 90, 255);
            }
        }
        checker_ = std::make_unique<Texture2D>(device, kTextureSize, kTextureSize);
        checker_->SetData(pixels.data(), static_cast<int>(pixels.size()));

        // Ground plane (200 m square, tiled) plus a 4.2 x 1.5 x 1.8 m box standing in for a car.
        std::vector<VertexPositionNormalTexture> vertices;
        std::vector<std::uint16_t> indices;

        const auto addQuad = [&](const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d,
                                 const Vector3& normal, float uvScale) {
            const auto base = static_cast<std::uint16_t>(vertices.size());
            vertices.emplace_back(a, normal, Vector2(0.0f, 0.0f));
            vertices.emplace_back(b, normal, Vector2(uvScale, 0.0f));
            vertices.emplace_back(c, normal, Vector2(uvScale, uvScale));
            vertices.emplace_back(d, normal, Vector2(0.0f, uvScale));
            const std::uint16_t quad[6] = {base, static_cast<std::uint16_t>(base + 1), static_cast<std::uint16_t>(base + 2),
                                           base, static_cast<std::uint16_t>(base + 2), static_cast<std::uint16_t>(base + 3)};
            indices.insert(indices.end(), quad, quad + 6);
        };

        constexpr float kHalf = 100.0f;
        addQuad(Vector3(-kHalf, 0.0f, -kHalf), Vector3(kHalf, 0.0f, -kHalf),
                Vector3(kHalf, 0.0f, kHalf), Vector3(-kHalf, 0.0f, kHalf), Vector3::Up, 50.0f);

        const Vector3 boxMin(-2.1f, 0.0f, -0.9f);
        const Vector3 boxMax(2.1f, 1.5f, 0.9f);
        // +Y (top)
        addQuad(Vector3(boxMin.X, boxMax.Y, boxMax.Z), Vector3(boxMax.X, boxMax.Y, boxMax.Z),
                Vector3(boxMax.X, boxMax.Y, boxMin.Z), Vector3(boxMin.X, boxMax.Y, boxMin.Z), Vector3::Up, 1.0f);
        // +Z (front)
        addQuad(Vector3(boxMin.X, boxMin.Y, boxMax.Z), Vector3(boxMax.X, boxMin.Y, boxMax.Z),
                Vector3(boxMax.X, boxMax.Y, boxMax.Z), Vector3(boxMin.X, boxMax.Y, boxMax.Z), Vector3(0.0f, 0.0f, 1.0f), 1.0f);
        // -Z (back)
        addQuad(Vector3(boxMax.X, boxMin.Y, boxMin.Z), Vector3(boxMin.X, boxMin.Y, boxMin.Z),
                Vector3(boxMin.X, boxMax.Y, boxMin.Z), Vector3(boxMax.X, boxMax.Y, boxMin.Z), Vector3(0.0f, 0.0f, -1.0f), 1.0f);
        // +X (right)
        addQuad(Vector3(boxMax.X, boxMin.Y, boxMax.Z), Vector3(boxMax.X, boxMin.Y, boxMin.Z),
                Vector3(boxMax.X, boxMax.Y, boxMin.Z), Vector3(boxMax.X, boxMax.Y, boxMax.Z), Vector3::Right, 1.0f);
        // -X (left)
        addQuad(Vector3(boxMin.X, boxMin.Y, boxMin.Z), Vector3(boxMin.X, boxMin.Y, boxMax.Z),
                Vector3(boxMin.X, boxMax.Y, boxMax.Z), Vector3(boxMin.X, boxMax.Y, boxMin.Z), Vector3(-1.0f, 0.0f, 0.0f), 1.0f);

        vertexCount_ = static_cast<int>(vertices.size());
        primitiveCount_ = static_cast<int>(indices.size() / 3);

        vertices_ = std::make_unique<VertexBuffer>(device, VertexPositionNormalTexture::getVertexDeclarationStatic(),
                                                   vertexCount_, BufferUsage::WriteOnly);
        vertices_->SetData(vertices.data(), vertexCount_);
        indices_ = std::make_unique<IndexBuffer>(device, IndexElementSize::SixteenBits,
                                                 static_cast<int>(indices.size()), BufferUsage::WriteOnly);
        indices_->SetData(indices.data(), static_cast<int>(indices.size()));

        effect_ = std::make_unique<BasicEffect>(device);
        effect_->EnableDefaultLighting();
        effect_->setPreferPerPixelLightingProperty(true);
        effect_->setTextureEnabledProperty(true);
        effect_->setTextureProperty(checker_.get());
        effect_->setSpecularColorProperty(Vector3(0.15f, 0.15f, 0.15f));
        effect_->setSpecularPowerProperty(32.0f);
        effect_->setAmbientLightColorProperty(Vector3(0.35f, 0.38f, 0.42f));
        effect_->getDirectionalLight0Property().setDirectionProperty(Vector3(-0.45f, -0.80f, -0.35f));
        effect_->getDirectionalLight0Property().setDiffuseColorProperty(Vector3(1.0f, 0.96f, 0.88f));
    }

    void SimulatorGame::Update(GameTime& gameTime)
    {
        if (exitRequested_) {
            return;
        }
        if (Keyboard::GetState().IsKeyDown(Keys::Escape)) {
            exitRequested_ = true;
            Exit();
            return;
        }
        elapsedSeconds_ += gameTime.getElapsedGameTimeProperty().getTotalSecondsProperty();
        Game::Update(gameTime);
    }

    void SimulatorGame::Draw(const GameTime& gameTime)
    {
        auto& device = getGraphicsDeviceProperty();
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                     Color(130, 170, 220, 255), 1.0f, 0);

        const float aspect = static_cast<float>(options_.width) / static_cast<float>(options_.height);
        const auto angle = static_cast<float>(elapsedSeconds_ * 0.25);
        const Vector3 eye(std::cos(angle) * 9.0f, 3.5f, std::sin(angle) * 9.0f);
        const Matrix view = Matrix::CreateLookAt(eye, Vector3(0.0f, 0.75f, 0.0f), Vector3::Up);
        const Matrix projection = Matrix::CreatePerspectiveFieldOfView(MathHelper::ToRadians(60.0f), aspect, 0.1f, 500.0f);

        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;

        effect_->setWorldProperty(Matrix::getIdentityProperty());
        effect_->setViewProperty(view);
        effect_->setProjectionProperty(projection);

        device.SetVertexBuffer(vertices_.get());
        device.setIndicesProperty(indices_.get());

        auto& passes = effect_->getCurrentTechniqueProperty()->getPassesProperty();
        for (int i = 0; i < passes.getCountProperty(); ++i) {
            passes[i]->Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, vertexCount_, 0, primitiveCount_);
        }

        Game::Draw(gameTime);
        FinishFrame();
    }

    void SimulatorGame::FinishFrame()
    {
        ++framesDrawn_;
        if (!options_.frames || framesDrawn_ < *options_.frames || exitRequested_) {
            return;
        }
        if (options_.screenshotPath) {
            if (Render::SaveBackBufferPng(getGraphicsDeviceProperty(), *options_.screenshotPath)) {
                std::cout << "screenshot saved to " << *options_.screenshotPath << "\n";
            }
        }
        std::cout << "frame limit reached (" << framesDrawn_ << " frames); exiting\n";
        exitRequested_ = true;
        Exit();
    }
}
