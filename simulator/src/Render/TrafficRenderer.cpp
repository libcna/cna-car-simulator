#include "CarSim/Render/TrafficRenderer.hpp"

#include "CarSim/Render/PlateRenderer.hpp"

#include <cmath>

namespace CarSim::Render
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    TrafficRenderer::TrafficRenderer(GraphicsDevice& device, VehicleMaterials& materials, const BitmapFont* plateFont)
        : plateFont_(plateFont)
    {
        if (plateFont_) {
            plateAtlas_ = std::make_unique<Image>(plateFont_->AtlasImage());
        }
        // One model per body style and size variant, without cockpits (a cabin block keeps them
        // from looking hollow through the glass).
        for (int b = 0; b < 5; ++b) {
            for (int k = 0; k < kVariantsPerBody; ++k) {
                const Sim::CarStyle style = Sim::CarStyle::Preset(static_cast<Sim::CarStyle::Body>(b), VariantSeed(static_cast<unsigned>(k)));
                renderers_[static_cast<std::size_t>(b * kVariantsPerBody + k)] = std::make_unique<VehicleRenderer>(device, materials, style, nullptr, false);
            }
        }
    }

    Vector3 TrafficRenderer::PaintColour(const int paletteIndex, const Sim::CarStyle::Body body)
    {
        // Common Czech car colours: white, silver, grey, black, dark blue, red, dark green, beige, light blue, brown.
        static const Vector3 palette[10] = {
            Vector3(0.90f, 0.90f, 0.88f), Vector3(0.66f, 0.68f, 0.70f), Vector3(0.36f, 0.37f, 0.39f), Vector3(0.05f, 0.05f, 0.06f),
            Vector3(0.08f, 0.14f, 0.36f), Vector3(0.62f, 0.10f, 0.10f), Vector3(0.10f, 0.28f, 0.16f), Vector3(0.72f, 0.64f, 0.50f),
            Vector3(0.40f, 0.55f, 0.72f), Vector3(0.32f, 0.20f, 0.12f),
        };
        int index = paletteIndex % 10;
        if (body == Sim::CarStyle::Body::Van && index >= 4) {
            index = index % 2;   // vans: mostly white or silver
        }
        return palette[static_cast<std::size_t>(index)];
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

    VehicleRenderer& TrafficRenderer::RendererFor(const Traffic::TrafficVehicle& v)
    {
        const int b = static_cast<int>(v.body);
        const int k = static_cast<int>(v.styleSeed % static_cast<unsigned>(kVariantsPerBody));
        return *renderers_[static_cast<std::size_t>(b * kVariantsPerBody + k)];
    }

    VehicleRenderer& TrafficRenderer::RendererFor(const Sim::CarStyle::Body body, const unsigned seed)
    {
        const int b = static_cast<int>(body);
        const int k = static_cast<int>(seed % static_cast<unsigned>(kVariantsPerBody));
        return *renderers_[static_cast<std::size_t>(b * kVariantsPerBody + k)];
    }

    Sim::VehicleState TrafficRenderer::StateOf(const Traffic::TrafficVehicle& v, const CarModel& model) const
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
        s.steeringWheelAngle = -v.steerAngle * 15.5f;
        const Matrix rotation = Matrix::CreateRotationY(-v.headingRad);
        for (std::size_t i = 0; i < 4; ++i) {
            const Vector3& c = model.wheelCenters[i];
            Sim::VehicleState::WheelPose pose;
            pose.worldCenter = v.position + Vector3::TransformNormal(c, rotation);
            pose.steerAngle = i < 2 ? v.steerAngle : 0.0f;
            pose.spinAngle = v.wheelSpin * (model.style.wheelRadius > 0.0f ? 0.31f / model.style.wheelRadius : 1.0f);   // the traffic model integrates spin at a 0.31 m reference radius
            pose.grounded = true;
            s.wheels[i] = pose;
        }
        return s;
    }

    void TrafficRenderer::DrawParked(GraphicsDevice& device, const std::vector<Map::PlacedVehicle>& cars, const std::vector<std::string>& plates,
                                     const Matrix& view, const Matrix& projection, const BoundingFrustum& frustum, const Vector3& cameraPosition,
                                     const LightingRig& rig, const GroundQuery& ground, const bool mirrored)
    {
        GaugePose none;
        for (std::size_t i = 0; i < cars.size(); ++i) {
            const Map::PlacedVehicle& car = cars[i];
            VehicleRenderer& renderer = RendererFor(car.body, car.seed);
            const CarModel& model = renderer.Model();
            const BoundingSphere sphere(car.position + Vector3(0.0f, 0.5f * model.style.height, 0.0f), 0.5f * model.style.length + 0.8f);
            if (!frustum.Intersects(sphere)) continue;
            const float distance = Vector3::Distance(cameraPosition, car.position);
            if (distance > cullDistanceM) continue;
            const int lod = distance < lod1DistanceM ? 0 : distance < lod2DistanceM ? 1 : 2;

            Sim::VehicleState state;
            state.originPosition = car.position;
            state.worldMatrix = Matrix::CreateRotationY(-car.headingRad) * Matrix::CreateTranslation(car.position);
            state.engineState = Sim::EngineState::Off;
            state.handbrake = true;
            const Matrix rotation = Matrix::CreateRotationY(-car.headingRad);
            for (std::size_t w = 0; w < 4; ++w) {
                Sim::VehicleState::WheelPose pose;
                pose.worldCenter = car.position + Vector3::TransformNormal(model.wheelCenters[w], rotation);
                pose.grounded = true;
                state.wheels[w] = pose;
            }
            renderer.SetPaintOverride(PaintColour(static_cast<int>(car.seed % 10u), car.body));
            renderer.SetPlateTexture(lod < 2 && i < plates.size() && !plates[i].empty() ? PlateTexture(device, plates[i]) : nullptr);
            renderer.DrawOpaque(device, state, view, projection, false, none, mirrored, lod);
            stats_.drawCalls += renderer.DrawCallsLastFrame();
            stats_.drawn += 1;
            if (distance < shadowDistanceM && !mirrored) {
                renderer.DrawShadow(device, state, view, projection, rig.sunDirection, ground);
            }
            if (lod < 2) {
                renderer.DrawTransparent(device, state, view, projection, mirrored);
            }
            renderer.SetPaintOverride(std::nullopt);
            renderer.SetPlateTexture(nullptr);
        }
    }

    void TrafficRenderer::Draw(GraphicsDevice& device, const Traffic::TrafficSystem& traffic, const Matrix& view, const Matrix& projection,
                               const BoundingFrustum& frustum, const Vector3& cameraPosition, const LightingRig& rig,
                               const GroundQuery& ground, const bool mirrored)
    {
        stats_ = TrafficRenderStats{};
        GaugePose none;
        for (const auto& v : traffic.Vehicles()) {
            const BoundingSphere sphere(v.position + Vector3(0.0f, 0.5f * v.heightM, 0.0f), 0.5f * v.lengthM + 0.8f);
            if (!frustum.Intersects(sphere)) {
                continue;
            }
            const float distance = Vector3::Distance(cameraPosition, v.position);
            if (distance > cullDistanceM) {
                continue;
            }
            const int lod = distance < lod1DistanceM ? 0 : distance < lod2DistanceM ? 1 : 2;
            VehicleRenderer& renderer = RendererFor(v);
            const Sim::VehicleState state = StateOf(v, renderer.Model());
            renderer.SetPaintOverride(PaintColour(v.paletteIndex, v.body));
            renderer.SetPlateTexture(lod < 2 ? PlateTexture(device, v.plate) : nullptr);
            renderer.DrawOpaque(device, state, view, projection, false, none, mirrored, lod);
            stats_.drawCalls += renderer.DrawCallsLastFrame();
            if (distance < shadowDistanceM && !mirrored) {
                renderer.DrawShadow(device, state, view, projection, rig.sunDirection, ground);
            }
            if (lod < 2) {
                renderer.DrawTransparent(device, state, view, projection, mirrored);
                renderer.DrawLampGlows(device, state, view, projection);
            }
            renderer.SetPaintOverride(std::nullopt);
            renderer.SetPlateTexture(nullptr);
            ++stats_.drawn;
            if (lod == 0) ++stats_.lod0; else if (lod == 1) ++stats_.lod1; else ++stats_.lod2;
        }
    }
}
