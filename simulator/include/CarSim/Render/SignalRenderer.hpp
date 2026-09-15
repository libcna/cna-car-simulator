// Traffic signal lenses. The masts and housings are ordinary props drawn with the world; this
// draws the three lenses of each head, dark by default and bright for whichever aspect the
// controller is showing, plus a small additive glow so a green light reads at a distance.
#pragma once

#include "CarSim/Map/MapWorld.hpp"
#include "CarSim/Render/GpuMesh.hpp"
#include "CarSim/Render/LightingRig.hpp"
#include "CarSim/Traffic/SignalController.hpp"

#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace CarSim::Render
{
    class SignalRenderer
    {
    public:
        SignalRenderer(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Map::MapWorld& world);

        /// `aspectOf(intersection, group)` answers what each head is showing.
        void Draw(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Microsoft::Xna::Framework::Matrix& view,
                  const Microsoft::Xna::Framework::Matrix& projection, const Microsoft::Xna::Framework::BoundingFrustum& frustum,
                  const Microsoft::Xna::Framework::Vector3& cameraPosition, const LightingRig& rig,
                  const std::function<Traffic::SignalAspect(int, int)>& aspectOf);

        [[nodiscard]] int HeadCount() const { return static_cast<int>(heads_.size()); }
        [[nodiscard]] int DrawCallsLastFrame() const { return drawCalls_; }

        /// Which lenses a given aspect lights: index 0 red, 1 amber, 2 green.
        [[nodiscard]] static bool LensLit(Traffic::SignalAspect aspect, int lens);

    private:
        struct Head
        {
            int intersection = -1;
            int group = -1;
            Microsoft::Xna::Framework::Vector3 centre{};
            std::unique_ptr<GpuMesh> lens[3];   // red, amber, green
            std::unique_ptr<GpuMesh> glow[3];
        };

        std::vector<Head> heads_;
        std::unique_ptr<GpuMesh> darkLenses_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> effect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> lensTexture_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> glowTexture_;
        int drawCalls_ = 0;
    };
}
