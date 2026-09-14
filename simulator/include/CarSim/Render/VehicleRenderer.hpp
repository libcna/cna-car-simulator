// Draws a CarModel with stock XNA effects and animates wheels, steering wheel, gear lever, lamps
// and needles. Materials are stock BasicEffect / EnvironmentMapEffect looks with procedural
// textures (paint detail, glass tint, tyre tread, lenses, grille, plastics, fabric).
#pragma once

#include "CarSim/Render/GpuMesh.hpp"
#include "CarSim/Render/LightingRig.hpp"
#include "CarSim/Render/ProceduralCar.hpp"
#include "CarSim/Sim/Vehicle.hpp"

#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace CarSim::Render
{
    /// Shared GPU resources for all vehicles (effects, environment map, material textures).
    class VehicleMaterials
    {
    public:
        VehicleMaterials(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const LightingRig& rig);

        Microsoft::Xna::Framework::Graphics::BasicEffect& Lit() { return *lit_; }
        Microsoft::Xna::Framework::Graphics::BasicEffect& LitTextured() { return *litTextured_; }
        Microsoft::Xna::Framework::Graphics::BasicEffect& InteriorLit() { return *interiorLit_; }
        Microsoft::Xna::Framework::Graphics::BasicEffect& Shadow() { return *shadow_; }
        Microsoft::Xna::Framework::Graphics::BasicEffect& Glow() { return *glow_; }
        Microsoft::Xna::Framework::Graphics::Texture2D& GlowTexture() { return *glowTexture_; }
        Microsoft::Xna::Framework::Graphics::DepthStencilState& ShadowStencil() { return *shadowStencil_; }
        Microsoft::Xna::Framework::Graphics::EnvironmentMapEffect& Paint() { return *paint_; }
        Microsoft::Xna::Framework::Graphics::TextureCube& Environment() { return *environment_; }
        Microsoft::Xna::Framework::Graphics::Texture2D& White() { return *white_; }
        Microsoft::Xna::Framework::Graphics::Texture2D& DefaultPlate() { return *defaultPlate_; }
        Microsoft::Xna::Framework::Graphics::Texture2D& DefaultCluster() { return *defaultCluster_; }
        /// Shared material texture for a slot (null for paint/glass, which are per model).
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* TextureFor(CarMaterial material) const;

    private:
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> lit_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> litTextured_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> interiorLit_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> shadow_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> glow_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> glowTexture_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::DepthStencilState> shadowStencil_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::EnvironmentMapEffect> paint_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> environment_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> white_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> defaultPlate_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> defaultCluster_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> tyre_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> rim_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> headlamp_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> tailLamp_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> grille_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> plastic_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> fabric_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> headliner_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> vent_;
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
        /// Player vehicle: model from the definition with a full cockpit.
        VehicleRenderer(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, VehicleMaterials& materials,
                        const Sim::VehicleDefinition& definition);
        /// Any style (traffic variants), optionally without the cockpit.
        VehicleRenderer(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, VehicleMaterials& materials,
                        const CarStyle& style, const Sim::VehicleDefinition* definition, bool interior);

        /// Sets the textures used for the plate and cluster faces (owned elsewhere).
        void SetPlateTexture(Microsoft::Xna::Framework::Graphics::Texture2D* texture) { plateTexture_ = texture; }
        /// Overrides the paint colour of the model (traffic cars share one model per style).
        void SetPaintOverride(const std::optional<Microsoft::Xna::Framework::Vector3>& paint) { paintOverride_ = paint; }
        void SetClusterTexture(Microsoft::Xna::Framework::Graphics::Texture2D* texture) { clusterTexture_ = texture; }
        void SetMirrorTexture(Microsoft::Xna::Framework::Graphics::Texture2D* texture) { mirrorTexture_ = texture; }

        /// Opaque parts. `drawInterior` selects cockpit-only parts (drawn from inside or in mirrors).
        /// `lod` 0 draws everything, 1 drops small detail parts, 2 also drops lamps and trim.
        void DrawOpaque(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Sim::VehicleState& state,
                        const Microsoft::Xna::Framework::Matrix& view, const Microsoft::Xna::Framework::Matrix& projection,
                        bool drawInterior, const GaugePose& gauges, bool mirrored = false, int lod = 0);
        /// Planar projected shadow of the exterior onto the ground plane under the car (sun light),
        /// stencil-masked so overlapping parts darken once. Call after the opaque world and vehicle.
        void DrawShadow(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Sim::VehicleState& state,
                        const Microsoft::Xna::Framework::Matrix& view, const Microsoft::Xna::Framework::Matrix& projection,
                        const Microsoft::Xna::Framework::Vector3& sunDirection, const Microsoft::Xna::Framework::Vector3& groundPoint,
                        const Microsoft::Xna::Framework::Vector3& groundNormal);
        /// Transparent parts (glass), drawn after all opaque geometry. `fromInside` (cockpit camera)
        /// uses the light tint without reflections; from outside the glass reflects the sky.
        void DrawTransparent(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Sim::VehicleState& state,
                             const Microsoft::Xna::Framework::Matrix& view, const Microsoft::Xna::Framework::Matrix& projection, bool mirrored = false,
                             bool fromInside = false);

        /// Additive glow sprites for the lamps that are lit (after the transparent pass).
        void DrawLampGlows(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Sim::VehicleState& state,
                           const Microsoft::Xna::Framework::Matrix& view, const Microsoft::Xna::Framework::Matrix& projection);

        [[nodiscard]] const CarModel& Model() const { return model_; }
        [[nodiscard]] int DrawCallsLastFrame() const { return drawCalls_; }
        [[nodiscard]] int TriangleCount() const { return triangles_; }

    private:
        struct GpuPart
        {
            const CarPart* part = nullptr;
            std::unique_ptr<GpuMesh> mesh;
        };

        void Upload(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
        [[nodiscard]] Microsoft::Xna::Framework::Matrix PartWorld(const CarPart& part, const Sim::VehicleState& state,
                                                                  const GaugePose& gauges) const;
        [[nodiscard]] static bool IsInteriorPart(const CarPart& part);
        void DrawPart(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const GpuPart& gpu,
                      const Sim::VehicleState& state, const Microsoft::Xna::Framework::Matrix& view,
                      const Microsoft::Xna::Framework::Matrix& projection, const GaugePose& gauges);

        VehicleMaterials& materials_;
        Microsoft::Xna::Framework::Vector3 paintColor_;
        Microsoft::Xna::Framework::Vector3 interiorColor_;
        CarModel model_;
        std::vector<GpuPart> parts_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> paintDetail_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> glassOutside_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> glassInside_;
        std::unique_ptr<GpuMesh> glowQuad_;
        bool glassFromInside_ = false;
        Microsoft::Xna::Framework::Graphics::Texture2D* plateTexture_ = nullptr;
        std::optional<Microsoft::Xna::Framework::Vector3> paintOverride_;
        Microsoft::Xna::Framework::Graphics::Texture2D* clusterTexture_ = nullptr;
        Microsoft::Xna::Framework::Graphics::Texture2D* mirrorTexture_ = nullptr;
        int drawCalls_ = 0;
        int triangles_ = 0;
    };
}
