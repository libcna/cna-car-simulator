// Temporary flat proving ground (textured grass plane with an asphalt strip and some boxes),
// used until the map renderer lands. Also serves as the visual regression scene.
#pragma once

#include "CarSim/Render/GpuMesh.hpp"
#include "CarSim/Render/LightingRig.hpp"

#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <memory>
#include <vector>

namespace CarSim::Render
{
    class TestGround
    {
    public:
        TestGround(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const LightingRig& rig);
        void Draw(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                  const Microsoft::Xna::Framework::Matrix& view, const Microsoft::Xna::Framework::Matrix& projection);

    private:
        struct Batch
        {
            std::unique_ptr<GpuMesh> mesh;
            Microsoft::Xna::Framework::Graphics::Texture2D* texture = nullptr;
        };
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> effect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> grass_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> asphalt_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> plaster_;
        std::vector<Batch> batches_;
    };
}
