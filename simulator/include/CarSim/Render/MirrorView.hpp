// Rear-view mirror: a RenderTarget2D drawn from a camera at the mirror looking backwards, with
// the image flipped horizontally as a real mirror shows it.
#pragma once

#include "CarSim/Render/Camera.hpp"
#include "CarSim/Sim/Vehicle.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"

#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <memory>

namespace CarSim::Render
{
    class MirrorView
    {
    public:
        /// How far the world is drawn into the mirror. The image is a 200-pixel strip at eleven
        /// degrees: past this nothing can be made out, and drawing it costs a second full world
        /// pass. Kept a little short of the far plane so geometry fades out rather than popping
        /// against it.
        static constexpr float kDrawDistanceM = 300.0f;

        MirrorView(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, int width = 768, int height = 200);

        /// Updates the mirror camera from the vehicle state (call once per frame before Begin).
        void Update(const Sim::VehicleState& state, const Sim::VehicleDefinition& definition);
        /// Wing mirror: a camera at the glass centre (body frame) looking back past the car's
        /// side, turned outboard by twice the glass's yaw as a mirror reflects it.
        void UpdateWing(const Sim::VehicleState& state, const Microsoft::Xna::Framework::Vector3& glassCentre, float glassYaw);

        /// Binds the mirror target and clears it. Draw the scene between Begin and End using
        /// View(), Projection() (horizontally mirrored) and Frustum(); front faces are clockwise
        /// in the mirrored projection, so pass `mirrored = true` to the renderers.
        void Begin(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
        void End(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        [[nodiscard]] const CameraPose& Pose() const { return pose_; }
        [[nodiscard]] const Microsoft::Xna::Framework::Matrix& View() const { return view_; }
        [[nodiscard]] const Microsoft::Xna::Framework::Matrix& Projection() const { return projectionMirrored_; }
        [[nodiscard]] const Microsoft::Xna::Framework::BoundingFrustum& Frustum() const { return frustum_; }
        [[nodiscard]] float Aspect() const { return aspect_; }
        /// Size of the off-screen target, in pixels. The diagnostic overlay reports it because the
        /// mirror's cost scales with it.
        [[nodiscard]] int Width() const { return width_; }
        [[nodiscard]] int Height() const { return height_; }
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* Texture() const;

    private:
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> target_;
        CameraPose pose_;
        Microsoft::Xna::Framework::Matrix view_;
        Microsoft::Xna::Framework::Matrix projectionMirrored_;
        Microsoft::Xna::Framework::BoundingFrustum frustum_;
        float aspect_ = 3.84f;
        int width_ = 0;
        int height_ = 0;
    };
}
