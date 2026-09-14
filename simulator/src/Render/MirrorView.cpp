#include "CarSim/Render/MirrorView.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

namespace CarSim::Render
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    MirrorView::MirrorView(GraphicsDevice& device, const int width, const int height)
        : frustum_(Matrix::getIdentityProperty())
    {
        target_ = std::make_unique<RenderTarget2D>(device, width, height, false, SurfaceFormat::Color, DepthFormat::Depth24Stencil8, 0,
                                                   RenderTargetUsage::DiscardContents);
        aspect_ = static_cast<float>(width) / static_cast<float>(height);
    }

    void MirrorView::Update(const Sim::VehicleState& state, const Sim::VehicleDefinition& definition)
    {
        const Matrix& world = state.worldMatrix;
        const Vector3 eye = Vector3::Transform(definition.visual.mirrorCenter, world);
        // Look straight back along the body with a slight downward tilt.
        const Vector3 lookLocal = definition.visual.mirrorCenter + Vector3(0.0f, -0.35f, 12.0f);
        const Vector3 target = Vector3::Transform(lookLocal, world);
        pose_.position = eye;
        pose_.target = target;
        pose_.up = world.getUpProperty();
        pose_.fieldOfViewDeg = 11.0f;   // vertical; the wide target gives ~40 degrees horizontally
        pose_.nearPlane = 0.5f;
        pose_.farPlane = 1500.0f;
        view_ = pose_.View();
        const Matrix projection = pose_.Projection(aspect_);
        projectionMirrored_ = projection * Matrix::CreateScale(-1.0f, 1.0f, 1.0f);
        frustum_ = BoundingFrustum(view_ * projection);
    }

    void MirrorView::Begin(GraphicsDevice& device)
    {
        device.SetRenderTarget(target_.get());
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil, Color(120, 160, 210, 255), 1.0f, 0);
    }

    void MirrorView::End(GraphicsDevice& device)
    {
        device.SetRenderTarget(nullptr);
    }

    Texture2D* MirrorView::Texture() const
    {
        return target_.get();
    }
}
