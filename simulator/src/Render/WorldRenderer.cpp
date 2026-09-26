#include "CarSim/Render/WorldRenderer.hpp"

#include "CarSim/Render/GroundShadowBaker.hpp"

#include "CarSim/Render/Image.hpp"
#include "CarSim/Render/ProceduralTextures.hpp"

#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"

#include <algorithm>
#include <atomic>
#include <iostream>
#include <chrono>
#include <thread>
#include <cmath>

namespace CarSim::Render
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    namespace
    {
        void ApplyAll(Effect& effect, GraphicsDevice& device, const GpuMesh& mesh)
        {
            auto& passes = effect.getCurrentTechniqueProperty()->getPassesProperty();
            for (int i = 0; i < passes.getCountProperty(); ++i) {
                passes[i]->Apply();
                mesh.Draw(device);
            }
        }

    }

    WorldRenderer::WorldRenderer(GraphicsDevice& device, const LightingRig& rig, const Map::MapWorld& world, const BitmapFont* signFont)
        : world_(world), rig_(rig)
    {
        grass_ = UploadTexture(device, Textures::Grass(512, 1u), true);
        asphalt_ = UploadTexture(device, Textures::Asphalt(512, 2u), true);
        gravel_ = UploadTexture(device, Textures::Gravel(256, 4u), true);
        paving_ = UploadTexture(device, Textures::PavingSlabs(256, 5u), true);
        cobbles_ = UploadTexture(device, Textures::Cobbles(256, 9u), true);
        concrete_ = UploadTexture(device, Textures::Plaster(128, Rgb::FromBytes(176, 174, 168), 6u), true);
        marking_ = UploadTexture(device, Textures::MarkingPaint(64, 7u), true);
        white_ = UploadTexture(device, Textures::Solid(4, Color(255, 255, 255, 255)), false);

        // Ground shadows of buildings and trees at two texels per terrain cell (capped), baked
        // into the terrain macro and the road vertex colours.
        const auto& terrain = world_.Terrain();
        const int macroW = std::min(2048, std::max(1, 2 * terrain.Columns()));
        const int macroH = std::min(2048, std::max(1, 2 * terrain.Rows()));
        const Image shadow = GroundShadows::Bake(world_, bakeRig_.sunDirection, macroW, macroH);
        shadowSun_ = bakeRig_.sunDirection;
        Image tint(macroW, macroH);
        BuildMacroTexture(device, shadow, tint);
        BuildTerrain(device);
        BuildRoads(device, shadow, tint);
        BuildIntersections(device, shadow);
        BuildPavedAreas(device, shadow);
        BuildObjects(device);
        BuildLampLights(device);
        BuildTrees(device);
        BuildSigns(device, signFont);

        treeEffect_ = std::make_unique<AlphaTestEffect>(device);
        treeEffect_->setAlphaFunctionProperty(CompareFunction::Greater);
        treeEffect_->setReferenceAlphaProperty(110);
        treeEffect_->setVertexColorEnabledProperty(true);
        treeEffect_->setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        treeEffect_->setFogEnabledProperty(true);
        treeEffect_->setFogColorProperty(rig.fogColor);
        treeEffect_->setFogStartProperty(rig.fogStart);
        treeEffect_->setFogEndProperty(rig.fogEnd);

        headlightObjectEffect_ = std::make_unique<BasicEffect>(device);
        headlightObjectEffect_->setLightingEnabledProperty(true);
        headlightObjectEffect_->setTextureEnabledProperty(true);
        headlightObjectEffect_->setVertexColorEnabledProperty(false);
        headlightObjectEffect_->setAmbientLightColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        headlightObjectEffect_->getDirectionalLight0Property().setEnabledProperty(true);
        headlightObjectEffect_->getDirectionalLight1Property().setEnabledProperty(false);
        headlightObjectEffect_->getDirectionalLight2Property().setEnabledProperty(false);
        headlightObjectEffect_->setFogEnabledProperty(true);
        headlightObjectEffect_->setFogColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        headlightTreeEffect_ = std::make_unique<AlphaTestEffect>(device);
        headlightTreeEffect_->setAlphaFunctionProperty(CompareFunction::Greater);
        headlightTreeEffect_->setReferenceAlphaProperty(110);
        headlightTreeEffect_->setVertexColorEnabledProperty(true);
        headlightTreeEffect_->setFogEnabledProperty(true);
        headlightTreeEffect_->setFogColorProperty(Vector3(0.0f, 0.0f, 0.0f));

        // Wet-road sheen. Wet asphalt is dark under your wheels and a mirror at the far end of
        // the street, because the sky's reflection climbs with the grazing angle -- and down a
        // road, distance is the grazing angle. A BasicEffect's fog is a distance ramp, so a
        // second additive pass over the road with a black diffuse and the sky as the fog colour
        // puts exactly that sheen on it: nothing near, sky far. No renderer internals, no custom
        // shader, and it costs one extra pass over the road batches only while the road is wet.
        puddleMask_ = UploadTexture(device, Textures::PuddleMask(256, 71u), true);
        snowTexture_ = UploadTexture(device, Textures::SnowLayer(256, 29u), true);
        snowEffect_ = std::make_unique<BasicEffect>(device);
        snowEffect_->setLightingEnabledProperty(false);
        snowEffect_->setTextureEnabledProperty(true);
        snowEffect_->setVertexColorEnabledProperty(false);
        snowEffect_->setFogEnabledProperty(true);
        puddleEffect_ = std::make_unique<BasicEffect>(device);
        puddleEffect_->setLightingEnabledProperty(false);
        puddleEffect_->setTextureEnabledProperty(true);
        puddleEffect_->setVertexColorEnabledProperty(false);
        puddleEffect_->setFogEnabledProperty(true);
        puddleEffect_->setFogColorProperty(Vector3(0.0f, 0.0f, 0.0f));   // additive: fog fades it out
        roadSheenEffect_ = std::make_unique<BasicEffect>(device);
        roadSheenEffect_->setLightingEnabledProperty(false);
        roadSheenEffect_->setTextureEnabledProperty(false);
        roadSheenEffect_->setVertexColorEnabledProperty(false);
        roadSheenEffect_->setDiffuseColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        roadSheenEffect_->setFogEnabledProperty(true);

        terrainEffect_ = std::make_unique<DualTextureEffect>(device);
        terrainEffect_->setVertexColorEnabledProperty(false);
        terrainEffect_->setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        terrainEffect_->setFogEnabledProperty(true);
        terrainEffect_->setFogColorProperty(rig.fogColor);
        terrainEffect_->setFogStartProperty(rig.fogStart);
        terrainEffect_->setFogEndProperty(rig.fogEnd);
        terrainEffect_->setTextureProperty(grass_.get());
        terrainEffect_->setTexture2Property(macro_.get());

        roadEffect_ = std::make_unique<BasicEffect>(device);
        rig.Apply(*roadEffect_);
        roadEffect_->setTextureEnabledProperty(true);
        roadEffect_->setVertexColorEnabledProperty(false);
        roadEffect_->setSpecularColorProperty(Vector3(0.06f, 0.06f, 0.06f));
        roadEffect_->setSpecularPowerProperty(10.0f);

        // Roads carry their lighting, wear and ground shadows in the vertex colours.
        roadUnlitEffect_ = std::make_unique<BasicEffect>(device);
        roadUnlitEffect_->setLightingEnabledProperty(false);
        roadUnlitEffect_->setTextureEnabledProperty(true);
        roadUnlitEffect_->setVertexColorEnabledProperty(true);
        roadUnlitEffect_->setFogEnabledProperty(true);
        roadUnlitEffect_->setFogColorProperty(rig.fogColor);
        roadUnlitEffect_->setFogStartProperty(rig.fogStart);
        roadUnlitEffect_->setFogEndProperty(rig.fogEnd);

        markingState_ = std::make_unique<RasterizerState>();
        markingState_->setCullModeProperty(CullMode::CullCounterClockwiseFace);
        markingState_->setDepthBiasProperty(-0.00002f);
        markingState_->setSlopeScaleDepthBiasProperty(-1.0f);
        markingStateMirrored_ = std::make_unique<RasterizerState>();
        markingStateMirrored_->setCullModeProperty(CullMode::CullClockwiseFace);
        markingStateMirrored_->setDepthBiasProperty(-0.00002f);
        markingStateMirrored_->setSlopeScaleDepthBiasProperty(-1.0f);
    }

    void WorldRenderer::ApplyLighting()
    {
        const Vector3 scale = rig_.BakedLightingScale(bakeRig_);
        if (roadEffect_) {
            rig_.Apply(*roadEffect_);
            roadEffect_->setTextureEnabledProperty(true);
            roadEffect_->setVertexColorEnabledProperty(false);
            roadEffect_->setSpecularColorProperty(Vector3(0.06f, 0.06f, 0.06f));
            roadEffect_->setSpecularPowerProperty(10.0f);
        }
        if (terrainEffect_) {
            terrainEffect_->setDiffuseColorProperty(scale);
            terrainEffect_->setFogColorProperty(rig_.fogColor);
            terrainEffect_->setFogStartProperty(rig_.fogStart);
            terrainEffect_->setFogEndProperty(rig_.fogEnd);
        }
        bakedScale_ = scale;
        lampFactor_ = rig_.LampFactor();
        if (roadUnlitEffect_) {
            roadUnlitEffect_->setDiffuseColorProperty(scale);
            roadUnlitEffect_->setFogColorProperty(rig_.fogColor);
            roadUnlitEffect_->setFogStartProperty(rig_.fogStart);
            roadUnlitEffect_->setFogEndProperty(rig_.fogEnd);
        }
        if (treeEffect_) {
            treeEffect_->setDiffuseColorProperty(scale);
            treeEffect_->setFogColorProperty(rig_.fogColor);
            treeEffect_->setFogStartProperty(rig_.fogStart);
            treeEffect_->setFogEndProperty(rig_.fogEnd);
        }
    }

    struct WorldRenderer::ShadowBakeJob
    {
        struct MacroLevel
        {
            int width = 0;
            int height = 0;
            std::vector<std::uint8_t> rgba;
        };
        Vector3 sun;
        std::vector<MacroLevel> macroLevels;
        std::vector<std::vector<Color>> colours;   // per road batch, in batch order
        std::atomic<bool> done{false};
        std::thread worker;
        double seconds = 0.0;
    };

    WorldRenderer::~WorldRenderer()
    {
        if (bakeJob_ && bakeJob_->worker.joinable()) bakeJob_->worker.join();
    }

    void WorldRenderer::UpdateSunShadows(GraphicsDevice& device, const Vector3& sunDirection,
                                         const float sunElevationDeg)
    {
#if defined(__EMSCRIPTEN__)
        // No worker threads in the browser build: the shadows stay as baked at load.
        (void)device;
        (void)sunDirection;
        (void)sunElevationDeg;
        return;
#else
        if (bakeJob_) {
            if (!bakeJob_->done.load()) return;
            if (bakeJob_->worker.joinable()) bakeJob_->worker.join();
            // The worker has prepared the mip chain and packed RGBA bytes. Submit all
            // levels together so the new texture is fully defined before it is bound.
            if (swapNext_ == 0 && !bakeJob_->macroLevels.empty()) {
                const auto& levels = bakeJob_->macroLevels;
                auto nextMacro = std::make_unique<Texture2D>(
                    device, levels[0].width, levels[0].height, true, SurfaceFormat::Color);
                for (std::size_t i = 0; i < levels.size(); ++i) {
                    const auto& level = levels[i];
                    nextMacro->SetData(static_cast<int>(i), nullptr,
                        level.rgba.data(), 0, static_cast<int>(level.rgba.size()));
                }
                macro_ = std::move(nextMacro);
                terrainEffect_->setTexture2Property(macro_.get());
            }
            // Replace the road colours in small batches after the terrain texture.
            constexpr std::size_t kBatchesPerFrame = 24;
            const std::size_t end = std::min(roadBatches_.size(), swapNext_ + kBatchesPerFrame);
            for (; swapNext_ < end; ++swapNext_) {
                Batch& b = roadBatches_[swapNext_];
                if (!b.source || swapNext_ >= bakeJob_->colours.size() || bakeJob_->colours[swapNext_].empty()) continue;
                MeshData m = *b.source;
                const auto& colours = bakeJob_->colours[swapNext_];
                for (std::size_t i = 0; i < m.vertices.size() && i < colours.size(); ++i) m.vertices[i].color = colours[i];
                b.mesh = GpuMesh::Create(device, m, VertexLayout::PositionColorTexture);
            }
            if (swapNext_ >= roadBatches_.size()) {
                std::cout << "shadows: re-baked for the sun at " << std::lround(std::asin(std::clamp(-bakeJob_->sun.Y, -1.0f, 1.0f)) * 57.2958f)
                          << " degrees in " << bakeJob_->seconds << " s\n";
                shadowSun_ = bakeJob_->sun;
                bakeJob_.reset();
                swapNext_ = 0;
            }
            return;
        }
        // LightingRig switches its key direction to the moon at night. Use the actual solar
        // elevation so that moonlight does not trigger a costly new *sun* shadow bake.
        // A bake already running above can still finish and swap in after sunset.
        if (sunElevationDeg < 3.5f) return;
        const Vector3 toSun = -sunDirection;
        if (toSun.Y < 0.06f) return;
        const float turned = std::acos(std::clamp(Vector3::Dot(sunDirection, shadowSun_), -1.0f, 1.0f));
        if (turned < 10.0f * 3.14159265f / 180.0f) return;

        bakeJob_ = std::make_unique<ShadowBakeJob>();
        bakeJob_->sun = sunDirection;
        swapNext_ = 0;
        std::vector<std::pair<std::shared_ptr<const MeshData>, bool>> sources;
        sources.reserve(roadBatches_.size());
        for (const auto& b : roadBatches_) sources.emplace_back(b.source, b.verge);
        ShadowBakeJob* job = bakeJob_.get();
        job->worker = std::thread([this, job, sources = std::move(sources)]() {
            const auto started = std::chrono::steady_clock::now();
            const auto& terrain = world_.Terrain();
            const int macroW = std::min(2048, std::max(1, 2 * terrain.Columns()));
            const int macroH = std::min(2048, std::max(1, 2 * terrain.Rows()));
            const Image shadow = GroundShadows::Bake(world_, job->sun, macroW, macroH);
            Image macro(macroW, macroH);
            Image tint(macroW, macroH);
            ComputeMacro(shadow, macro, tint);
            job->colours.resize(sources.size());
            for (std::size_t i = 0; i < sources.size(); ++i) {
                if (!sources[i].first) continue;
                MeshData m = *sources[i].first;
                BakeRoadColours(m, shadow, sources[i].second ? &tint : nullptr);
                auto& out = job->colours[i];
                out.reserve(m.vertices.size());
                for (const auto& v : m.vertices) out.push_back(v.color);
            }
            // Downsample and pack on the worker. The byte SetData overload can copy
            // each complete level without repacking Color objects on the update thread.
            Image level = std::move(macro);
            for (;;) {
                ShadowBakeJob::MacroLevel packed;
                packed.width = level.Width();
                packed.height = level.Height();
                packed.rgba.reserve(level.Pixels().size() * 4);
                for (const Color& c : level.Pixels()) {
                    packed.rgba.push_back(c.getRProperty());
                    packed.rgba.push_back(c.getGProperty());
                    packed.rgba.push_back(c.getBProperty());
                    packed.rgba.push_back(c.getAProperty());
                }
                job->macroLevels.push_back(std::move(packed));
                if (level.Width() == 1 && level.Height() == 1) break;
                level = level.Downsampled();
            }
            job->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            job->done.store(true);
        });
#endif
    }

    void WorldRenderer::SetHeadlights(const Vector3& position, const Vector3& forward,
                                      const float intensity, const bool highBeam)
    {
        headlightPosition_ = position;
        headlightForward_ = forward;
        headlightForward_.Y = 0.0f;
        if (headlightForward_.LengthSquared() > 1e-6f) headlightForward_.Normalize();
        headlightIntensity_ = std::clamp(intensity, 0.0f, 1.0f);
        headlightHighBeam_ = highBeam;
    }

    void WorldRenderer::DrawLampLights(GraphicsDevice& device, const Matrix& view, const Matrix& projection, const BoundingFrustum& frustum)
    {
        if (lampFactor_ <= 0.01f || !glowEffect_ || lampLights_.empty()) {
            return;
        }
        glowEffect_->setWorldProperty(Matrix::getIdentityProperty());
        glowEffect_->setViewProperty(view);
        glowEffect_->setProjectionProperty(projection);
        glowEffect_->setTextureProperty(glowTexture_.get());
        // Sodium-white lamps. The lantern runs near white; the pool on the ground carries its
        // lower intensity in the vertex colour, since both share this one effect.
        glowEffect_->setDiffuseColorProperty(Vector3(0.95f, 0.84f, 0.60f) * lampFactor_);
        device.setBlendStateProperty(BlendState::Additive);
        device.setDepthStencilStateProperty(DepthStencilState::DepthRead);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
        for (const auto& mesh : lampLights_) {
            if (!mesh || !frustum.Intersects(mesh->Sphere())) continue;
            ApplyAll(*glowEffect_, device, *mesh);
            ++stats_.drawCalls;
            stats_.triangles += mesh->PrimitiveCount();
        }
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
        glowEffect_->setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
    }

    void WorldRenderer::DrawHeadlightFill(GraphicsDevice& device, const Matrix& view, const Matrix& projection,
                                          const BoundingFrustum& frustum)
    {
        if (headlightIntensity_ <= 0.01f) return;
        const float range = headlightHighBeam_ ? 275.0f : 165.0f;
        const auto inBeam = [&](const BoundingSphere& sphere) {
            const Vector3 delta = sphere.Center - headlightPosition_;
            const float along = Vector3::Dot(delta, headlightForward_);
            if (along + sphere.Radius < 0.0f || along - sphere.Radius > range) return false;
            const float sideways = std::sqrt(std::max(0.0f, delta.X * delta.X + delta.Z * delta.Z - along * along));
            return sideways < sphere.Radius + 5.0f + std::max(0.0f, along) * (headlightHighBeam_ ? 0.15f : 0.22f);
        };
        device.setBlendStateProperty(BlendState::Additive);
        device.setDepthStencilStateProperty(DepthStencilState::DepthRead);
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;

        auto& object = *headlightObjectEffect_;
        object.setWorldProperty(Matrix::getIdentityProperty());
        object.setViewProperty(view);
        object.setProjectionProperty(projection);
        object.setFogStartProperty(10.0f);
        object.setFogEndProperty(range);
        object.setSpecularColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        auto& light = object.getDirectionalLight0Property();
        light.setDirectionProperty(headlightForward_);
        light.setDiffuseColorProperty((headlightHighBeam_ ? Vector3(0.72f, 0.70f, 0.63f) :
                                                           Vector3(0.61f, 0.57f, 0.49f)) * headlightIntensity_);
        light.setSpecularColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        for (const auto& batch : objectBatches_) {
            if (!batch.mesh || !frustum.Intersects(batch.mesh->Sphere()) || !inBeam(batch.mesh->Sphere())) continue;
            if (batch.treeTrunk && rig_.fogEnd < 400.0f &&
                Vector3::Distance(batch.mesh->Sphere().Center, headlightPosition_) > rig_.fogEnd + 30.0f) continue;
            object.setTextureProperty(batch.texture);
            object.setDiffuseColorProperty(batch.diffuse);
            ApplyAll(object, device, *batch.mesh);
            ++stats_.drawCalls;
            stats_.triangles += batch.mesh->PrimitiveCount();
        }

        auto& trees = *headlightTreeEffect_;
        trees.setWorldProperty(Matrix::getIdentityProperty());
        trees.setViewProperty(view);
        trees.setProjectionProperty(projection);
        trees.setFogStartProperty(6.0f);
        trees.setFogEndProperty(range);
        trees.setDiffuseColorProperty((headlightHighBeam_ ? Vector3(0.40f, 0.39f, 0.34f) :
                                                           Vector3(0.32f, 0.30f, 0.26f)) * headlightIntensity_);
        device.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
        for (const auto& batch : treeBatches_) {
            if (!batch.mesh || !frustum.Intersects(batch.mesh->Sphere()) || !inBeam(batch.mesh->Sphere())) continue;
            trees.setTextureProperty(batch.texture);
            ApplyAll(trees, device, *batch.mesh);
            ++stats_.drawCalls;
            stats_.triangles += batch.mesh->PrimitiveCount();
        }
        trees.setDiffuseColorProperty((headlightHighBeam_ ? Vector3(0.59f, 0.56f, 0.48f) :
                                                           Vector3(0.50f, 0.46f, 0.39f)) * headlightIntensity_);
        for (const auto& batch : signBatches_) {
            if (!batch.mesh || !frustum.Intersects(batch.mesh->Sphere()) || !inBeam(batch.mesh->Sphere())) continue;
            trees.setTextureProperty(batch.texture);
            ApplyAll(trees, device, *batch.mesh);
            ++stats_.drawCalls;
            stats_.triangles += batch.mesh->PrimitiveCount();
        }

        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
    }

    Texture2D* WorldRenderer::TextureFor(const Surface s) const
    {
        switch (s) {
            case Surface::Asphalt: return asphalt_.get();
            case Surface::Gravel: return gravel_.get();
            case Surface::Paving: return paving_.get();
            case Surface::Concrete: return concrete_.get();
            case Surface::Marking: return marking_.get();
            case Surface::Grass: return grass_.get();
            case Surface::Cobbles: return cobbles_.get();
        }
        return white_.get();
    }

    void WorldRenderer::Draw(GraphicsDevice& device, const Matrix& view, const Matrix& projection, const BoundingFrustum& frustum,
                             const bool mirrored, const float maxDistance)
    {
        const RasterizerState& solid = mirrored ? RasterizerState::CullClockwise : RasterizerState::CullCounterClockwise;
        // Everything this pass may draw stops here.
        // The quality tier scales the main view's distances; a pass with its own cap (the mirror)
        // takes the shorter of the two.
        const float scale = maxDistance > 0.0f ? 1.0f : drawDistanceScale_;
        const float horizon = maxDistance > 0.0f ? std::min(maxDistance, rig_.fogEnd) : rig_.fogEnd * scale;
        const float terrainCull = maxDistance > 0.0f ? std::min(maxDistance, terrainCullDistanceM) : terrainCullDistanceM * scale;
        stats_.terrainChunksDrawn = 0;
        stats_.roadBatchesDrawn = 0;
        stats_.drawCalls = 0;
        stats_.triangles = 0;

        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(solid);
        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
        device.getSamplerStatesProperty()[1] = SamplerState::LinearClamp;

        // Wet ground, grass and walls read darker: water fills the pores and the fine texture
        // that scattered light back. Applied per frame on top of the rig's scale, since the
        // wetness moves continuously while the lighting refreshes in steps.
        const float wetGround = 1.0f - 0.16f * wetness_;
        terrainEffect_->setDiffuseColorProperty(bakedScale_ * wetGround);
        terrainEffect_->setWorldProperty(Matrix::getIdentityProperty());
        terrainEffect_->setViewProperty(view);
        terrainEffect_->setProjectionProperty(projection);
        const Vector3 eye = Matrix::Invert(view).getTranslationProperty();
        // Lying snow: the same surfaces again, alpha-blended white, pulled forward by the
        // marking depth bias so they win the depth test against themselves.
        const auto beginSnow = [&](const float alpha) {
            const Vector3 tint(std::min(1.0f, bakedScale_.X * 1.55f), std::min(1.0f, bakedScale_.Y * 1.58f),
                               std::min(1.0f, bakedScale_.Z * 1.66f));
            const float opacity = std::clamp(alpha, 0.0f, 1.0f);
            snowEffect_->setWorldProperty(Matrix::getIdentityProperty());
            snowEffect_->setViewProperty(view);
            snowEffect_->setProjectionProperty(projection);
            snowEffect_->setTextureProperty(snowTexture_.get());
            // Snow reflects most of the light the baked ground absorbed: well above the grass.
            snowEffect_->setDiffuseColorProperty(tint);
            snowEffect_->setAlphaProperty(opacity);
            snowEffect_->setFogColorProperty(rig_.fogColor);
            snowEffect_->setFogStartProperty(rig_.fogStart);
            snowEffect_->setFogEndProperty(rig_.fogEnd);
            device.setBlendStateProperty(BlendState::NonPremultiplied);
            device.setDepthStencilStateProperty(DepthStencilState::DepthRead);
            device.setRasterizerStateProperty(mirrored ? *markingStateMirrored_ : *markingState_);
            device.getSamplerStatesProperty()[0] = SamplerState::LinearWrap;
        };
        const auto endSnow = [&]() {
            device.setBlendStateProperty(BlendState::Opaque);
            device.setDepthStencilStateProperty(DepthStencilState::Default);
            device.setRasterizerStateProperty(solid);
            device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
        };
        const bool snowing = snow_ > 0.01f && snowEffect_ && snowTexture_;
        // At full cover the terrain already supplies snow beneath the grass verge.
        // Let it meet the gravel shoulder directly instead of layering two snow patterns.
        const bool fullSnowGround = snowing && snow_ >= 0.98f;
        std::vector<const GpuMesh*> snowTerrain;
        if (terrainSkirt_) {
            ApplyAll(*terrainEffect_, device, *terrainSkirt_);
            ++stats_.drawCalls;
            stats_.triangles += terrainSkirt_->PrimitiveCount();
        }
        for (const auto& chunk : terrainChunks_) {
            if (!chunk.lod0) {
                continue;
            }
            const float distance = Vector3::Distance(eye, chunk.centre) - chunk.radius;
            if (distance > terrainCull || !frustum.Intersects(chunk.lod0->Sphere())) {
                continue;
            }
            const GpuMesh* mesh = distance < lod1DistanceM ? chunk.lod0.get() : (distance < lod2DistanceM ? chunk.lod1.get() : chunk.lod2.get());
            if (!mesh) mesh = chunk.lod0.get();
            ApplyAll(*terrainEffect_, device, *mesh);
            ++stats_.terrainChunksDrawn;
            ++stats_.drawCalls;
            stats_.triangles += mesh->PrimitiveCount();
            if (snowing) {
                const GpuMesh* snowMesh = distance < lod1DistanceM ? chunk.snowLod0.get() :
                                          (distance < lod2DistanceM ? chunk.snowLod1.get() : chunk.snowLod2.get());
                snowTerrain.push_back(snowMesh ? snowMesh : chunk.snowLod0.get());
            }
        }
        if (snowing) {
            beginSnow(snow_);
            for (const GpuMesh* mesh : snowTerrain) {
                ApplyAll(*snowEffect_, device, *mesh);
                ++stats_.drawCalls;
            }
            endSnow();
        }

        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
        roadUnlitEffect_->setWorldProperty(Matrix::getIdentityProperty());
        roadUnlitEffect_->setViewProperty(view);
        roadUnlitEffect_->setProjectionProperty(projection);
        for (int pass = 0; pass < 2; ++pass) {
            // Pass 0: surfaces; pass 1: markings with a depth bias.
            const bool markings = pass == 1;
            device.setRasterizerStateProperty(markings ? (mirrored ? *markingStateMirrored_ : *markingState_) : solid);
            Vector3 tint = markings ? Vector3(0.92f, 0.92f, 0.90f) : Vector3(1.0f, 1.0f, 1.0f);
            if (wetness_ > 0.0f && !markings) {
                // Per batch below: grass verges darken like the terrain beside them.
            } else if (wetness_ > 0.0f) {
                // Wet asphalt swallows light and takes on the colour of the sky it reflects; the
                // paint on it darkens less, because it stays rough.
                const float wet = wetness_ * (markings ? 0.45f : 1.0f);
                const Vector3 sky = rig_.horizonColor;
                tint = Vector3(tint.X * (1.0f - 0.42f * wet) + sky.X * 0.10f * wet,
                               tint.Y * (1.0f - 0.42f * wet) + sky.Y * 0.10f * wet,
                               tint.Z * (1.0f - 0.40f * wet) + sky.Z * 0.13f * wet);
            }
            roadUnlitEffect_->setDiffuseColorProperty(Vector3(tint.X * bakedScale_.X, tint.Y * bakedScale_.Y, tint.Z * bakedScale_.Z));
            for (const auto& b : roadBatches_) {
                if ((b.surface == Surface::Marking) != markings || !b.mesh || !frustum.Intersects(b.mesh->Sphere())) {
                    continue;
                }
                if (fullSnowGround && b.verge) continue;
                if (wetness_ > 0.0f && !markings) {
                    // Wet asphalt and stone swallow light and take on the colour of the sky
                    // they reflect; grass only darkens, like the terrain beside it.
                    Vector3 t(1.0f, 1.0f, 1.0f);
                    if (b.surface == Surface::Grass) {
                        t = Vector3(wetGround, wetGround, wetGround);
                    } else {
                        const float wet = wetness_ * (b.surface == Surface::Asphalt ? 1.0f : 0.8f);
                        const Vector3 sky = rig_.horizonColor;
                        t = Vector3(1.0f - 0.42f * wet + sky.X * 0.10f * wet, 1.0f - 0.42f * wet + sky.Y * 0.10f * wet,
                                    1.0f - 0.40f * wet + sky.Z * 0.13f * wet);
                    }
                    roadUnlitEffect_->setDiffuseColorProperty(Vector3(t.X * bakedScale_.X, t.Y * bakedScale_.Y, t.Z * bakedScale_.Z));
                }
                roadUnlitEffect_->setTextureProperty(TextureFor(b.surface));
                ApplyAll(*roadUnlitEffect_, device, *b.mesh);
                ++stats_.roadBatchesDrawn;
                ++stats_.drawCalls;
                stats_.triangles += b.mesh->PrimitiveCount();
            }
        }
        // Wet sheen over the road surfaces (not the markings: paint stays rough when it is wet).
        if (wetness_ > 0.01f && roadSheenEffect_) {
            const Vector3 sky = rig_.horizonColor;
            const float strength = 0.95f * wetness_;
            roadSheenEffect_->setWorldProperty(Matrix::getIdentityProperty());
            roadSheenEffect_->setViewProperty(view);
            roadSheenEffect_->setProjectionProperty(projection);
            roadSheenEffect_->setFogColorProperty(Vector3(sky.X * strength, sky.Y * strength, sky.Z * strength));
            // The ramp: nothing under the bumper, full sheen by the time the road is a hundred
            // metres off, and never beyond the fog, where there is no road left to see.
            roadSheenEffect_->setFogStartProperty(7.0f);
            roadSheenEffect_->setFogEndProperty(std::min(150.0f, rig_.fogEnd));
            // The road loop left the marking rasterizer state (with its depth bias) in place;
            // the sheen is the same geometry as the surfaces, so it wants the plain one.
            device.setRasterizerStateProperty(solid);
            device.setBlendStateProperty(BlendState::Additive);
            device.setDepthStencilStateProperty(DepthStencilState::DepthRead);
            for (const auto& b : roadBatches_) {
                if (b.surface == Surface::Marking || b.surface == Surface::Gravel || b.surface == Surface::Grass || !b.mesh ||
                    !frustum.Intersects(b.mesh->Sphere())) {
                    continue;
                }
                // Smooth asphalt mirrors the most; slabs, kerbs and cobbles are rougher and
                // drain between their joints.
                const float surfaceShare = b.surface == Surface::Asphalt ? 1.0f : (b.surface == Surface::Cobbles ? 0.55f : 0.75f);
                roadSheenEffect_->setFogColorProperty(sky * (strength * surfaceShare));
                ApplyAll(*roadSheenEffect_, device, *b.mesh);
                ++stats_.drawCalls;
                stats_.triangles += b.mesh->PrimitiveCount();
            }
            // Standing water once the rain has soaked the road: brighter patches of reflected
            // sky, fading out with distance like the sheen.
            const float standing = std::clamp((wetness_ - 0.35f) / 0.65f, 0.0f, 1.0f);
            if (standing > 0.0f && puddleEffect_ && puddleMask_) {
                puddleEffect_->setWorldProperty(Matrix::getIdentityProperty());
                puddleEffect_->setViewProperty(view);
                puddleEffect_->setProjectionProperty(projection);
                puddleEffect_->setTextureProperty(puddleMask_.get());
                puddleEffect_->setDiffuseColorProperty(Vector3(0.015f, 0.015f, 0.02f) + sky * (0.20f * standing));
                puddleEffect_->setFogStartProperty(6.0f);
                puddleEffect_->setFogEndProperty(std::min(120.0f, rig_.fogEnd));
                // The puddles lie on the road surface itself; the marking state's depth bias
                // keeps them from fighting it for the depth test.
                device.setRasterizerStateProperty(mirrored ? *markingStateMirrored_ : *markingState_);
                for (const auto& b : roadBatches_) {
                    if (!b.puddles || !frustum.Intersects(b.puddles->Sphere())) {
                        continue;
                    }
                    ApplyAll(*puddleEffect_, device, *b.puddles);
                    ++stats_.drawCalls;
                    stats_.triangles += b.puddles->PrimitiveCount();
                }
                device.setRasterizerStateProperty(solid);
            }
            device.setBlendStateProperty(BlendState::Opaque);
            device.setDepthStencilStateProperty(DepthStencilState::Default);
        }

        // Snow on the roads and pavements: a little thinner than on the fields, where the
        // traffic has worn it into slush.
        if (snowing) {
            beginSnow(snow_ * 0.78f);
            for (const auto& b : roadBatches_) {
                if (!b.mesh || !frustum.Intersects(b.mesh->Sphere())) continue;
                if (fullSnowGround && b.verge) continue;
                // Traffic wears snow thin on pavement. The grass verge belongs to the
                // adjoining field, while loose gravel retains more cover than asphalt.
                // Match those surfaces to their surroundings without another pass.
                const float cover = b.surface == Surface::Grass ? snow_ :
                                    b.surface == Surface::Gravel ? snow_ * 0.92f : snow_ * 0.78f;
                snowEffect_->setAlphaProperty(std::clamp(cover, 0.0f, 1.0f));
                ApplyAll(*snowEffect_, device, b.snowBase ? *b.snowBase : *b.mesh);
                ++stats_.drawCalls;
            }
            endSnow();
        }

        // Static objects: buildings, props, trunks (lit, textured).
        device.setRasterizerStateProperty(solid);
        roadEffect_->setWorldProperty(Matrix::getIdentityProperty());
        roadEffect_->setViewProperty(view);
        roadEffect_->setProjectionProperty(projection);
        stats_.objectBatchesDrawn = 0;
        for (const auto& b : objectBatches_) {
            if (!b.mesh || !frustum.Intersects(b.mesh->Sphere())) {
                continue;
            }
            // Nothing past the fog end can be told from the fog itself, so no batch is drawn
            // beyond it; detail batches keep their own shorter range. Without this the buildings
            // of a settlement three kilometres away are still submitted in full.
            const float cull = b.cullDistance > 0.0f ? std::min(b.cullDistance, horizon) : horizon;
            if (Vector3::Distance(eye, b.mesh->Sphere().Center) - b.mesh->Sphere().Radius > cull) {
                continue;
            }
            if (b.treeTrunk && rig_.fogEnd < 400.0f &&
                Vector3::Distance(eye, b.mesh->Sphere().Center) > horizon + 30.0f) continue;
            roadEffect_->setTextureProperty(b.texture);
            // Rain-soaked plaster, roof tiles and wood darken and pick up a wet gloss.
            roadEffect_->setDiffuseColorProperty(b.diffuse * (1.0f - 0.22f * wetness_));
            roadEffect_->setEmissiveColorProperty(b.emissive + b.nightEmissive * lampFactor_);
            roadEffect_->setSpecularColorProperty(b.specular + Vector3(0.20f, 0.20f, 0.21f) * wetness_);
            roadEffect_->setSpecularPowerProperty(b.specularPower + 20.0f * wetness_);
            ApplyAll(*roadEffect_, device, *b.mesh);
            ++stats_.objectBatchesDrawn;
            ++stats_.drawCalls;
            stats_.triangles += b.mesh->PrimitiveCount();
        }
        roadEffect_->setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        roadEffect_->setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        roadEffect_->setSpecularColorProperty(Vector3(0.06f, 0.06f, 0.06f));
        roadEffect_->setSpecularPowerProperty(10.0f);
        // Snow on the roofs.
        if (snowing) {
            beginSnow(snow_);
            for (const auto& b : objectBatches_) {
                if (!b.roof || !b.mesh || !frustum.Intersects(b.mesh->Sphere())) continue;
                if (Vector3::Distance(eye, b.mesh->Sphere().Center) - b.mesh->Sphere().Radius > horizon) continue;
                ApplyAll(*snowEffect_, device, *b.mesh);
                ++stats_.drawCalls;
            }
            endSnow();
        }

        // Trees: alpha-tested cards, both windings present, distance culled.
        const Vector3 cameraPosition = Matrix::Invert(view).getTranslationProperty();
        // Alpha-tested foliage otherwise leaves pale cut-out silhouettes after the rest of the
        // world has vanished into dense fog. Use the same visibility horizon as opaque objects;
        // the effect's fog ramp hides the last visible cards before a batch is culled.
        const float treeRange = std::min(horizon, maxDistance > 0.0f ? std::min(maxDistance, 1100.0f) : 1100.0f * scale * vegetationScale_);
        device.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
        treeEffect_->setDiffuseColorProperty(bakedScale_ * (1.0f - 0.10f * wetness_));
        treeEffect_->setWorldProperty(Matrix::getIdentityProperty());
        treeEffect_->setViewProperty(view);
        treeEffect_->setProjectionProperty(projection);
        stats_.treeBatchesDrawn = 0;
        for (const auto& b : treeBatches_) {
            if (!b.mesh || !frustum.Intersects(b.mesh->Sphere())) {
                continue;
            }
            const BoundingSphere& sphere = b.mesh->Sphere();
            const float centreDistance = Vector3::Distance(sphere.Center, cameraPosition);
            if (centreDistance - sphere.Radius > treeRange) {
                continue;
            }
            if (rig_.fogEnd < 400.0f && centreDistance > treeRange + 30.0f) {
                continue;   // a coarse 256 m chunk must not leave a white silhouette beyond dense fog
            }
            treeEffect_->setTextureProperty(b.texture);
            ApplyAll(*treeEffect_, device, *b.mesh);
            ++stats_.treeBatchesDrawn;
            ++stats_.drawCalls;
            stats_.triangles += b.mesh->PrimitiveCount();
        }
        // Sign faces: alpha-tested, both sides drawn (two quads), no distance cull beyond the frustum.
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        for (const auto& b : signBatches_) {
            if (!b.mesh || !frustum.Intersects(b.mesh->Sphere())) {
                continue;
            }
            treeEffect_->setTextureProperty(b.texture);
            ApplyAll(*treeEffect_, device, *b.mesh);
            ++stats_.drawCalls;
            stats_.triangles += b.mesh->PrimitiveCount();
        }
        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);

        if (!mirrored) DrawHeadlightFill(device, view, projection, frustum);
        // Street lamp light last, so it adds on top of everything the pass has drawn.
        DrawLampLights(device, view, projection, frustum);
    }
}
