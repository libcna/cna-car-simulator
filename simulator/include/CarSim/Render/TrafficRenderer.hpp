// Draws the traffic cars with the shared procedural car model: per-car paint, plate texture,
// wheel animation, brake lights and indicators; shadows for nearby cars.
#pragma once

#include "CarSim/Render/BitmapFont.hpp"
#include "CarSim/Render/LightingRig.hpp"
#include "CarSim/Render/VehicleRenderer.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"
#include "CarSim/Traffic/TrafficSystem.hpp"

#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <map>
#include <memory>
#include <string>

namespace CarSim::Render
{
    class TrafficRenderer
    {
    public:
        TrafficRenderer(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, VehicleRenderer& renderer, const Sim::VehicleDefinition& definition,
                        const BitmapFont* plateFont);

        /// Draws all cars inside the frustum (opaque, shadow and glass passes).
        void Draw(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Traffic::TrafficSystem& traffic,
                  const Microsoft::Xna::Framework::Matrix& view, const Microsoft::Xna::Framework::Matrix& projection,
                  const Microsoft::Xna::Framework::BoundingFrustum& frustum, const Microsoft::Xna::Framework::Vector3& cameraPosition,
                  const LightingRig& rig, const std::function<Microsoft::Xna::Framework::Vector3(const Microsoft::Xna::Framework::Vector3&)>& groundNormal,
                  bool mirrored = false);

        /// Plate texture for a text (cached); also used for the player's plate.
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* PlateTexture(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                                                                                    const std::string& text);

        [[nodiscard]] static Microsoft::Xna::Framework::Vector3 PaintColour(int paletteIndex);
        [[nodiscard]] int DrawnLastFrame() const { return drawn_; }

    private:
        [[nodiscard]] Sim::VehicleState StateOf(const Traffic::TrafficVehicle& v) const;

        VehicleRenderer& renderer_;
        const Sim::VehicleDefinition& definition_;
        const BitmapFont* plateFont_;
        std::unique_ptr<Image> plateAtlas_;
        std::map<std::string, std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D>> plates_;
        int drawn_ = 0;
    };
}
