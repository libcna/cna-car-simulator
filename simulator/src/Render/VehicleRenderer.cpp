#include "CarSim/Render/VehicleRenderer.hpp"

#include "CarSim/Render/CarTextures.hpp"
#include "CarSim/Render/Image.hpp"
#include "CarSim/Render/ProceduralTextures.hpp"
#include "CarSim/Render/ShadowGeometry.hpp"
#include "CarSim/Sim/Units.hpp"

#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"

#include <cmath>
#include <cstdint>
#include <optional>

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

        struct MaterialLook
        {
            Vector3 diffuse{0.8f, 0.8f, 0.8f};
            Vector3 specular{0.1f, 0.1f, 0.1f};
            float specularPower = 16.0f;
            Vector3 emissive{0.0f, 0.0f, 0.0f};
            float alpha = 1.0f;
            float envAmount = 0.0f;   // > 0: environment-mapped (paint, chrome)
        };

        MaterialLook LookFor(const CarPart& part, const Vector3& paintColor, const Vector3& interiorColor, const Sim::VehicleState& state,
                             const std::optional<Vector3>& paintOverride)
        {
            MaterialLook look;
            switch (part.material) {
                case CarMaterial::Paint:
                    look.diffuse = paintOverride.value_or(paintColor);
                    look.specular = Vector3(0.7f, 0.7f, 0.7f);
                    look.specularPower = 48.0f;
                    look.envAmount = 0.22f;
                    break;
                case CarMaterial::Glass:
                    look.diffuse = Vector3(0.10f, 0.13f, 0.16f);
                    look.specular = Vector3(0.5f, 0.5f, 0.5f);
                    look.specularPower = 90.0f;
                    look.alpha = 1.0f;   // the tint texture is premultiplied and carries the alpha
                    look.envAmount = 0.45f;   // sky reflection from outside (disabled from inside)
                    break;
                case CarMaterial::BlackTrim:
                    look.diffuse = Vector3(0.05f, 0.05f, 0.055f);
                    look.specular = Vector3(0.12f, 0.12f, 0.12f);
                    look.specularPower = 10.0f;
                    break;
                case CarMaterial::GlossBlack:
                    look.diffuse = Vector3(0.025f, 0.025f, 0.03f);
                    look.specular = Vector3(0.9f, 0.9f, 0.9f);
                    look.specularPower = 70.0f;
                    break;
                case CarMaterial::Chrome:
                    look.diffuse = Vector3(0.62f, 0.63f, 0.65f);
                    look.specular = Vector3(1.0f, 1.0f, 1.0f);
                    look.specularPower = 80.0f;
                    look.envAmount = 0.8f;
                    break;
                case CarMaterial::Tyre:
                    look.diffuse = Vector3(0.95f, 0.95f, 0.95f);   // the tread texture carries the tone
                    look.specular = Vector3(0.06f, 0.06f, 0.06f);
                    look.specularPower = 8.0f;
                    break;
                case CarMaterial::Rim:
                    look.diffuse = Vector3(0.78f, 0.79f, 0.80f);
                    look.specular = Vector3(0.9f, 0.9f, 0.9f);
                    look.specularPower = 44.0f;
                    break;
                case CarMaterial::BrakeDisc:
                    look.diffuse = Vector3(0.30f, 0.30f, 0.31f);
                    look.specular = Vector3(0.5f, 0.5f, 0.5f);
                    look.specularPower = 30.0f;
                    break;
                case CarMaterial::Grille:
                    look.diffuse = Vector3(1.0f, 1.0f, 1.0f);
                    look.specular = Vector3(0.25f, 0.25f, 0.25f);
                    look.specularPower = 20.0f;
                    break;
                case CarMaterial::Interior:
                    look.diffuse = interiorColor * 1.15f;
                    look.specular = Vector3(0.06f, 0.06f, 0.06f);
                    look.specularPower = 8.0f;
                    break;
                case CarMaterial::InteriorMid:
                    look.diffuse = interiorColor * 2.0f + Vector3(0.04f, 0.04f, 0.04f);
                    look.specular = Vector3(0.05f, 0.05f, 0.05f);
                    look.specularPower = 8.0f;
                    break;
                case CarMaterial::Vent:
                    look.diffuse = Vector3(1.0f, 1.0f, 1.0f);
                    look.specular = Vector3(0.15f, 0.15f, 0.15f);
                    look.specularPower = 12.0f;
                    break;
                case CarMaterial::InteriorLight:
                    look.diffuse = Vector3(0.62f, 0.62f, 0.60f);
                    look.specular = Vector3(0.02f, 0.02f, 0.02f);
                    look.specularPower = 4.0f;
                    break;
                case CarMaterial::Fabric:
                    look.diffuse = Vector3(0.21f, 0.21f, 0.23f);   // dark grey cloth
                    look.specular = Vector3(0.03f, 0.03f, 0.03f);
                    look.specularPower = 4.0f;
                    break;
                case CarMaterial::LampHead:
                    look.diffuse = Vector3(0.85f, 0.88f, 0.92f);
                    look.specular = Vector3(1.0f, 1.0f, 1.0f);
                    look.specularPower = 80.0f;
                    if (state.lowBeam) {
                        look.emissive = state.highBeam ? Vector3(1.0f, 0.98f, 0.90f) : Vector3(0.70f, 0.70f, 0.64f);
                    }
                    break;
                case CarMaterial::LampTail:
                    look.diffuse = Vector3(0.62f, 0.05f, 0.04f);
                    look.specular = Vector3(0.8f, 0.8f, 0.8f);
                    look.specularPower = 60.0f;
                    if (state.brakeLights) {
                        look.emissive = Vector3(0.95f, 0.05f, 0.03f);
                    } else if (state.lowBeam) {
                        look.emissive = Vector3(0.38f, 0.02f, 0.01f);
                    }
                    break;
                case CarMaterial::LampIndicator: {
                    look.diffuse = Vector3(0.90f, 0.50f, 0.10f);
                    look.specular = Vector3(0.8f, 0.8f, 0.8f);
                    look.specularPower = 60.0f;
                    const bool left = part.name.find("left") != std::string::npos;
                    const bool lit = left ? state.leftIndicatorLit : state.rightIndicatorLit;
                    if (lit) {
                        look.emissive = Vector3(1.0f, 0.55f, 0.05f);
                    }
                    break;
                }
                case CarMaterial::LampReverse:
                    look.diffuse = Vector3(0.82f, 0.82f, 0.82f);
                    look.specular = Vector3(0.8f, 0.8f, 0.8f);
                    look.specularPower = 60.0f;
                    if (state.reverseLights) {
                        look.emissive = Vector3(0.95f, 0.95f, 0.9f);
                    }
                    break;
                case CarMaterial::Plate:
                    look.diffuse = Vector3(1.0f, 1.0f, 1.0f);
                    look.specular = Vector3(0.3f, 0.3f, 0.3f);
                    look.specularPower = 20.0f;
                    break;
                case CarMaterial::Cluster:
                    look.diffuse = Vector3(1.0f, 1.0f, 1.0f);
                    look.specular = Vector3(0.0f, 0.0f, 0.0f);
                    look.emissive = state.ignitionOn ? Vector3(0.55f, 0.55f, 0.55f) : Vector3(0.15f, 0.15f, 0.15f);
                    break;
                case CarMaterial::Needle:
                    look.diffuse = Vector3(0.9f, 0.1f, 0.05f);
                    look.emissive = state.ignitionOn ? Vector3(0.8f, 0.05f, 0.02f) : Vector3(0.1f, 0.0f, 0.0f);
                    break;
            }
            return look;
        }
    }

    // ------------------------------------------------------------------ materials

    VehicleMaterials::VehicleMaterials(GraphicsDevice& device, const LightingRig& rig)
    {
        lit_ = std::make_unique<BasicEffect>(device);
        rig.Apply(*lit_);
        lit_->setTextureEnabledProperty(true);   // bound to the white texture; keeps one shader path for all lit parts
        lit_->setVertexColorEnabledProperty(false);

        litTextured_ = std::make_unique<BasicEffect>(device);
        rig.Apply(*litTextured_);
        litTextured_->setTextureEnabledProperty(true);
        litTextured_->setVertexColorEnabledProperty(false);

        paint_ = std::make_unique<EnvironmentMapEffect>(device);
        rig.Apply(*paint_);
        const Rgb zenith{rig.zenithColor.X, rig.zenithColor.Y, rig.zenithColor.Z};
        const Rgb horizon{rig.horizonColor.X, rig.horizonColor.Y, rig.horizonColor.Z};
        environment_ = UploadCubeMap(device, Textures::SkyCubeFaces(64, zenith, horizon, {0.30f, 0.30f, 0.26f}, -rig.sunDirection, 300.0f));
        paint_->setEnvironmentMapProperty(environment_.get());
        // Subtle, view-dependent sheen: the diffuse paint colour must stay readable head-on.
        // The cube map's alpha is a sun mask, so EnvironmentMapSpecular only adds a sun glint.
        paint_->setEnvironmentMapAmountProperty(0.22f);
        paint_->setEnvironmentMapSpecularProperty(Vector3(0.9f, 0.86f, 0.78f));
        paint_->setFresnelFactorProperty(2.2f);

        // Cabin surfaces are lit by light entering through the glass from every direction:
        // stronger ambient, softer key light, no fog.
        interiorLit_ = std::make_unique<BasicEffect>(device);
        rig.Apply(*interiorLit_);
        interiorLit_->setAmbientLightColorProperty(Vector3(0.50f, 0.51f, 0.55f));
        interiorLit_->getDirectionalLight0Property().setDiffuseColorProperty(rig.sunColor * 0.65f);
        interiorLit_->getDirectionalLight0Property().setSpecularColorProperty(rig.sunColor * 0.3f);
        interiorLit_->getDirectionalLight1Property().setDiffuseColorProperty(Vector3(0.24f, 0.26f, 0.30f));
        interiorLit_->getDirectionalLight2Property().setDiffuseColorProperty(Vector3(0.16f, 0.15f, 0.13f));
        interiorLit_->setFogEnabledProperty(false);
        interiorLit_->setTextureEnabledProperty(true);
        interiorLit_->setVertexColorEnabledProperty(false);

        // Shadow: unlit, vertex alpha times a texture alpha (white for the hull, the box falloff
        // for the contact shadow), drawn with premultiplied alpha so it only darkens.
        shadow_ = std::make_unique<BasicEffect>(device);
        shadow_->setLightingEnabledProperty(false);
        shadow_->setTextureEnabledProperty(true);
        shadow_->setVertexColorEnabledProperty(true);
        shadow_->setDiffuseColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        shadow_->setAlphaProperty(1.0f);
        shadow_->setFogEnabledProperty(false);
        shadow_->setWorldProperty(Matrix::getIdentityProperty());
        Image contact(64, 64, Color(255, 255, 255, 0));
        contact.Generate([](int, int, float u, float v) {
            // Flat core, smooth falloff to the edge of the footprint quad.
            const float inner = 0.42f;
            const float dx = std::max(0.0f, std::fabs(u * 2.0f - 1.0f) - inner) / (1.0f - inner);
            const float dy = std::max(0.0f, std::fabs(v * 2.0f - 1.0f) - inner) / (1.0f - inner);
            const float d = std::clamp(std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
            const float a = 1.0f - d * d * (3.0f - 2.0f * d);
            return Color(255, 255, 255, static_cast<int>(a * 255.0f));
        });
        contactTexture_ = UploadTexture(device, contact, false);

        white_ = UploadTexture(device, Textures::Solid(4, Color(255, 255, 255, 255)), false);
        // Lamp glow: unlit additive sprite with a soft radial falloff.
        glow_ = std::make_unique<BasicEffect>(device);
        glow_->setLightingEnabledProperty(false);
        glow_->setTextureEnabledProperty(true);
        glow_->setVertexColorEnabledProperty(false);
        glow_->setFogEnabledProperty(false);
        Image glowImage(64, 64, Color(0, 0, 0, 255));
        glowImage.Generate([](int, int, float u, float v) {
            const float d = std::sqrt((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f)) * 2.0f;
            const float a = std::clamp(1.0f - d, 0.0f, 1.0f);
            const int c = static_cast<int>(a * a * 255.0f);
            return Color(c, c, c, 255);
        });
        glowTexture_ = UploadTexture(device, glowImage, true);
        tyre_ = UploadTexture(device, CarTextures::TyreTread(256), true);
        rim_ = UploadTexture(device, CarTextures::RimFinish(128), true);
        headlamp_ = UploadTexture(device, CarTextures::HeadlampLens(128), true);
        tailLamp_ = UploadTexture(device, CarTextures::TailLampLens(64), true);
        grille_ = UploadTexture(device, CarTextures::GrilleMesh(128), true);
        plastic_ = UploadTexture(device, CarTextures::InteriorPlastic(256, Rgb{0.86f, 0.86f, 0.88f}, 5u), true);
        fabric_ = UploadTexture(device, CarTextures::Fabric(256, Rgb{0.95f, 0.95f, 0.98f}, 9u), true);
        headliner_ = UploadTexture(device, CarTextures::Headliner(128), true);
        vent_ = UploadTexture(device, CarTextures::VentSlats(64), true);

        // Default plate: blank white face with the blue band (the traffic system supplies real plates).
        Image plate(256, 54, Color(250, 250, 250, 255));
        plate.FillRect(0, 0, 22, 54, Color(0, 51, 153, 255));
        plate.FillRect(0, 0, 256, 2, Color(20, 20, 20, 255));
        plate.FillRect(0, 52, 256, 54, Color(20, 20, 20, 255));
        plate.FillRect(0, 0, 2, 54, Color(20, 20, 20, 255));
        plate.FillRect(254, 0, 256, 54, Color(20, 20, 20, 255));
        defaultPlate_ = UploadTexture(device, plate, true);

        // Default cluster: dark face with two dial rings (replaced by the instrument cluster renderer).
        Image cluster(512, 224, Color(18, 18, 20, 255));
        for (const float cx : {128.0f, 384.0f}) {
            cluster.FillCircle(cx, 112.0f, 96.0f, Color(40, 40, 44, 255));
            cluster.FillCircle(cx, 112.0f, 90.0f, Color(22, 22, 25, 255));
        }
        defaultCluster_ = UploadTexture(device, cluster, true);
    }

    Texture2D* VehicleMaterials::TextureFor(const CarMaterial material) const
    {
        switch (material) {
            case CarMaterial::Tyre: return tyre_.get();
            case CarMaterial::Rim: return rim_.get();
            case CarMaterial::LampHead: return headlamp_.get();
            case CarMaterial::LampTail:
            case CarMaterial::LampIndicator:
            case CarMaterial::LampReverse: return tailLamp_.get();
            case CarMaterial::Grille: return grille_.get();
            case CarMaterial::Interior:
            case CarMaterial::InteriorMid: return plastic_.get();
            case CarMaterial::Vent: return vent_.get();
            case CarMaterial::Fabric: return fabric_.get();
            case CarMaterial::InteriorLight: return headliner_.get();
            default: return white_.get();
        }
    }

    // ------------------------------------------------------------------ renderer

    VehicleRenderer::VehicleRenderer(GraphicsDevice& device, VehicleMaterials& materials, const Sim::VehicleDefinition& definition)
        : materials_(materials),
          paintColor_(definition.visual.paintColor),
          interiorColor_(definition.visual.interiorColor),
          model_(GenerateCar(definition))
    {
        Upload(device);
    }

    VehicleRenderer::VehicleRenderer(GraphicsDevice& device, VehicleMaterials& materials, const CarStyle& style,
                                     const Sim::VehicleDefinition* definition, const bool interior)
        : materials_(materials),
          paintColor_(definition ? definition->visual.paintColor : Vector3(0.7f, 0.7f, 0.7f)),
          interiorColor_(definition ? definition->visual.interiorColor : Vector3(0.16f, 0.16f, 0.17f)),
          model_(GenerateCar(style, definition, interior))
    {
        Upload(device);
    }

    void VehicleRenderer::Upload(GraphicsDevice& device)
    {
        triangles_ = 0;
        for (const auto& part : model_.parts) {
            GpuPart gpu;
            gpu.part = &part;
            gpu.mesh = GpuMesh::Create(device, part.mesh, VertexLayout::PositionNormalTexture);
            triangles_ += static_cast<int>(part.mesh.TriangleCount());
            parts_.push_back(std::move(gpu));
        }
        paintDetail_ = UploadTexture(device, CarTextures::PaintDetail(model_.uv, 1024), true);
        MeshData quad;
        quad.AddQuad(Vector3(-0.5f, -0.5f, 0), Vector3(0.5f, -0.5f, 0), Vector3(0.5f, 0.5f, 0), Vector3(-0.5f, 0.5f, 0), Vector3(0, 0, 1),
                     Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0));
        glowQuad_ = GpuMesh::Create(device, quad, VertexLayout::PositionTexture);
        glassOutside_ = UploadTexture(device, CarTextures::GlassTint(model_.uv, 512, 0.62f), true);
        glassInside_ = UploadTexture(device, CarTextures::GlassTint(model_.uv, 512, 0.20f), true);

        // Shadow casters: the rigid exterior (body, glass, lamps, mirrors) and each wheel reduced
        // to their extreme vertices; small detail parts add nothing to the silhouette.
        casters_.clear();
        std::vector<Vector3> body;
        std::vector<Vector3> wheels[4];
        const CarPart* wheelPart[4] = {nullptr, nullptr, nullptr, nullptr};
        for (const auto& part : model_.parts) {
            if (IsInteriorPart(part) || part.detail) continue;
            int wheel = -1;
            switch (part.role) {
                case CarPart::Role::WheelFL: wheel = 0; break;
                case CarPart::Role::WheelFR: wheel = 1; break;
                case CarPart::Role::WheelRL: wheel = 2; break;
                case CarPart::Role::WheelRR: wheel = 3; break;
                case CarPart::Role::Static: break;
                default: continue;
            }
            std::vector<Vector3>& target = wheel < 0 ? body : wheels[wheel];
            for (const auto& v : part.mesh.vertices) target.push_back(v.position);
            if (wheel >= 0 && !wheelPart[wheel]) wheelPart[wheel] = &part;
        }
        if (!body.empty()) casters_.push_back({nullptr, ShadowGeometry::ExtremePoints(body, 160)});
        for (int i = 0; i < 4; ++i) {
            if (!wheels[i].empty()) casters_.push_back({wheelPart[i], ShadowGeometry::ExtremePoints(wheels[i], 40)});
        }
        // Shadow batch buffers: refilled every frame, drawn through the same indexed path as the meshes.
        shadowVertices_ = std::make_unique<VertexBuffer>(device, VertexPositionColorTexture::getVertexDeclarationStatic(), kShadowVertexCapacity,
                                                         BufferUsage::WriteOnly);
        std::vector<std::uint32_t> identity(static_cast<std::size_t>(kShadowVertexCapacity));
        for (std::size_t i = 0; i < identity.size(); ++i) identity[i] = static_cast<std::uint32_t>(i);
        shadowIndices_ = std::make_unique<IndexBuffer>(device, IndexElementSize::ThirtyTwoBits, kShadowVertexCapacity, BufferUsage::WriteOnly);
        shadowIndices_->SetData(identity.data(), kShadowVertexCapacity);
    }

    Matrix VehicleRenderer::PartWorld(const CarPart& part, const Sim::VehicleState& state, const GaugePose& gauges) const
    {
        const Matrix vehicle = state.worldMatrix;
        switch (part.role) {
            case CarPart::Role::WheelFL:
            case CarPart::Role::WheelFR:
            case CarPart::Role::WheelRL:
            case CarPart::Role::WheelRR: {
                const int index = part.role == CarPart::Role::WheelFL ? 0 : part.role == CarPart::Role::WheelFR ? 1
                                : part.role == CarPart::Role::WheelRL ? 2 : 3;
                const auto& w = state.wheels[static_cast<std::size_t>(index)];
                // Spin about the axle (rolling forward = top of the wheel moving towards -z), then
                // steer about the vehicle's up axis, then place at the suspended wheel centre.
                const Matrix spin = Matrix::CreateRotationX(-w.spinAngle);
                const Matrix steer = Matrix::CreateRotationY(-w.steerAngle);
                Matrix rotationOnly = vehicle;
                rotationOnly.M41 = 0.0f;
                rotationOnly.M42 = 0.0f;
                rotationOnly.M43 = 0.0f;
                return spin * steer * rotationOnly * Matrix::CreateTranslation(w.worldCenter);
            }
            case CarPart::Role::SteeringWheel: {
                const Matrix turn = Matrix::CreateFromAxisAngle(part.axis, -state.steeringWheelAngle);
                return turn * Matrix::CreateTranslation(part.pivot) * vehicle;
            }
            case CarPart::Role::GearLever: {
                // Manual: H pattern (odd gears forward, even back, left/right by pair); automatic:
                // P forward through R, N to D back. Angles in radians about the lever base.
                float fore = 0.0f;
                float lateral = 0.0f;
                if (state.transmissionMode == Sim::TransmissionMode::Automatic) {
                    const char c = state.gearLabel.empty() ? 'N' : state.gearLabel[0];
                    fore = c == 'P' ? -0.34f : c == 'R' ? -0.16f : c == 'N' ? 0.02f : 0.20f;
                } else if (state.gear < 0) {
                    fore = 0.26f;
                    lateral = 0.30f;
                } else if (state.gear > 0) {
                    const int pair = (state.gear - 1) / 2;   // 0: 1-2, 1: 3-4, 2: 5-6
                    fore = (state.gear % 2 == 1) ? -0.26f : 0.26f;
                    lateral = pair == 0 ? 0.20f : pair == 1 ? 0.0f : -0.20f;
                }
                const Matrix tilt = Matrix::CreateRotationZ(lateral) * Matrix::CreateRotationX(fore);
                return tilt * Matrix::CreateTranslation(part.pivot) * vehicle;
            }
            case CarPart::Role::NeedleSpeed:
            case CarPart::Role::NeedleRpm:
            case CarPart::Role::NeedleFuel:
            case CarPart::Role::NeedleTemp: {
                float fraction = 0.0f;
                float sweep = 270.0f;
                switch (part.role) {
                    case CarPart::Role::NeedleSpeed: fraction = gauges.speed; break;
                    case CarPart::Role::NeedleRpm: fraction = gauges.rpm; break;
                    case CarPart::Role::NeedleFuel: fraction = gauges.fuel; sweep = 120.0f; break;
                    default: fraction = gauges.temperature; sweep = 120.0f; break;
                }
                const float angle = Sim::Units::DegToRad(-sweep * 0.5f + sweep * std::clamp(fraction, 0.0f, 1.0f));
                const Matrix turn = Matrix::CreateFromAxisAngle(part.axis, -angle);
                return turn * Matrix::CreateTranslation(part.pivot) * vehicle;
            }
            default:
                return vehicle;
        }
    }

    bool VehicleRenderer::IsInteriorPart(const CarPart& part)
    {
        return part.role == CarPart::Role::Interior || part.role == CarPart::Role::SteeringWheel || part.role == CarPart::Role::GearLever ||
               part.role == CarPart::Role::NeedleSpeed || part.role == CarPart::Role::NeedleRpm ||
               part.role == CarPart::Role::NeedleFuel || part.role == CarPart::Role::NeedleTemp;
    }

    void VehicleRenderer::DrawPart(GraphicsDevice& device, const GpuPart& gpu, const Sim::VehicleState& state,
                                   const Matrix& view, const Matrix& projection, const GaugePose& gauges)
    {
        if (!gpu.mesh) {
            return;
        }
        const CarPart& part = *gpu.part;
        MaterialLook look = LookFor(part, paintColor_, interiorColor_, state, paintOverride_);
        const Matrix world = PartWorld(part, state, gauges);
        const bool interior = IsInteriorPart(part);
        const bool mirrorFace = part.name == "mirror_face" && mirrorTexture_ != nullptr;
        const bool glass = part.material == CarMaterial::Glass;
        if (glass && glassFromInside_) {
            look.envAmount = 0.0f;
        }

        if (look.envAmount > 0.0f && !mirrorFace) {
            auto& e = materials_.Paint();
            e.setWorldProperty(world);
            e.setViewProperty(view);
            e.setProjectionProperty(projection);
            e.setDiffuseColorProperty(look.diffuse);
            e.setEmissiveColorProperty(look.emissive);
            e.setEnvironmentMapAmountProperty(look.envAmount);
            e.setFresnelFactorProperty(part.material == CarMaterial::Chrome ? 0.0f : glass ? 1.2f : 2.2f);
            e.setAlphaProperty(1.0f);
            e.setTextureProperty(part.material == CarMaterial::Paint ? paintDetail_.get() : glass ? glassOutside_.get() : &materials_.White());
            device.getSamplerStatesProperty()[0] = glass ? SamplerState::LinearClamp : SamplerState::AnisotropicWrap;
            ApplyAll(e, device, *gpu.mesh);
        } else if (part.material == CarMaterial::Plate || part.material == CarMaterial::Cluster || mirrorFace) {
            auto& e = materials_.LitTextured();
            Texture2D* texture = part.material == CarMaterial::Plate ? (plateTexture_ ? plateTexture_ : &materials_.DefaultPlate())
                               : part.material == CarMaterial::Cluster ? (clusterTexture_ ? clusterTexture_ : &materials_.DefaultCluster())
                               : mirrorTexture_;
            device.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
            const bool display = (part.material == CarMaterial::Cluster && clusterTexture_ != nullptr) || mirrorFace;
            e.setWorldProperty(world);
            e.setViewProperty(view);
            e.setProjectionProperty(projection);
            // Displays and the mirror image are self-lit: the texture is shown as is.
            e.setDiffuseColorProperty(display ? Vector3(0.0f, 0.0f, 0.0f) : look.diffuse);
            e.setEmissiveColorProperty(display ? Vector3(1.0f, 1.0f, 1.0f) : look.emissive);
            e.setSpecularColorProperty(display ? Vector3(0.0f, 0.0f, 0.0f) : look.specular);
            e.setSpecularPowerProperty(look.specularPower);
            e.setAlphaProperty(1.0f);
            e.setTextureProperty(texture);
            ApplyAll(e, device, *gpu.mesh);
        } else {
            auto& e = interior ? materials_.InteriorLit() : materials_.Lit();
            Texture2D* texture = glass ? glassInside_.get() : materials_.TextureFor(part.material);
            device.getSamplerStatesProperty()[0] = glass ? SamplerState::LinearClamp : SamplerState::AnisotropicWrap;
            e.setWorldProperty(world);
            e.setViewProperty(view);
            e.setProjectionProperty(projection);
            e.setDiffuseColorProperty(look.diffuse);
            e.setEmissiveColorProperty(look.emissive);
            e.setSpecularColorProperty(look.specular);
            e.setSpecularPowerProperty(look.specularPower);
            e.setAlphaProperty(look.alpha);
            e.setTextureProperty(texture);
            ApplyAll(e, device, *gpu.mesh);
        }
        ++drawCalls_;
    }

    void VehicleRenderer::DrawOpaque(GraphicsDevice& device, const Sim::VehicleState& state, const Matrix& view,
                                     const Matrix& projection, const bool drawInterior, const GaugePose& gauges, const bool mirrored, const int lod)
    {
        drawCalls_ = 0;
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(mirrored ? RasterizerState::CullClockwise : RasterizerState::CullCounterClockwise);
        for (const auto& gpu : parts_) {
            const CarPart& part = *gpu.part;
            if (part.material == CarMaterial::Glass) {
                continue;
            }
            if (IsInteriorPart(part) && !drawInterior && (!part.cabin || lod >= 2)) {
                continue;
            }
            if (lod >= 1 && part.detail) {
                continue;
            }
            if (lod >= 2 && (part.material == CarMaterial::Chrome || part.material == CarMaterial::GlossBlack || part.material == CarMaterial::Grille ||
                             part.material == CarMaterial::BrakeDisc || part.material == CarMaterial::Plate)) {
                continue;
            }
            DrawPart(device, gpu, state, view, projection, gauges);
        }
        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
    }

    void VehicleRenderer::DrawShadow(GraphicsDevice& device, const Sim::VehicleState& state, const Matrix& view, const Matrix& projection,
                                     const Vector3& sunDirection, const GroundQuery& ground)
    {
        using namespace ShadowGeometry;
        if (casters_.empty()) {
            return;
        }
        constexpr float kLift = 0.03f;          // above the road markings, below the wheels' contact patches
        constexpr float kPenumbraM = 0.12f;     // soft rim width
        constexpr int kSunAlpha = 140;          // 0.55: the sun is ~2/3 of the light on a horizontal surface
        constexpr int kContactAlpha = 115;      // 0.45 under the footprint (sky occluded by the car)

        const Vector3 origin = state.originPosition;
        Vector3 n = ground.normal ? ground.normal(origin.X, origin.Z) : Vector3(0.0f, 1.0f, 0.0f);
        if (n.LengthSquared() < 1e-6f) n = Vector3(0.0f, 1.0f, 0.0f);
        n.Normalize();
        const float baseHeight = ground.height ? ground.height(origin.X, origin.Z) : origin.Y;
        const Vector3 p0(origin.X, baseHeight, origin.Z);
        const auto drape = [&](Vector3 p, const float lift) {
            p.Y = (ground.height ? ground.height(p.X, p.Z) : baseHeight) + lift;
            return p;
        };
        Vector3 light = sunDirection;
        light.Normalize();
        Vector3 e1, e2;
        PlaneBasis(n, e1, e2);

        // Sun shadow: project every caster and take the hull in the plane's tangent basis.
        std::vector<Vector2> flat;
        flat.reserve(320);
        GaugePose none;
        for (const auto& caster : casters_) {
            const Matrix world = caster.part ? PartWorld(*caster.part, state, none) : state.worldMatrix;
            for (const Vector3& p : caster.points) {
                Vector3 q;
                if (!ProjectToPlane(Vector3::Transform(p, world), light, p0, n, q)) continue;
                const Vector3 d = q - p0;
                flat.emplace_back(Vector3::Dot(d, e1), Vector3::Dot(d, e2));
            }
        }
        const std::vector<Vector2> hull = ConvexHull(std::move(flat));
        std::vector<VertexPositionColorTexture> verts;
        const Color dark(0, 0, 0, kSunAlpha);
        const Color clear(0, 0, 0, 0);
        const Vector2 uvMid(0.5f, 0.5f);
        if (hull.size() >= 3) {
            const std::vector<Vector2> normals = OutwardNormals(hull);
            std::vector<Vector3> inner(hull.size()), outer(hull.size());
            Vector2 centre2(0.0f, 0.0f);
            for (const Vector2& h : hull) centre2 = centre2 + h;
            centre2 = centre2 * (1.0f / static_cast<float>(hull.size()));
            const Vector3 centre = drape(p0 + e1 * centre2.X + e2 * centre2.Y, kLift);
            for (std::size_t i = 0; i < hull.size(); ++i) {
                inner[i] = drape(p0 + e1 * hull[i].X + e2 * hull[i].Y, kLift);
                const Vector2 o = hull[i] + normals[i] * kPenumbraM;
                outer[i] = p0 + e1 * o.X + e2 * o.Y;
                outer[i].Y = inner[i].Y;
            }
            verts.reserve(hull.size() * 9);
            for (std::size_t i = 0; i < hull.size(); ++i) {
                const std::size_t j = (i + 1) % hull.size();
                verts.emplace_back(centre, dark, uvMid);
                verts.emplace_back(inner[i], dark, uvMid);
                verts.emplace_back(inner[j], dark, uvMid);
                verts.emplace_back(inner[i], dark, uvMid);
                verts.emplace_back(outer[i], clear, uvMid);
                verts.emplace_back(outer[j], clear, uvMid);
                verts.emplace_back(inner[i], dark, uvMid);
                verts.emplace_back(outer[j], clear, uvMid);
                verts.emplace_back(inner[j], dark, uvMid);
            }
        }

        // Contact shadow: the footprint as a 4 x 2 grid draped on the ground, alpha from the
        // box-falloff texture.
        std::vector<VertexPositionColorTexture> contact;
        {
            const CarStyle& st = model_.style;
            const float halfW = st.width * 0.5f + 0.10f;
            const float z0 = st.FrontZ() - 0.05f, z1 = st.RearZ() + 0.05f;
            constexpr int nx = 2, nz = 4;
            Vector3 grid[nz + 1][nx + 1];
            for (int iz = 0; iz <= nz; ++iz) {
                for (int ix = 0; ix <= nx; ++ix) {
                    const float x = -halfW + 2.0f * halfW * static_cast<float>(ix) / static_cast<float>(nx);
                    const float z = z0 + (z1 - z0) * static_cast<float>(iz) / static_cast<float>(nz);
                    grid[iz][ix] = drape(Vector3::Transform(Vector3(x, 0.0f, z), state.worldMatrix), kLift * 0.6f);
                }
            }
            const Color shade(0, 0, 0, kContactAlpha);
            contact.reserve(nz * nx * 6);
            for (int iz = 0; iz < nz; ++iz) {
                for (int ix = 0; ix < nx; ++ix) {
                    const auto uv = [&](int jz, int jx) {
                        return Vector2(static_cast<float>(jx) / static_cast<float>(nx), static_cast<float>(jz) / static_cast<float>(nz));
                    };
                    contact.emplace_back(grid[iz][ix], shade, uv(iz, ix));
                    contact.emplace_back(grid[iz][ix + 1], shade, uv(iz, ix + 1));
                    contact.emplace_back(grid[iz + 1][ix + 1], shade, uv(iz + 1, ix + 1));
                    contact.emplace_back(grid[iz][ix], shade, uv(iz, ix));
                    contact.emplace_back(grid[iz + 1][ix + 1], shade, uv(iz + 1, ix + 1));
                    contact.emplace_back(grid[iz + 1][ix], shade, uv(iz + 1, ix));
                }
            }
        }

        // One upload per car: the contact grid first, then the hull fan and rim; two indexed draws
        // with different textures (a stock vertex/index buffer pair, refilled every frame).
        if (!shadowVertices_ || !shadowIndices_) {
            return;
        }
        const int contactCount = static_cast<int>(contact.size());
        int hullCount = static_cast<int>(verts.size());
        if (contactCount + hullCount > kShadowVertexCapacity) {
            hullCount = std::max(0, (kShadowVertexCapacity - contactCount) / 3 * 3);
        }
        if (contactCount + hullCount == 0) {
            return;
        }
        contact.insert(contact.end(), verts.begin(), verts.begin() + hullCount);
        shadowVertices_->SetData(contact.data(), contactCount + hullCount);

        device.setBlendStateProperty(BlendState::AlphaBlend);
        device.setDepthStencilStateProperty(DepthStencilState::DepthRead);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
        device.SetVertexBuffer(shadowVertices_.get());
        device.setIndicesProperty(shadowIndices_.get());
        auto& e = materials_.Shadow();
        e.setViewProperty(view);
        e.setProjectionProperty(projection);
        const auto draw = [&](const int first, const int count, Texture2D& texture) {
            if (count < 3) return;
            e.setTextureProperty(&texture);
            auto& passes = e.getCurrentTechniqueProperty()->getPassesProperty();
            for (int i = 0; i < passes.getCountProperty(); ++i) {
                passes[i]->Apply();
                device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, contactCount + hullCount, first, count / 3);
            }
            ++drawCalls_;
        };
        draw(0, contactCount, materials_.ContactTexture());
        draw(contactCount, hullCount, materials_.White());
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
    }

    void VehicleRenderer::DrawLampGlows(GraphicsDevice& device, const Sim::VehicleState& state, const Matrix& view, const Matrix& projection)
    {
        if (!glowQuad_ || model_.lamps.empty()) {
            return;
        }
        const Vector3 camera = Matrix::Invert(view).getTranslationProperty();
        auto& e = materials_.Glow();
        e.setViewProperty(view);
        e.setProjectionProperty(projection);
        e.setTextureProperty(&materials_.GlowTexture());
        device.setBlendStateProperty(BlendState::Additive);
        device.setDepthStencilStateProperty(DepthStencilState::DepthRead);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
        for (const auto& lamp : model_.lamps) {
            Vector3 colour(0, 0, 0);
            float size = 0.0f;
            switch (lamp.kind) {
                case CarMaterial::LampHead:
                    if (state.lowBeam) { colour = state.highBeam ? Vector3(0.55f, 0.55f, 0.50f) : Vector3(0.30f, 0.30f, 0.27f); size = 0.55f; }
                    break;
                case CarMaterial::LampTail:
                    if (state.brakeLights) { colour = Vector3(0.55f, 0.04f, 0.02f); size = 0.45f; }
                    else if (state.lowBeam) { colour = Vector3(0.18f, 0.01f, 0.01f); size = 0.32f; }
                    break;
                case CarMaterial::LampIndicator:
                    if (lamp.left ? state.leftIndicatorLit : state.rightIndicatorLit) { colour = Vector3(0.55f, 0.28f, 0.03f); size = 0.30f; }
                    break;
                case CarMaterial::LampReverse:
                    if (state.reverseLights) { colour = Vector3(0.40f, 0.40f, 0.36f); size = 0.30f; }
                    break;
                default:
                    break;
            }
            if (size <= 0.0f) continue;
            const Vector3 worldPos = Vector3::Transform(lamp.position, state.worldMatrix);
            const Vector3 worldNormal = Vector3::TransformNormal(lamp.normal, state.worldMatrix);
            Vector3 toCamera = camera - worldPos;
            const float distance = toCamera.Length();
            if (distance < 1e-3f) continue;
            toCamera = toCamera * (1.0f / distance);
            const float facing = Vector3::Dot(worldNormal, toCamera);
            if (facing < -0.15f) continue;
            const float fade = std::clamp((facing + 0.15f) / 0.5f, 0.0f, 1.0f) * std::clamp(distance / 2.0f, 0.3f, 1.0f);
            const Matrix billboard = Matrix::CreateBillboard(worldPos + worldNormal * 0.04f, camera, Vector3(0, 1, 0), std::nullopt);
            e.setWorldProperty(Matrix::CreateScale(size) * billboard);
            e.setDiffuseColorProperty(colour * fade);
            e.setAlphaProperty(1.0f);
            ApplyAll(e, device, *glowQuad_);
            ++drawCalls_;
        }
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
    }

    void VehicleRenderer::DrawTransparent(GraphicsDevice& device, const Sim::VehicleState& state, const Matrix& view,
                                          const Matrix& projection, const bool mirrored, const bool fromInside)
    {
        glassFromInside_ = fromInside;
        device.setBlendStateProperty(BlendState::AlphaBlend);
        device.setDepthStencilStateProperty(DepthStencilState::DepthRead);
        // Glass is seen from both sides (cockpit camera, mirrors): no culling.
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        GaugePose none;
        for (const auto& gpu : parts_) {
            if (gpu.part->material == CarMaterial::Glass) {
                DrawPart(device, gpu, state, view, projection, none);
            }
        }
        glassFromInside_ = false;
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(mirrored ? RasterizerState::CullClockwise : RasterizerState::CullCounterClockwise);
        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
    }
}
