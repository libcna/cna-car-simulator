// Renders a loaded map: terrain chunks with a baked macro/lighting texture, road strips with
// kerbs, sidewalks, shoulders and markings, and intersection patches. Everything is generated
// from the map's simulation data so that what is drawn is what the vehicle drives on.
#pragma once

#include "CarSim/Map/MapWorld.hpp"
#include "CarSim/Render/BitmapFont.hpp"
#include "CarSim/Render/GpuMesh.hpp"
#include "CarSim/Render/Image.hpp"
#include "CarSim/Render/LightingRig.hpp"

#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <memory>
#include <vector>

namespace CarSim::Render
{
    struct WorldRenderStats
    {
        int terrainChunksDrawn = 0;
        int terrainChunksTotal = 0;
        int roadBatchesDrawn = 0;
        int roadBatchesTotal = 0;
        int objectBatchesDrawn = 0;
        int objectBatchesTotal = 0;
        int treeBatchesDrawn = 0;
        int treeBatchesTotal = 0;
        int drawCalls = 0;
        int triangles = 0;
    };

    class WorldRenderer
    {
    public:
        /// `signFont` sets the text on road signs (town names, directions); null uses no text.
        /// How wet the road is (0..1). Wet asphalt is darker and picks up the colour of the sky;
        /// call before ApplyLighting, which folds it into the effects.
        void SetWetness(float wetness) { wetness_ = wetness; }
        /// Snow on the ground, roads, roofs and tree crowns (0..1).
        void SetSnow(float cover);
        /// Street lanterns (world positions), for the reflections on a wet road.
        [[nodiscard]] const std::vector<Microsoft::Xna::Framework::Vector3>& Lanterns() const { return lanterns_; }

        /// Keeps the baked ground shadows of buildings and trees under the sun: once the sun has
        /// moved far enough, they are baked again on a worker thread and swapped in over the
        /// next frames. Call once per frame. Only the shadows move; the baked light keeps its
        /// reference so the per-frame scale stays right.
        void UpdateSunShadows(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                              const Microsoft::Xna::Framework::Vector3& sunDirection,
                              float sunElevationDeg);
        /// Direction the current ground shadows were cast along.
        [[nodiscard]] const Microsoft::Xna::Framework::Vector3& ShadowSunDirection() const { return shadowSun_; }
        [[nodiscard]] bool ShadowBakeRunning() const { return bakeJob_ != nullptr; }
        ~WorldRenderer();
        /// Re-applies the current rig to the world effects. The terrain macro, the road vertex
        /// colours and the tree cards carry lighting baked under `LightingRig::BakeReference()`,
        /// so they are scaled by the ratio between the two rigs instead of being re-baked.
        void ApplyLighting();
        /// Positions the player's headlamp fill on nearby vertical scenery for the next draw.
        void SetHeadlights(const Microsoft::Xna::Framework::Vector3& position,
                           const Microsoft::Xna::Framework::Vector3& forward, float intensity, bool highBeam);

        WorldRenderer(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const LightingRig& rig, const Map::MapWorld& world,
                      const BitmapFont* signFont);

        /// `mirrored`: the projection flips x (mirror pass), so front faces are clockwise.
        /// `maxDistance`: caps every draw distance for this pass (0 = the rig's own horizon). The
        /// rear-view mirror uses it: in a strip 200 pixels tall nothing beyond a couple of
        /// hundred metres can be made out, and drawing it costs as much as the main view.
        void Draw(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Microsoft::Xna::Framework::Matrix& view,
                  const Microsoft::Xna::Framework::Matrix& projection, const Microsoft::Xna::Framework::BoundingFrustum& frustum,
                  bool mirrored = false, float maxDistance = 0.0f);

        /// Scales every world draw distance in the main view (the graphics quality tier).
        /// 1 is the distance everything in docs/performance.md was measured at.
        void SetDrawDistanceScale(float scale) { drawDistanceScale_ = scale > 0.05f ? scale : 0.05f; }
        /// Scales the tree draw distance on top of the above: vegetation is the cheapest thing to
        /// pull in and the least missed far away.
        void SetVegetationScale(float scale) { vegetationScale_ = scale > 0.05f ? scale : 0.05f; }

