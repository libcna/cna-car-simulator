// Renders a loaded map: terrain chunks with a baked macro/lighting texture, road strips with
// kerbs, sidewalks, shoulders and markings, and intersection patches. Everything is generated
// from the map's simulation data so that what is drawn is what the vehicle drives on.
#pragma once

#include "CarSim/Map/MapWorld.hpp"
#include "CarSim/Render/BitmapFont.hpp"
#include "CarSim/Render/GpuMesh.hpp"
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
        WorldRenderer(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const LightingRig& rig, const Map::MapWorld& world,
                      const BitmapFont* signFont);

        /// `mirrored`: the projection flips x (mirror pass), so front faces are clockwise.
        void Draw(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Microsoft::Xna::Framework::Matrix& view,
                  const Microsoft::Xna::Framework::Matrix& projection, const Microsoft::Xna::Framework::BoundingFrustum& frustum,
                  bool mirrored = false);

        [[nodiscard]] const WorldRenderStats& Stats() const { return stats_; }

    private:
        enum class Surface
        {
            Asphalt,
            Gravel,
            Paving,
            Concrete,
            Marking
        };

        struct Batch
        {
            std::unique_ptr<GpuMesh> mesh;
            Surface surface = Surface::Asphalt;
        };

        struct ObjectBatch
        {
            std::unique_ptr<GpuMesh> mesh;
            Microsoft::Xna::Framework::Graphics::Texture2D* texture = nullptr;
            Microsoft::Xna::Framework::Vector3 diffuse{1.0f, 1.0f, 1.0f};
            Microsoft::Xna::Framework::Vector3 emissive{0.0f, 0.0f, 0.0f};
            Microsoft::Xna::Framework::Vector3 specular{0.05f, 0.05f, 0.05f};
            float specularPower = 8.0f;
        };
        struct TreeBatch
        {
            std::unique_ptr<GpuMesh> mesh;
            Microsoft::Xna::Framework::Graphics::Texture2D* texture = nullptr;
        };

        void BuildObjects(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
        void BuildSigns(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const BitmapFont* font);
        void BuildTrees(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
        void BuildTerrain(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
        void BuildMacroTexture(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
        void BuildRoads(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
        void BuildIntersections(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
        Microsoft::Xna::Framework::Graphics::Texture2D* TextureFor(Surface s) const;

        const Map::MapWorld& world_;
        const LightingRig& rig_;

        std::unique_ptr<Microsoft::Xna::Framework::Graphics::DualTextureEffect> terrainEffect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> roadEffect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::RasterizerState> markingState_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::RasterizerState> markingStateMirrored_;

        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> grass_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> macro_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> asphalt_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> gravel_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> paving_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> concrete_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> white_;

        std::unique_ptr<Microsoft::Xna::Framework::Graphics::AlphaTestEffect> treeEffect_;
        std::vector<std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D>> wallTextures_;
        std::vector<std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D>> roofTextures_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> windowTexture_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> woodTexture_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> barkTexture_;
        std::vector<std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D>> treeCards_;

        struct TerrainChunk
        {
            std::unique_ptr<GpuMesh> lod0;   // full resolution
            std::unique_ptr<GpuMesh> lod1;   // every second vertex
            std::unique_ptr<GpuMesh> lod2;   // every fourth vertex
            Microsoft::Xna::Framework::Vector3 centre{};
            float radius = 0.0f;
        };
        std::vector<TerrainChunk> terrainChunks_;
        float lod1DistanceM = 420.0f;
        float lod2DistanceM = 1000.0f;
        float terrainCullDistanceM = 2300.0f;
        std::vector<Batch> roadBatches_;
        std::vector<ObjectBatch> objectBatches_;
        std::vector<TreeBatch> treeBatches_;
        std::vector<std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D>> signTextures_;
        std::vector<TreeBatch> signBatches_;   // alpha-tested faces (share the tree effect)
        WorldRenderStats stats_;
    };
}
