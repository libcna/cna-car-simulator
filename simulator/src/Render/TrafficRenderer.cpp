#include "CarSim/Render/TrafficRenderer.hpp"

#include "CarSim/Render/PlateRenderer.hpp"

#include <cmath>

namespace CarSim::Render
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    TrafficRenderer::TrafficRenderer(GraphicsDevice& device, VehicleRenderer& renderer, const Sim::VehicleDefinition& definition, const BitmapFont* plateFont)
        : renderer_(renderer), definition_(definition), plateFont_(plateFont)
    {
        (void)device;
        if (plateFont_) {
            plateAtlas_ = std::make_unique<Image>(plateFont_->AtlasImage());
        }
    }

    Vector3 TrafficRenderer::PaintColour(const int paletteIndex)
    {
        // Common Czech car colours: white, silver, grey, black, dark blue, red, dark green, beige.
        static const Vector3 palette[8] = {
            Vector3(0.90f, 0.90f, 0.88f), Vector3(0.66f, 0.68f, 0.70f), Vector3(0.36f, 0.37f, 0.39f), Vector3(0.05f, 0.05f, 0.06f),
            Vector3(0.08f, 0.14f, 0.36f), Vector3(0.62f, 0.10f, 0.10f), Vector3(0.10f, 0.28f, 0.16f), Vector3(0.72f, 0.64f, 0.50f),
        };
        return palette[static_cast<std::size_t>(paletteIndex % 8)];
    }

    Texture2D* TrafficRenderer::PlateTexture(GraphicsDevice& device, const std::string& text)
    {
        auto it = plates_.find(text);
        if (it != plates_.end()) {
            return it->second.get();
        }
        if (!plateFont_ || !plateAtlas_) {
            return nullptr;
        }
        auto texture = UploadTexture(device, PlateRenderer::Render(text, *plateFont_, *plateAtlas_), true);
        Texture2D* raw = texture.get();
        plates_.emplace(text, std::move(texture));
        return raw;
    }

    Sim::VehicleState TrafficRenderer::StateOf(const Traffic::TrafficVehicle& v) const
    {
        Sim::VehicleState s;
        s.originPosition = v.position;
        s.worldMatrix = v.WorldMatrix();
        s.velocity = v.Velocity();
        s.speedMs = v.speed;
        s.speedKmh = v.speed * 3.6f;
        s.engineState = Sim::EngineState::Running;
        s.ignitionOn = true;
        s.engineRpm = 900.0f + v.speed * 120.0f;
        s.brakeLights = v.brakeLights;
        const bool blink = std::fmod(v.age, 0.8f) < 0.4f;
        s.leftIndicatorLit = v.indicatorLeft && blink;
        s.rightIndicatorLit = v.indicatorRight && blink;
        s.lowBeam = true;   // daytime running lights
        s.steeringWheelAngle = -v.steerAngle * definition_.steering.steeringRatio;
        const Matrix rotation = Matrix::CreateRotationY(-v.headingRad);
        for (std::size_t i = 0; i < 4 && i < definition_.wheels.size(); ++i) {
            const auto& w = definition_.wheels[i];
            Sim::VehicleState::WheelPose pose;
            pose.worldCenter = v.position + Vector3::TransformNormal(Vector3(w.position.X, w.radiusM, w.position.Z), rotation);
            pose.steerAngle = w.steered ? v.steerAngle : 0.0f;
            pose.spinAngle = v.wheelSpin;
            pose.grounded = true;
            s.wheels[i] = pose;
        }
        return s;
    }

    void TrafficRenderer::Draw(GraphicsDevice& device, const Traffic::TrafficSystem& traffic, const Matrix& view, const Matrix& projection,
                               const BoundingFrustum& frustum, const Vector3& cameraPosition, const LightingRig& rig,
                               const std::function<Vector3(const Vector3&)>& groundNormal, const bool mirrored)
    {
        drawn_ = 0;
        GaugePose none;
        for (const auto& v : traffic.Vehicles()) {
            const BoundingSphere sphere(v.position + Vector3(0.0f, 0.8f, 0.0f), 2.8f);
            if (!frustum.Intersects(sphere)) {
                continue;
            }
            const float distance = Vector3::Distance(cameraPosition, v.position);
            if (distance > 900.0f) {
                continue;
            }
            const Sim::VehicleState state = StateOf(v);
            renderer_.SetPaintOverride(PaintColour(v.paletteIndex));
            renderer_.SetPlateTexture(PlateTexture(device, v.plate));
            renderer_.DrawOpaque(device, state, view, projection, false, none, mirrored);
            if (distance < 120.0f && !mirrored) {
                renderer_.DrawShadow(device, state, view, projection, rig.sunDirection, v.position, groundNormal(v.position));
            }
            renderer_.DrawTransparent(device, state, view, projection, mirrored);
            ++drawn_;
        }
        renderer_.SetPaintOverride(std::nullopt);
        renderer_.SetPlateTexture(nullptr);
    }
}
