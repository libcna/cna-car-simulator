// Sky dome with gradient, sun disc and a cloud layer for the fixed daytime environment.
#pragma once

#include "CarSim/Render/Camera.hpp"
#include "CarSim/Render/GpuMesh.hpp"
#include "CarSim/Render/LightingRig.hpp"

#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <memory>

namespace CarSim::Render
{
    class SkyRenderer
    {
    public:
        SkyRenderer(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const LightingRig& rig);

        /// Draws the sky centred on the camera. Call first in the frame, after Clear.
        void Draw(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const CameraPose& camera, float aspect);
        /// Draws with explicit matrices (mirror pass); `position` centres the dome.
        void Draw(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Microsoft::Xna::Framework::Matrix& view,
                  const Microsoft::Xna::Framework::Matrix& projection, const Microsoft::Xna::Framework::Vector3& position, bool mirrored = false);

    private:
        const LightingRig& rig_;
        std::unique_ptr<GpuMesh> dome_;
        std::unique_ptr<GpuMesh> sun_;
        std::unique_ptr<GpuMesh> clouds_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> cloudTexture_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> sunTexture_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> colorEffect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> textureEffect_;
    };
}
