// Draws the traffic cars: one procedural model per body style variant (hatchback, sedan,
// estate, SUV, van in two size variations each), per-car paint, plate texture, wheel
// animation, brake lights and indicators, distance LODs and shadows for nearby cars.
#pragma once

#include "CarSim/Map/ObjectPlacement.hpp"
#include "CarSim/Render/BitmapFont.hpp"
#include "CarSim/Render/LightingRig.hpp"
#include "CarSim/Render/VehicleRenderer.hpp"
#include "CarSim/Sim/CarStyle.hpp"
#include "CarSim/Traffic/TrafficSystem.hpp"

#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace CarSim::Render
{
    struct TrafficRenderStats
    {
        int drawn = 0;          // moving traffic cars drawn this frame
        int parkedDrawn = 0;    // parked cars of the map drawn this frame
        int lod0 = 0, lod1 = 0, lod2 = 0;
        int drawCalls = 0;
        int triangles = 0;
    };

    class TrafficRenderer
    {
    public:
        static constexpr int kVariantsPerBody = 2;
        /// Number of paint colours in the traffic palette.
        static constexpr int kPaletteSize = 16;

        TrafficRenderer(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, VehicleMaterials& materials, const BitmapFont* plateFont);

        /// Draws all cars inside the frustum (opaque, shadow and glass passes).
        void Draw(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Traffic::TrafficSystem& traffic,
                  const Microsoft::Xna::Framework::Matrix& view, const Microsoft::Xna::Framework::Matrix& projection,
                  const Microsoft::Xna::Framework::BoundingFrustum& frustum, const Microsoft::Xna::Framework::Vector3& cameraPosition,
                  const LightingRig& rig, const GroundQuery& ground, bool mirrored = false);

        /// Draws the parked cars of the map: same models, LOD and shadows, but standing still with
        /// the engine off. `plates` must have one entry per car (empty strings are allowed).
        void DrawParked(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const std::vector<Map::PlacedVehicle>& cars,
                        const std::vector<std::string>& plates, const Microsoft::Xna::Framework::Matrix& view,
                        const Microsoft::Xna::Framework::Matrix& projection, const Microsoft::Xna::Framework::BoundingFrustum& frustum,
                        const Microsoft::Xna::Framework::Vector3& cameraPosition, const LightingRig& rig, const GroundQuery& ground,
                        bool mirrored = false);

        /// Plate texture for a text (cached); also used for the player's plate.
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* PlateTexture(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                                                                                    const std::string& text);

        /// Paint palette: common Czech car colours; vans lean to white and silver.
        [[nodiscard]] static Microsoft::Xna::Framework::Vector3 PaintColour(int paletteIndex, Sim::CarStyle::Body body);
        /// Preset seed of a style variant slot.
        [[nodiscard]] static unsigned VariantSeed(unsigned styleSeed) { return styleSeed % kVariantsPerBody == 0 ? 1u : 5u; }
        [[nodiscard]] int DrawnLastFrame() const { return stats_.drawn; }
        [[nodiscard]] const TrafficRenderStats& Stats() const { return stats_; }
        [[nodiscard]] int ModelCount() const { return static_cast<int>(renderers_.size()); }

        /// LOD radii (metres): full detail, no small parts, reduced.
        float lod1DistanceM = 45.0f;
        float lod2DistanceM = 130.0f;
        float cullDistanceM = 900.0f;
        float shadowDistanceM = 120.0f;
        /// Parked cars are scenery: they drop detail sooner and disappear earlier than traffic.
        float parkedLod1DistanceM = 25.0f;
        float parkedLod2DistanceM = 70.0f;
        float parkedCullDistanceM = 400.0f;
        float parkedShadowDistanceM = 60.0f;

    private:
        [[nodiscard]] Sim::VehicleState StateOf(const Traffic::TrafficVehicle& v, const CarModel& model) const;
        [[nodiscard]] VehicleRenderer& RendererFor(const Traffic::TrafficVehicle& v);
        [[nodiscard]] VehicleRenderer& RendererFor(Sim::CarStyle::Body body, unsigned seed);

        std::array<std::unique_ptr<VehicleRenderer>, Sim::CarStyle::kBodyCount * kVariantsPerBody> renderers_;
        const BitmapFont* plateFont_;
        std::unique_ptr<Image> plateAtlas_;
        std::map<std::string, std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D>> plates_;
        TrafficRenderStats stats_;
    };
}
