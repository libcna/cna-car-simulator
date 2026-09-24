// Ground contact shadow and projected headlamp illumination for the player vehicle.
// Extracted without changing the silhouette, beam or render-state algorithms.
#include "CarSim/Render/VehicleRenderer.hpp"

#include "CarSim/Render/ShadowGeometry.hpp"

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
#include <utility>
#include <vector>

namespace CarSim::Render
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    /// Smoothstep on an unclamped argument; used to shape the headlamp beam's cut-off.
    inline float SmoothStepf(const float t)
    {
        const float c = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        return c * c * (3.0f - 2.0f * c);
    }

    void VehicleRenderer::DrawShadow(GraphicsDevice& device, const Sim::VehicleState& state, const Matrix& view, const Matrix& projection,
                                     const Vector3& sunDirection, const GroundQuery& ground)
    {
        if (state.flightMode) return;
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


    float HeadlampBeamFalloff(const float along, const float across, const bool highBeam)
    {
        const float bias = highBeam ? 0.0f : 0.20f;
        const float span = across > bias ? (1.0f - bias) : (1.0f + bias);
        const float u = std::clamp((across - bias) / std::max(0.05f, span), -1.0f, 1.0f);
        const float lateral = std::max(0.0f, 1.0f - u * u);
        const float rise = SmoothStepf(along / 0.025f);
        const float cutoff = 1.0f - SmoothStepf((along - (highBeam ? 0.83f : 0.75f)) /
                                                  (highBeam ? 0.17f : 0.25f));
        return (highBeam ? lateral * lateral : lateral) * rise * cutoff;
    }

    void VehicleRenderer::DrawHeadlightPool(GraphicsDevice& device, const Sim::VehicleState& state, const Matrix& view,
                                            const Matrix& projection, const GroundQuery& ground, const float intensity)
    {
        if (!poolVertices_ || !poolIndices_ || (!state.flightMode && !state.lowBeam) || intensity <= 0.01f) {
            return;
        }
        // Two separate emitters spread from the headlamp positions. The left dipped beam cuts
        // off sooner to spare oncoming traffic; the right one carries light onto the verge.
        // Their smooth overlap avoids the flat white trapezoid of the former single ground pool.
        const bool high = state.highBeam;
        const bool flight = state.flightMode;
        const float nearM = flight ? -2.0f : 1.2f;
        const Vector3 origin = state.originPosition;
        Vector3 forward = state.worldMatrix.getForwardProperty();
        forward.Y = 0.0f;
        if (forward.LengthSquared() < 1e-6f) return;
        forward.Normalize();
        const Vector3 right(-forward.Z, 0.0f, forward.X);
        const Vector3 warm = flight ? Vector3(0.43f, 0.44f, 0.42f) :
                             high ? Vector3(0.46f, 0.46f, 0.43f) : Vector3(0.31f, 0.30f, 0.27f);

        std::vector<VertexPositionColorTexture> verts;
        verts.reserve(static_cast<std::size_t>(kPoolVertexCapacity));
        for (int lamp = 0; lamp < (flight ? 1 : 2); ++lamp) {
            const float farM = flight ? (high ? 300.0f : 190.0f) : high ? 280.0f : lamp == 0 ? 130.0f : 175.0f;
            const float lampX = flight ? 0.0f : lamp == 0 ? -0.60f : 0.60f;
            const auto sample = [&](const int j, const int i) {
                const float t = static_cast<float>(j) / kPoolCellsAlong;
                const float u = static_cast<float>(i) / kPoolCellsAcross * 2.0f - 1.0f;
                const float distance = nearM + t * (farM - nearM);
                const float halfWidth = flight ? 2.5f + std::max(0.0f, distance) * (high ? 0.12f : 0.18f) :
                                        1.0f + distance * (high ? 0.095f : 0.14f);
                const float kick = high || flight ? 0.0f : distance * 0.008f;
                const Vector3 p = origin + forward * distance + right * (lampX + kick + u * halfWidth);
                const float y = (ground.height ? ground.height(p.X, p.Z) : origin.Y) + 0.05f;
                const float lateral = std::max(0.0f, 1.0f - u * u);
                const float flightFalloff = lateral * lateral *
                    (1.0f - SmoothStepf((t - 0.82f) / 0.18f)) * SmoothStepf(t / 0.04f);
                const float f = (flight ? flightFalloff : HeadlampBeamFalloff(t, u, high)) * intensity;
                const Vector3 c = warm * f;
                const Color colour(static_cast<int>(c.X * 255.0f), static_cast<int>(c.Y * 255.0f), static_cast<int>(c.Z * 255.0f), 255);
                return VertexPositionColorTexture(Vector3(p.X, y, p.Z), colour, Vector2(0.5f, 0.5f));
            };
            std::vector<VertexPositionColorTexture> grid;
            grid.reserve(static_cast<std::size_t>((kPoolCellsAlong + 1) * (kPoolCellsAcross + 1)));
            for (int j = 0; j <= kPoolCellsAlong; ++j) {
                for (int i = 0; i <= kPoolCellsAcross; ++i) grid.push_back(sample(j, i));
            }
            const auto at = [&](const int j, const int i) -> const VertexPositionColorTexture& {
                return grid[static_cast<std::size_t>(j * (kPoolCellsAcross + 1) + i)];
            };
            for (int j = 0; j < kPoolCellsAlong; ++j) {
                for (int i = 0; i < kPoolCellsAcross; ++i) {
                    const auto& a = at(j, i);
                    const auto& b = at(j, i + 1);
                    const auto& c = at(j + 1, i + 1);
                    const auto& d = at(j + 1, i);
                    verts.push_back(a); verts.push_back(b); verts.push_back(c);
                    verts.push_back(a); verts.push_back(c); verts.push_back(d);
                }
            }
        }
        poolVertices_->SetData(verts.data(), static_cast<int>(verts.size()));

        device.setBlendStateProperty(BlendState::Additive);
        device.setDepthStencilStateProperty(DepthStencilState::DepthRead);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
        device.SetVertexBuffer(poolVertices_.get());
        device.setIndicesProperty(poolIndices_.get());
        // The shadow effect is the only unlit vertex-colour effect the car owns; it normally runs
        // with a black diffuse (shadows), so the pool borrows it with a white one.
        auto& e = materials_.Shadow();
        e.setViewProperty(view);
        e.setProjectionProperty(projection);
        e.setWorldProperty(Matrix::getIdentityProperty());
        e.setTextureProperty(&materials_.White());
        e.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        auto& passes = e.getCurrentTechniqueProperty()->getPassesProperty();
        for (int i = 0; i < passes.getCountProperty(); ++i) {
            passes[i]->Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, static_cast<int>(verts.size()), 0, static_cast<int>(verts.size()) / 3);
        }
        ++drawCalls_;
        e.setDiffuseColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
    }

}
