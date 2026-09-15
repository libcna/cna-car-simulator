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
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <functional>
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

        /// Re-applies a changed lighting rig (time of day, weather) to every vehicle effect.
        /// `rebuildEnvironment` also regenerates the paint's sky cube map, which is only worth
        /// doing when the sun has moved a noticeable amount.
        void ApplyLighting(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const LightingRig& rig,
                           bool rebuildEnvironment = false);

        Microsoft::Xna::Framework::Graphics::BasicEffect& Lit() { return *lit_; }
        Microsoft::Xna::Framework::Graphics::BasicEffect& LitTextured() { return *litTextured_; }
        Microsoft::Xna::Framework::Graphics::BasicEffect& InteriorLit() { return *interiorLit_; }
        Microsoft::Xna::Framework::Graphics::BasicEffect& Shadow() { return *shadow_; }
        Microsoft::Xna::Framework::Graphics::BasicEffect& Glow() { return *glow_; }
        Microsoft::Xna::Framework::Graphics::Texture2D& GlowTexture() { return *glowTexture_; }
        /// Soft box falloff (alpha) for the contact shadow under a car.
        Microsoft::Xna::Framework::Graphics::Texture2D& ContactTexture() { return *contactTexture_; }
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
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> contactTexture_;
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

    /// Ground surface queries (world x, z) used to drape shadows over roads, kerbs and terrain.
    struct GroundQuery
    {
        std::function<float(float, float)> height;
        std::function<Microsoft::Xna::Framework::Vector3(float, float)> normal;
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
        /// Sun shadow on the ground: the convex hull of the exterior projected along the sun,
        /// drawn once as a fan with a soft rim (no stencil, no overlaps), plus a contact shadow
        /// under the footprint. Vertices are draped on the ground surface. Call after the
        /// opaque world and vehicle.
        void DrawShadow(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Sim::VehicleState& state,
                        const Microsoft::Xna::Framework::Matrix& view, const Microsoft::Xna::Framework::Matrix& projection,
                        const Microsoft::Xna::Framework::Vector3& sunDirection, const GroundQuery& ground);
        /// Warm pool the headlamps throw on the road ahead, draped on the ground and drawn
        /// additively. `intensity` is the rig's lamp factor, so nothing shows in daylight.
        void DrawHeadlightPool(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Sim::VehicleState& state,
                               const Microsoft::Xna::Framework::Matrix& view, const Microsoft::Xna::Framework::Matrix& projection,
                               const GroundQuery& ground, float intensity);
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
        /// Extreme vertices of a rigid group (null part: the body) in that group's frame.
        struct ShadowCaster
        {
            const CarPart* part = nullptr;
            std::vector<Microsoft::Xna::Framework::Vector3> points;
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
        static constexpr int kShadowVertexCapacity = 1536;   // 48 contact + up to 165 hull vertices x 9

        std::vector<GpuPart> parts_;
        std::vector<ShadowCaster> casters_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::VertexBuffer> shadowVertices_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer> shadowIndices_;
        static constexpr int kPoolCellsAlong = 12;
        static constexpr int kPoolCellsAcross = 10;
        static constexpr int kPoolVertexCapacity = kPoolCellsAlong * kPoolCellsAcross * 6;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::VertexBuffer> poolVertices_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer> poolIndices_;
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
