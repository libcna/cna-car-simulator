#include "CarSim/Render/WindscreenRain.hpp"

#include "CarSim/Render/Image.hpp"

#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>

namespace CarSim::Render
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    namespace
    {
        constexpr float kPi = 3.14159265f;
        constexpr float kBladeInner = 0.22f;   // blade covers this share of the arm outwards

        float Variation(const unsigned serial, const unsigned salt)
        {
            unsigned n = serial * 747796405u + salt * 2891336453u;
            n = (n ^ (n >> 16)) * 2246822519u;
            n ^= n >> 13;
            return static_cast<float>(n % 10000u) / 10000.0f;
        }
    }

    void WindscreenRain::SetGlass(const float widthM, const float heightM)
    {
        width_ = std::max(0.3f, widthM);
        height_ = std::max(0.2f, heightM);
    }

    Vector2 WindscreenRain::Pivot(const int wiper) const
    {
        // Tandem wipers for a left-hand-drive car: pivots below the driver and near the middle,
        // parked lying to the right, sweeping up and over to the left.
        return Vector2(width_ * (wiper == 0 ? 0.16f : 0.60f), -0.03f);
    }

    float WindscreenRain::ArmLength(const int wiper) const
    {
        return std::min(width_ * (wiper == 0 ? 0.60f : 0.52f), height_ * (wiper == 0 ? 1.05f : 0.95f));
    }

    float WindscreenRain::BladeAngle(const float wiperPosition)
    {
        return 0.06f + std::clamp(wiperPosition, 0.0f, 1.0f) * (1.95f - 0.06f);
    }

    bool WindscreenRain::Swept(const Vector2& p, const float from, const float to) const
    {
        const float lo = std::min(from, to) - 0.02f;
        const float hi = std::max(from, to) + 0.02f;
        for (int wiper = 0; wiper < 2; ++wiper) {
            const Vector2 d = p - Pivot(wiper);
            const float r = d.Length();
            const float arm = ArmLength(wiper);
            if (r < arm * kBladeInner || r > arm) continue;
            const float a = std::atan2(d.Y, d.X);
            if (a >= lo && a <= hi) return true;
        }
        return false;
    }

    void WindscreenRain::Update(const float dt, const float rain, const float speedMs, const float wiperPosition)
    {
        if (dt <= 0.0f) return;
        const float step = std::min(dt, 0.1f);

        // Airflow over the glass pushes drops up the slope above town speeds; below that the big
        // ones run down under their own weight. Without rain they slowly dry off.
        const float windUp = std::max(0.0f, std::fabs(speedMs) - 11.0f) * 0.012f;
        for (WindscreenDrop& d : drops_) {
            d.age += step;
            float vy = windUp * (d.radiusM / 0.004f);
            if (d.radiusM > 0.0048f && windUp <= 0.0f) vy = -0.03f * (d.radiusM / 0.005f);
            d.position.Y += vy * step;
            if (rain <= 0.02f) d.opacity -= step / 45.0f;
        }

        // The wipers clear whatever the blades crossed since the last step.
        const float fromAngle = BladeAngle(lastWiper_);
        const float toAngle = BladeAngle(wiperPosition);
        const bool moving = std::fabs(wiperPosition - lastWiper_) > 1e-5f;
        lastWiper_ = wiperPosition;
        drops_.erase(std::remove_if(drops_.begin(), drops_.end(),
                                    [&](const WindscreenDrop& d) {
                                        return d.opacity <= 0.0f || d.position.Y > height_ + 0.02f || d.position.Y < -0.02f ||
                                               (moving && Swept(d.position, fromAngle, toAngle));
                                    }),
                     drops_.end());

        if (rain <= 0.02f) {
            spawnCredit_ = 0.0f;
            return;
        }
        // More drops strike a moving screen: it sweeps through the falling rain.
        spawnCredit_ += step * rain * (45.0f + 4.0f * std::fabs(speedMs));
        while (spawnCredit_ >= 1.0f) {
            spawnCredit_ -= 1.0f;
            const unsigned serial = serial_++;
            WindscreenDrop d;
            d.position = Vector2(Variation(serial, 1u) * width_, Variation(serial, 2u) * height_);
            d.radiusM = 0.0022f + 0.0022f * (Variation(serial, 3u) + Variation(serial, 4u));
            d.opacity = 0.55f + 0.45f * Variation(serial, 5u);
            if (static_cast<int>(drops_.size()) < kMaxDrops) {
                drops_.push_back(d);
            } else {
                // A full screen: a new drop lands on (and merges with) an old one.
                WindscreenDrop& old = drops_[serial % drops_.size()];
                old.radiusM = std::min(0.0075f, std::sqrt(old.radiusM * old.radiusM + d.radiusM * d.radiusM));
            }
        }
    }

    WindscreenRainRenderer::WindscreenRainRenderer(GraphicsDevice& device)
    {
        // A drop refracts the brighter sky above into its lower half and shows a dark rim: a
        // light crescent low, a thin dark ring, a small highlight high.
        Image drop(64, 64, Color(255, 255, 255, 0));
        drop.Generate([](int, int, const float u, const float v) {
            const float x = u * 2.0f - 1.0f;
            const float y = v * 2.0f - 1.0f;
            const float r = std::sqrt(x * x + y * y);
            if (r > 1.0f) return Color(255, 255, 255, 0);
            const float rim = std::clamp((r - 0.72f) / 0.28f, 0.0f, 1.0f);
            const float lower = std::clamp(y, 0.0f, 1.0f);   // v grows downwards
            const float spot = std::clamp(1.0f - std::sqrt((x + 0.3f) * (x + 0.3f) + (y + 0.35f) * (y + 0.35f)) / 0.22f, 0.0f, 1.0f);
            const float light = std::clamp(0.55f + 0.45f * lower + 0.6f * spot - 0.55f * rim, 0.0f, 1.0f);
            const float alpha = std::clamp(0.45f + 0.4f * rim + 0.25f * lower + 0.4f * spot, 0.0f, 1.0f) *
                                std::clamp((1.0f - r) / 0.08f, 0.0f, 1.0f);
            const int g = static_cast<int>(light * 255.0f);
            return Color(g, g, std::min(255, g + 6), static_cast<int>(alpha * 255.0f));
        });
        dropTexture_ = UploadTexture(device, drop, true);
        Image white(4, 4, Color(255, 255, 255, 255));
        white_ = UploadTexture(device, white, false);

        constexpr int capacity = (WindscreenRain::kMaxDrops + 8) * 6;
        vertices_ = std::make_unique<VertexBuffer>(device, VertexPositionColorTexture::getVertexDeclarationStatic(), capacity,
                                                   BufferUsage::WriteOnly);
        std::vector<std::uint32_t> identity(capacity);
        std::iota(identity.begin(), identity.end(), 0u);
        indices_ = std::make_unique<IndexBuffer>(device, IndexElementSize::ThirtyTwoBits, capacity, BufferUsage::WriteOnly);
        indices_->SetData(identity.data(), capacity);

        effect_ = std::make_unique<BasicEffect>(device);
        effect_->setLightingEnabledProperty(false);
        effect_->setTextureEnabledProperty(true);
        effect_->setVertexColorEnabledProperty(true);
        effect_->setFogEnabledProperty(false);
    }

    void WindscreenRainRenderer::Draw(GraphicsDevice& device, const Matrix& view, const Matrix& projection, const Matrix& world,
                                      const std::array<Vector3, 4>& glass, const float wiperPosition, const Vector3& light)
    {
        const Vector3& bl = glass[0];
        const Vector3& br = glass[1];
        const Vector3& tr = glass[2];
        const Vector3& tl = glass[3];
        const float width = Vector3::Distance(bl, br);
        const float height = Vector3::Distance((bl + br) * 0.5f, (tl + tr) * 0.5f);
        if (width < 0.1f || height < 0.1f) return;
        rain_.SetGlass(width, height);

        Vector3 normal = Vector3::Cross(br - bl, tl - bl);
        normal.Normalize();
        if (normal.Y < 0.0f) normal = -normal;   // outwards: up and forwards
        // Glass metres -> body frame, following the trapezoid (the top edge is narrower).
        const auto at = [&](const float x, const float y, const float lift) {
            const float u = std::clamp(x / width, -0.2f, 1.2f);
            const float v = y / height;
            const Vector3 bottom = Vector3::Lerp(bl, br, u);
            const Vector3 top = Vector3::Lerp(tl, tr, u);
            return Vector3::Lerp(bottom, top, v) + normal * lift;
        };
        Vector3 across = br - bl;
        across.Normalize();
        Vector3 up = Vector3::Cross(normal, across);
        if (Vector3::Dot(up, tl - bl) < 0.0f) up = -up;

        std::vector<VertexPositionColorTexture> verts;
        verts.reserve((rain_.Drops().size() + 8) * 6);
        const auto quad = [&](const Vector3& centre, const Vector3& ax, const Vector3& ay, const Color& colour) {
            const Vector3 a = centre - ax - ay, b = centre + ax - ay, c = centre + ax + ay, d = centre - ax + ay;
            verts.emplace_back(a, colour, Vector2(0.0f, 1.0f));
            verts.emplace_back(b, colour, Vector2(1.0f, 1.0f));
            verts.emplace_back(c, colour, Vector2(1.0f, 0.0f));
            verts.emplace_back(a, colour, Vector2(0.0f, 1.0f));
            verts.emplace_back(c, colour, Vector2(1.0f, 0.0f));
            verts.emplace_back(d, colour, Vector2(0.0f, 0.0f));
        };
        const auto channel = [](const float x) { return static_cast<int>(std::clamp(x, 0.0f, 1.0f) * 255.0f); };

        // Wiper arms and blades, lying on the glass.
        const float angle = WindscreenRain::BladeAngle(wiperPosition);
        const Vector2 dir(std::cos(angle), std::sin(angle));
        const Color blade(channel(0.06f * light.X + 0.02f), channel(0.06f * light.Y + 0.02f), channel(0.065f * light.Z + 0.02f), 255);
        for (int wiper = 0; wiper < 2; ++wiper) {
            const Vector2 pivot = rain_.Pivot(wiper);
            const float arm = rain_.ArmLength(wiper);
            const auto segment = [&](const float r0, const float r1, const float halfWidth, const float lift) {
                const Vector2 p0 = pivot + dir * r0;
                const Vector2 p1 = pivot + dir * r1;
                const Vector3 a = at(p0.X, p0.Y, lift);
                const Vector3 b = at(p1.X, p1.Y, lift);
                Vector3 along = b - a;
                const float len = along.Length();
                if (len < 1e-4f) return;
                along.Normalize();
                Vector3 side = Vector3::Cross(normal, along);
                side.Normalize();
                quad((a + b) * 0.5f, along * (len * 0.5f), side * halfWidth, blade);
            };
            segment(0.0f, arm, 0.006f, 0.018f);                        // the arm, standing off the glass
            segment(arm * kBladeInner, arm, 0.009f, 0.010f);                 // the rubber blade
        }

        const std::size_t bladeVerts = verts.size();
        for (const WindscreenDrop& d : rain_.Drops()) {
            const Color colour(channel(light.X), channel(light.Y), channel(light.Z), channel(d.opacity));
            quad(at(d.position.X, d.position.Y, 0.006f), across * d.radiusM, up * (d.radiusM * 1.1f), colour);
        }
        for (auto& v : verts) v.Position = Vector3::Transform(v.Position, world);
        if (verts.empty()) return;
        vertices_->SetData(verts.data(), static_cast<int>(verts.size()));

        device.setBlendStateProperty(BlendState::NonPremultiplied);
        device.setDepthStencilStateProperty(DepthStencilState::DepthRead);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
        device.SetVertexBuffer(vertices_.get());
        device.setIndicesProperty(indices_.get());
        effect_->setWorldProperty(Matrix::getIdentityProperty());
        effect_->setViewProperty(view);
        effect_->setProjectionProperty(projection);
        auto& passes = effect_->getCurrentTechniqueProperty()->getPassesProperty();
        const auto draw = [&](Texture2D* texture, const int first, const int count) {
            if (count <= 0) return;
            effect_->setTextureProperty(texture);
            for (int i = 0; i < passes.getCountProperty(); ++i) {
                passes[i]->Apply();
                device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, first, count, first, count / 3);
            }
        };
        draw(white_.get(), 0, static_cast<int>(bladeVerts));
        draw(dropTexture_.get(), static_cast<int>(bladeVerts), static_cast<int>(verts.size() - bladeVerts));
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
    }
}