        [[nodiscard]] const WorldRenderStats& Stats() const { return stats_; }

    private:
        enum class Surface
        {
            Asphalt,
            Gravel,
            Paving,
            Concrete,
            Marking,
            Grass,     // road verges (share the terrain's grass texture)
            Cobbles    // paved town squares
        };

        struct Batch
        {
            std::unique_ptr<GpuMesh> mesh;
            Surface surface = Surface::Asphalt;
            /// For repaired asphalt only: unpatched road surface for lying snow, so it is
            /// not blended twice over the repair geometry.
            std::unique_ptr<GpuMesh> snowBase;
            /// Asphalt only: the same surface with its texture coordinates stretched so the
            /// puddle mask repeats every 16 x 16 m instead of every texture tile.
            std::unique_ptr<GpuMesh> puddles;
            /// The mesh before its light and ground shadows were baked in, kept so the shadows
            /// can be baked again when the sun has moved.
            std::shared_ptr<const MeshData> source;
            bool verge = false;   // blends into the terrain macro tint at its outer edge
        };

        struct ObjectBatch
        {
            std::unique_ptr<GpuMesh> mesh;
            Microsoft::Xna::Framework::Graphics::Texture2D* texture = nullptr;
            Microsoft::Xna::Framework::Vector3 diffuse{1.0f, 1.0f, 1.0f};
            Microsoft::Xna::Framework::Vector3 emissive{0.0f, 0.0f, 0.0f};
            /// Added to `emissive` in proportion to the rig's lamp factor: lit windows.
            Microsoft::Xna::Framework::Vector3 nightEmissive{0.0f, 0.0f, 0.0f};
            Microsoft::Xna::Framework::Vector3 specular{0.05f, 0.05f, 0.05f};
            float specularPower = 8.0f;
            float cullDistance = 0.0f;   // > 0: skipped when the batch sphere is farther than this
            bool roof = false;           // takes a layer of snow
            bool treeTrunk = false;      // shares dense-fog chunk culling with foliage cards
        };
        struct TreeBatch
        {
            std::unique_ptr<GpuMesh> mesh;
            Microsoft::Xna::Framework::Graphics::Texture2D* texture = nullptr;
        };

