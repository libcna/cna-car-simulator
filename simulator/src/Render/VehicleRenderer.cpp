#include "CarSim/Render/VehicleRenderer.hpp"

#include "CarSim/Render/Image.hpp"
#include "CarSim/Render/ProceduralTextures.hpp"
#include "CarSim/Sim/Units.hpp"

#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"

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

        struct MaterialLook
        {
            Vector3 diffuse{0.8f, 0.8f, 0.8f};
            Vector3 specular{0.1f, 0.1f, 0.1f};
            float specularPower = 16.0f;
            Vector3 emissive{0.0f, 0.0f, 0.0f};
            float alpha = 1.0f;
        };

        MaterialLook LookFor(const CarPart& part, const Sim::VehicleDefinition& def, const Sim::VehicleState& state,
                             const std::optional<Vector3>& paintOverride)
        {
            MaterialLook look;
            switch (part.material) {
                case CarMaterial::Paint:
                    look.diffuse = paintOverride.value_or(def.visual.paintColor);
                    look.specular = Vector3(0.7f, 0.7f, 0.7f);
                    look.specularPower = 48.0f;
                    break;
                case CarMaterial::Glass:
                    look.diffuse = Vector3(0.10f, 0.14f, 0.16f);
                    look.specular = Vector3(1.0f, 1.0f, 1.0f);
                    look.specularPower = 90.0f;
                    look.alpha = 0.42f;
                    break;
                case CarMaterial::BlackTrim:
                    look.diffuse = Vector3(0.05f, 0.05f, 0.055f);
                    look.specular = Vector3(0.15f, 0.15f, 0.15f);
                    look.specularPower = 12.0f;
                    break;
                case CarMaterial::Chrome:
                    look.diffuse = Vector3(0.55f, 0.56f, 0.58f);
                    look.specular = Vector3(1.0f, 1.0f, 1.0f);
                    look.specularPower = 64.0f;
                    break;
                case CarMaterial::Tyre:
                    look.diffuse = Vector3(0.035f, 0.035f, 0.035f);
                    look.specular = Vector3(0.05f, 0.05f, 0.05f);
                    look.specularPower = 8.0f;
                    break;
                case CarMaterial::Rim:
                    look.diffuse = Vector3(0.62f, 0.63f, 0.65f);
                    look.specular = Vector3(0.8f, 0.8f, 0.8f);
                    look.specularPower = 40.0f;
                    break;
                case CarMaterial::Interior:
                    look.diffuse = def.visual.interiorColor;
                    look.specular = Vector3(0.05f, 0.05f, 0.05f);
                    look.specularPower = 6.0f;
                    break;
                case CarMaterial::InteriorLight:
                    look.diffuse = def.visual.interiorColor * 2.6f + Vector3(0.08f, 0.08f, 0.08f);
                    look.specular = Vector3(0.02f, 0.02f, 0.02f);
                    look.specularPower = 4.0f;
                    break;
                case CarMaterial::LampHead:
                    look.diffuse = Vector3(0.75f, 0.78f, 0.80f);
                    look.specular = Vector3(1.0f, 1.0f, 1.0f);
                    look.specularPower = 80.0f;
                    if (state.lowBeam) {
                        look.emissive = state.highBeam ? Vector3(1.0f, 0.98f, 0.90f) : Vector3(0.75f, 0.74f, 0.66f);
                    }
                    break;
                case CarMaterial::LampTail:
                    look.diffuse = Vector3(0.55f, 0.04f, 0.04f);
                    look.specular = Vector3(0.8f, 0.8f, 0.8f);
                    look.specularPower = 60.0f;
                    if (state.brakeLights) {
                        look.emissive = Vector3(0.95f, 0.05f, 0.03f);
                    } else if (state.lowBeam) {
                        look.emissive = Vector3(0.35f, 0.02f, 0.01f);
                    }
                    break;
                case CarMaterial::LampIndicator: {
                    look.diffuse = Vector3(0.85f, 0.45f, 0.08f);
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
                    look.diffuse = Vector3(0.8f, 0.8f, 0.8f);
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
        interiorLit_->setAmbientLightColorProperty(Vector3(0.55f, 0.56f, 0.60f));
        interiorLit_->getDirectionalLight0Property().setDiffuseColorProperty(rig.sunColor * 0.7f);
        interiorLit_->getDirectionalLight0Property().setSpecularColorProperty(rig.sunColor * 0.3f);
        interiorLit_->getDirectionalLight1Property().setDiffuseColorProperty(Vector3(0.22f, 0.24f, 0.28f));
        interiorLit_->getDirectionalLight2Property().setDiffuseColorProperty(Vector3(0.18f, 0.17f, 0.15f));
        interiorLit_->setFogEnabledProperty(false);
        interiorLit_->setTextureEnabledProperty(true);
        interiorLit_->setVertexColorEnabledProperty(false);

        // Shadow: flat dark translucent colour, no lighting; stencil so the projected parts
        // darken each pixel only once.
        shadow_ = std::make_unique<BasicEffect>(device);
        shadow_->setLightingEnabledProperty(false);
        shadow_->setTextureEnabledProperty(true);
        shadow_->setVertexColorEnabledProperty(false);
        shadow_->setDiffuseColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        shadow_->setAlphaProperty(0.42f);
        shadow_->setFogEnabledProperty(false);
        shadowStencil_ = std::make_unique<DepthStencilState>();
        shadowStencil_->setDepthBufferEnableProperty(true);
        shadowStencil_->setDepthBufferWriteEnableProperty(false);
        shadowStencil_->setDepthBufferFunctionProperty(CompareFunction::LessEqual);
        shadowStencil_->setStencilEnableProperty(true);
        shadowStencil_->setStencilFunctionProperty(CompareFunction::Equal);
        shadowStencil_->setReferenceStencilProperty(0);
        shadowStencil_->setStencilPassProperty(StencilOperation::Increment);
        shadowStencil_->setStencilFailProperty(StencilOperation::Keep);
        shadowStencil_->setStencilDepthBufferFailProperty(StencilOperation::Keep);

        white_ = UploadTexture(device, Textures::Solid(4, Color(255, 255, 255, 255)), false);

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

    // ------------------------------------------------------------------ renderer

    VehicleRenderer::VehicleRenderer(GraphicsDevice& device, VehicleMaterials& materials, const Sim::VehicleDefinition& definition)
        : materials_(materials),
          definition_(definition),
          model_(GenerateCar(definition))
    {
        for (const auto& part : model_.parts) {
            GpuPart gpu;
            gpu.part = &part;
            gpu.mesh = GpuMesh::Create(device, part.mesh, VertexLayout::PositionNormalTexture);
            parts_.push_back(std::move(gpu));
        }
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
            case CarPart::Role::NeedleSpeed:
            case CarPart::Role::NeedleRpm:
            case CarPart::Role::NeedleFuel:
            case CarPart::Role::NeedleTemp: {
                // Dials sweep clockwise (seen by the driver) from -135 to +135 degrees for the big
                // gauges and -60 to +60 for the small ones.
                float fraction = 0.0f;
                float sweep = 270.0f;
                switch (part.role) {
                    case CarPart::Role::NeedleSpeed: fraction = gauges.speed; break;
                    case CarPart::Role::NeedleRpm: fraction = gauges.rpm; break;
                    case CarPart::Role::NeedleFuel: fraction = gauges.fuel; sweep = 120.0f; break;
                    default: fraction = gauges.temperature; sweep = 120.0f; break;
                }
                const float angle = Sim::Units::DegToRad(-sweep * 0.5f + sweep * std::clamp(fraction, 0.0f, 1.0f));
                // Needle local +y points up at zero; clockwise for the driver is a negative rotation
                // about the axis pointing towards the driver.
                const Matrix turn = Matrix::CreateFromAxisAngle(part.axis, -angle);
                return turn * Matrix::CreateTranslation(part.pivot) * vehicle;
            }
            default:
                return vehicle;
        }
    }

    bool VehicleRenderer::IsInteriorPart(const CarPart& part)
    {
        return part.role == CarPart::Role::Interior || part.role == CarPart::Role::SteeringWheel ||
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
        const MaterialLook look = LookFor(part, definition_, state, paintOverride_);
        const Matrix world = PartWorld(part, state, gauges);
        const bool interior = IsInteriorPart(part);

        if (part.material == CarMaterial::Paint) {
            auto& e = materials_.Paint();
            e.setWorldProperty(world);
            e.setViewProperty(view);
            e.setProjectionProperty(projection);
            e.setDiffuseColorProperty(look.diffuse);
            e.setEmissiveColorProperty(look.emissive);
            e.setTextureProperty(&materials_.White());
            ApplyAll(e, device, *gpu.mesh);
        } else if (part.material == CarMaterial::Plate || part.material == CarMaterial::Cluster ||
                   (part.name == "mirror_face" && mirrorTexture_ != nullptr)) {
            auto& e = materials_.LitTextured();
            Texture2D* texture = part.material == CarMaterial::Plate ? (plateTexture_ ? plateTexture_ : &materials_.DefaultPlate())
                               : part.material == CarMaterial::Cluster ? (clusterTexture_ ? clusterTexture_ : &materials_.DefaultCluster())
                               : mirrorTexture_;
            device.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
            const bool display = (part.material == CarMaterial::Cluster && clusterTexture_ != nullptr) || part.name == "mirror_face";
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
            e.setWorldProperty(world);
            e.setViewProperty(view);
            e.setProjectionProperty(projection);
            e.setDiffuseColorProperty(look.diffuse);
            e.setEmissiveColorProperty(look.emissive);
            e.setSpecularColorProperty(look.specular);
            e.setSpecularPowerProperty(look.specularPower);
            e.setAlphaProperty(look.alpha);
            e.setTextureProperty(&materials_.White());
            ApplyAll(e, device, *gpu.mesh);
        }
        ++drawCalls_;
    }

    void VehicleRenderer::DrawOpaque(GraphicsDevice& device, const Sim::VehicleState& state, const Matrix& view,
                                     const Matrix& projection, const bool drawInterior, const GaugePose& gauges, const bool mirrored)
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
            if (IsInteriorPart(part) && !drawInterior) {
                continue;
            }
            DrawPart(device, gpu, state, view, projection, gauges);
        }
    }

    void VehicleRenderer::DrawShadow(GraphicsDevice& device, const Sim::VehicleState& state, const Matrix& view, const Matrix& projection,
                                     const Vector3& sunDirection, const Vector3& groundPoint, const Vector3& groundNormal)
    {
        // Plane slightly above the ground so the shadow wins the depth test against the road.
        Vector3 n = groundNormal;
        if (n.LengthSquared() < 1e-6f) n = Vector3(0.0f, 1.0f, 0.0f);
        n.Normalize();
        const Vector3 p = groundPoint + n * 0.02f;
        const Plane plane(n, -Vector3::Dot(n, p));
        Vector3 toSun = sunDirection * -1.0f;
        toSun.Normalize();
        const Matrix shadow = Matrix::CreateShadow(toSun, plane);

        device.setBlendStateProperty(BlendState::AlphaBlend);
        device.setDepthStencilStateProperty(materials_.ShadowStencil());
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        auto& e = materials_.Shadow();
        e.setViewProperty(view);
        e.setProjectionProperty(projection);
        e.setTextureProperty(&materials_.White());
        GaugePose none;
        for (const auto& gpu : parts_) {
            const CarPart& part = *gpu.part;
            if (!gpu.mesh || part.material == CarMaterial::Glass || IsInteriorPart(part)) {
                continue;
            }
            e.setWorldProperty(PartWorld(part, state, none) * shadow);
            ApplyAll(e, device, *gpu.mesh);
        }
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
    }

    void VehicleRenderer::DrawTransparent(GraphicsDevice& device, const Sim::VehicleState& state, const Matrix& view,
                                          const Matrix& projection, const bool mirrored)
    {
        device.setBlendStateProperty(BlendState::AlphaBlend);
        device.setDepthStencilStateProperty(DepthStencilState::DepthRead);
        device.setRasterizerStateProperty(mirrored ? RasterizerState::CullClockwise : RasterizerState::CullCounterClockwise);
        GaugePose none;
        for (const auto& gpu : parts_) {
            if (gpu.part->material == CarMaterial::Glass) {
                DrawPart(device, gpu, state, view, projection, none);
            }
        }
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
    }
}
