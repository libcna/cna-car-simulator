// Draws the people on the pavements: a simple figure (legs, torso, arms, head) in everyday
// clothes, legs and arms swinging with the walk.
#pragma once

#include "CarSim/Render/GpuMesh.hpp"
#include "CarSim/Render/LightingRig.hpp"
#include "CarSim/Traffic/Pedestrians.hpp"

#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <memory>

namespace CarSim::Render
{
    class PedestrianRenderer
    {
    public:
        explicit PedestrianRenderer(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        void Draw(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const std::vector<Traffic::Pedestrian>& people,
                  const Microsoft::Xna::Framework::Matrix& view, const Microsoft::Xna::Framework::Matrix& projection,
                  const Microsoft::Xna::Framework::BoundingFrustum& frustum, const Microsoft::Xna::Framework::Vector3& camera,
                  const LightingRig& rig, bool mirrored = false);

        [[nodiscard]] int DrawnLastFrame() const { return drawn_; }
        static constexpr float kRangeM = 160.0f;

    private:
        std::unique_ptr<GpuMesh> torso_, head_, leg_, arm_, shoe_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> effect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> white_;
        int drawn_ = 0;
    };
}
