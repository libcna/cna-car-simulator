// Draws a CarModel with stock XNA effects and animates wheels, steering wheel, lamps and needles.
#pragma once

#include "CarSim/Render/GpuMesh.hpp"
#include "CarSim/Render/LightingRig.hpp"
#include "CarSim/Render/ProceduralCar.hpp"
#include "CarSim/Sim/Vehicle.hpp"

#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <memory>
#include <vector>

namespace CarSim::Render
{
    /// Shared GPU resources for all vehicles (effects, environment map, default textures).
    class VehicleMaterials
    {
    public:
        VehicleMaterials(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const LightingRig& rig);

        Microsoft::Xna::Framework::Graphics::BasicEffect& Lit() { return *lit_; }
        Microsoft::Xna::Framework::Graphics::BasicEffect& LitTextured() { return *litTextured_; }
        Microsoft::Xna::Framework::Graphics::BasicEffect& InteriorLit() { return *interiorLit_; }
        Microsoft::Xna::Framework::Graphics::EnvironmentMapEffect& Paint() { return *paint_; }
        Microsoft::Xna::Framework::Graphics::TextureCube& Environment() { return *environment_; }
        Microsoft::Xna::Framework::Graphics::Texture2D& White() { return *white_; }
        Microsoft::Xna::Framework::Graphics::Texture2D& DefaultPlate() { return *defaultPlate_; }
        Microsoft::Xna::Framework::Graphics::Texture2D& DefaultCluster() { return *defaultCluster_; }

    private:
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> lit_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> litTextured_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> interiorLit_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::EnvironmentMapEffect> paint_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> environment_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> white_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> defaultPlate_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> defaultCluster_;
    };

    /// Gauge values used to pose the needles (fractions 0..1 of the dial sweep).
    struct GaugePose
    {
        float speed = 0.0f;
        float rpm = 0.0f;
        float fuel = 0.0f;
        float temperature = 0.0f;
    };

    class VehicleRenderer
    {
    public:
        VehicleRenderer(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, VehicleMaterials& materials,
                        const Sim::VehicleDefinition& definition);

        /// Sets the textures used for the plate and cluster faces (owned elsewhere).
        void SetPlateTexture(Microsoft::Xna::Framework::Graphics::Texture2D* texture) { plateTexture_ = texture; }
        void SetClusterTexture(Microsoft::Xna::Framework::Graphics::Texture2D* texture) { clusterTexture_ = texture; }
        void SetMirrorTexture(Microsoft::Xna::Framework::Graphics::Texture2D* texture) { mirrorTexture_ = texture; }

        /// Opaque parts. `drawInterior` selects cockpit-only parts (drawn from inside or in mirrors).
        void DrawOpaque(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Sim::VehicleState& state,
                        const Microsoft::Xna::Framework::Matrix& view, const Microsoft::Xna::Framework::Matrix& projection,
                        bool drawInterior, const GaugePose& gauges);
        /// Transparent parts (glass), drawn after all opaque geometry.
        void DrawTransparent(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Sim::VehicleState& state,
                             const Microsoft::Xna::Framework::Matrix& view, const Microsoft::Xna::Framework::Matrix& projection);

        [[nodiscard]] const CarModel& Model() const { return model_; }
        [[nodiscard]] int DrawCallsLastFrame() const { return drawCalls_; }

    private:
        struct GpuPart
        {
            const CarPart* part = nullptr;
            std::unique_ptr<GpuMesh> mesh;
        };

        [[nodiscard]] Microsoft::Xna::Framework::Matrix PartWorld(const CarPart& part, const Sim::VehicleState& state,
                                                                  const GaugePose& gauges) const;
        [[nodiscard]] static bool IsInteriorPart(const CarPart& part);
        void DrawPart(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const GpuPart& gpu,
                      const Sim::VehicleState& state, const Microsoft::Xna::Framework::Matrix& view,
                      const Microsoft::Xna::Framework::Matrix& projection, const GaugePose& gauges);

        VehicleMaterials& materials_;
        const Sim::VehicleDefinition& definition_;
        CarModel model_;
        std::vector<GpuPart> parts_;
        Microsoft::Xna::Framework::Graphics::Texture2D* plateTexture_ = nullptr;
        Microsoft::Xna::Framework::Graphics::Texture2D* clusterTexture_ = nullptr;
        Microsoft::Xna::Framework::Graphics::Texture2D* mirrorTexture_ = nullptr;
        int drawCalls_ = 0;
    };
}
