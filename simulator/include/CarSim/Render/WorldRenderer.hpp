// Renders a loaded map: terrain chunks with a baked macro/lighting texture, road strips with
// kerbs, sidewalks, shoulders and markings, and intersection patches. Everything is generated
// from the map's simulation data so that what is drawn is what the vehicle drives on.
#pragma once

#include "CarSim/Map/MapWorld.hpp"
#include "CarSim/Render/GpuMesh.hpp"
#include "CarSim/Render/LightingRig.hpp"

#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
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
        int drawCalls = 0;
        int triangles = 0;
    };

    class WorldRenderer
    {
    public:
        WorldRenderer(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const LightingRig& rig, const Map::MapWorld& world);

        void Draw(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Microsoft::Xna::Framework::Matrix& view,
                  const Microsoft::Xna::Framework::Matrix& projection, const Microsoft::Xna::Framework::BoundingFrustum& frustum);

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

        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> grass_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> macro_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> asphalt_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> gravel_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> paving_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> concrete_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> white_;

        std::vector<std::unique_ptr<GpuMesh>> terrainChunks_;
        std::vector<Batch> roadBatches_;
        WorldRenderStats stats_;
    };
}
