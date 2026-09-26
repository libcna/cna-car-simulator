// GPU upload of MeshData into the XNA vertex layouts the renderers understand.
#pragma once

#include "CarSim/Render/MeshData.hpp"

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <memory>

namespace CarSim::Render
{
    /// Vertex layouts accepted by CNA's renderers (selected by stride; see docs/framework-findings.md).
    enum class VertexLayout
    {
        PositionColor,           // 16 bytes, unlit vertex colours (sky, debug lines)
        PositionTexture,         // 20 bytes, unlit textured (sprites in 3D, decals)
        PositionColorTexture,    // 24 bytes, unlit textured with colour tint
        PositionNormalTexture,   // 32 bytes, lit textured (BasicEffect)
        PositionNormalDualTexture // 40 bytes, DualTextureEffect (albedo + lightmap)
    };

    /// 40-byte layout for DualTextureEffect: position, normal, two texture coordinate sets.
    struct VertexPositionNormalDualTexture
    {
        Microsoft::Xna::Framework::Vector3 Position;
        Microsoft::Xna::Framework::Vector3 Normal;
        Microsoft::Xna::Framework::Vector2 TextureCoordinate;
        Microsoft::Xna::Framework::Vector2 TextureCoordinate2;

        [[nodiscard]] static const Microsoft::Xna::Framework::Graphics::VertexDeclaration& Declaration();
    };
    static_assert(sizeof(VertexPositionNormalDualTexture) == 40, "dual-texture layout must be 40 bytes");

    /// Owns a vertex and an index buffer plus the draw parameters for one triangle list.
    class GpuMesh
    {
    public:
        struct SubmissionTotals
        {
            long long draws = 0;
            long long triangles = 0;
        };

        GpuMesh() = default;
        GpuMesh(const GpuMesh&) = delete;
        GpuMesh& operator=(const GpuMesh&) = delete;

        /// Uploads `mesh` with the chosen layout. Returns nullptr for an empty mesh.
        [[nodiscard]] static std::unique_ptr<GpuMesh> Create(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                                                             const MeshData& mesh, VertexLayout layout,
                                                             const GpuMesh* indexSource = nullptr);

        /// Binds the buffers and issues the indexed draw. The caller applies the effect pass.
        void Draw(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const;

        /// Per-render-thread 3D submission accounting for project benchmarks. Direct indexed
        /// draws call RecordSubmission too; SpriteBatch UI work is intentionally separate.
        static void ResetSubmissions();
        static void RecordSubmission(int triangles);
        [[nodiscard]] static SubmissionTotals Submissions();

        [[nodiscard]] int VertexCount() const { return vertexCount_; }
        [[nodiscard]] int PrimitiveCount() const { return primitiveCount_; }
        [[nodiscard]] VertexLayout Layout() const { return layout_; }
        [[nodiscard]] const BoundingBox& Bounds() const { return bounds_; }
        [[nodiscard]] const BoundingSphere& Sphere() const { return sphere_; }
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::VertexBuffer* VertexBufferPtr() const { return vertices_.get(); }
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::IndexBuffer* IndexBufferPtr() const { return indices_.get(); }

    private:
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::VertexBuffer> vertices_;
        std::shared_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer> indices_;
        int vertexCount_ = 0;
        int primitiveCount_ = 0;
        VertexLayout layout_ = VertexLayout::PositionNormalTexture;
        BoundingBox bounds_;
        BoundingSphere sphere_;
    };
}