        void BuildObjects(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
        /// Warm pools on the ground under the street lamps and a glow at each lantern, drawn
        /// additively while the lamps are on.
        void BuildLampLights(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
        void DrawLampLights(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                            const Microsoft::Xna::Framework::Matrix& view,
                            const Microsoft::Xna::Framework::Matrix& projection,
                            const Microsoft::Xna::Framework::BoundingFrustum& frustum);
        void DrawHeadlightFill(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                               const Microsoft::Xna::Framework::Matrix& view,
                               const Microsoft::Xna::Framework::Matrix& projection,
                               const Microsoft::Xna::Framework::BoundingFrustum& frustum);
        void BuildSigns(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const BitmapFont* font);
        void BuildTrees(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
        void BuildTerrain(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
        /// Region tint x baked sun light x ground shadows; also returns the tint map used to
        /// blend road verges into the terrain.
        void BuildMacroTexture(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Image& shadow, Image& tintOut);
        void ComputeMacro(const Image& shadow, Image& macro, Image& tintOut) const;
        void BuildRoads(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Image& shadow, const Image& tint);
        void BuildIntersections(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Image& shadow);
        /// Paves every `RegionType::Square` region with cobbles, draped on the terrain and cut
        /// around the roads that cross it.
        void BuildPavedAreas(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Image& shadow);
        /// Multiplies the rig lighting and the ground shadow into the wear colours of a road
        /// mesh; `tint` (verges) blends the outer vertices into the terrain's macro tint.
        void BakeRoadColours(MeshData& mesh, const Image& shadow, const Image* tint) const;
        /// Creates `batch.puddles` from an asphalt mesh whose UVs are in 4 m tiles.
        static void AddPuddleMesh(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const MeshData& asphalt, Batch& batch);
        Microsoft::Xna::Framework::Graphics::Texture2D* TextureFor(Surface s) const;

        const Map::MapWorld& world_;
        const LightingRig& rig_;
        /// Fixed reference sun the terrain macro, the ground shadows and the road vertex colours
        /// are baked under; `rig_` may be at any hour, so the bakes must not follow it.
        LightingRig bakeRig_ = LightingRig::BakeReference();
        Microsoft::Xna::Framework::Vector3 bakedScale_{1.0f, 1.0f, 1.0f};   // baked lighting -> current rig

        std::unique_ptr<Microsoft::Xna::Framework::Graphics::DualTextureEffect> terrainEffect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> roadEffect_;        // lit: buildings, props, trunks
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> roadUnlitEffect_;   // baked vertex colours: roads, verges, markings
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::RasterizerState> markingState_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::RasterizerState> markingStateMirrored_;

        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> grass_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> macro_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> asphalt_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> gravel_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> paving_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> cobbles_;
        std::unique_ptr<GpuMesh> terrainSkirt_;   // flat apron around the map so the ground does not end in mid-air
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> concrete_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> marking_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> white_;

        std::unique_ptr<Microsoft::Xna::Framework::Graphics::AlphaTestEffect> treeEffect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> headlightObjectEffect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::AlphaTestEffect> headlightTreeEffect_;
        Microsoft::Xna::Framework::Vector3 headlightPosition_{};
        Microsoft::Xna::Framework::Vector3 headlightForward_{0.0f, 0.0f, -1.0f};
        float headlightIntensity_ = 0.0f;
        bool headlightHighBeam_ = false;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> glowEffect_;        // unlit, additive: street lamp light
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> glowTexture_;         // radial falloff
        std::vector<std::unique_ptr<GpuMesh>> lampLights_;                                    // one mesh per chunk
        float lampFactor_ = 0.0f;                                                             // 0 by day, 1 after dark
        float wetness_ = 0.0f;                                                                // 0 dry road, 1 soaked
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> roadSheenEffect_;    // wet-road sky sheen
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> puddleEffect_;       // additive sky in standing water
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> puddleMask_;
        float snow_ = 0.0f;
        std::vector<Microsoft::Xna::Framework::Vector3> lanterns_;
        struct ShadowBakeJob;
        std::unique_ptr<ShadowBakeJob> bakeJob_;
        Microsoft::Xna::Framework::Vector3 shadowSun_{0.0f, -1.0f, 0.0f};
        std::size_t swapNext_ = 0;   // next road batch to swap in from a finished bake
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> snowEffect_;       // alpha-blended white over ground, roads, roofs
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> snowTexture_;
        std::vector<std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D>> wallTextures_;
        std::vector<std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D>> roofTextures_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> windowTexture_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> woodTexture_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> barkTexture_;
        std::vector<std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D>> treeCards_;
        std::vector<Image> treeSummerCards_;  // kept for gradual seasonal recolouring
        std::vector<Image> treeWinterCards_;
        float treeAtlasSnow_ = 0.0f;         // last value uploaded to the eight atlases

        struct TerrainChunk
        {
            std::unique_ptr<GpuMesh> lod0;   // full resolution
            std::unique_ptr<GpuMesh> lod1;   // every second vertex
            std::unique_ptr<GpuMesh> lod2;   // every fourth vertex
            std::unique_ptr<GpuMesh> snowLod0; // same triangles, compact BasicEffect layout
            std::unique_ptr<GpuMesh> snowLod1;
            std::unique_ptr<GpuMesh> snowLod2;
            Microsoft::Xna::Framework::Vector3 centre{};
            float radius = 0.0f;
        };
        std::vector<TerrainChunk> terrainChunks_;
        float lod1DistanceM = 420.0f;
        float lod2DistanceM = 1000.0f;
        float terrainCullDistanceM = 2300.0f;
        float drawDistanceScale_ = 1.0f;
        float vegetationScale_ = 1.0f;
        std::vector<Batch> roadBatches_;
        std::vector<ObjectBatch> objectBatches_;
        std::vector<TreeBatch> treeBatches_;
        std::vector<std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D>> signTextures_;
        std::vector<TreeBatch> signBatches_;   // alpha-tested faces (share the tree effect)
        WorldRenderStats stats_;
    };
}
