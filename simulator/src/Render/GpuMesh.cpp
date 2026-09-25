#include "CarSim/Render/GpuMesh.hpp"

#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace CarSim::Render
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    const VertexDeclaration& VertexPositionNormalDualTexture::Declaration()
    {
        static const VertexDeclaration declaration(40, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(32, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1),
        });
        return declaration;
    }

    namespace
    {
        template <typename TVertex>
        std::unique_ptr<VertexBuffer> Upload(GraphicsDevice& device, const VertexDeclaration& declaration,
                                             const std::vector<TVertex>& data)
        {
            auto buffer = std::make_unique<VertexBuffer>(device, declaration, static_cast<int>(data.size()),
                                                         BufferUsage::WriteOnly);
            buffer->SetData(data.data(), static_cast<int>(data.size()));
            return buffer;
        }
    }

    std::unique_ptr<GpuMesh> GpuMesh::Create(GraphicsDevice& device, const MeshData& mesh, const VertexLayout layout,
                                            const GpuMesh* indexSource)
    {
        if (mesh.Empty()) {
            return nullptr;
        }
        auto gpu = std::unique_ptr<GpuMesh>(new GpuMesh());
        gpu->layout_ = layout;
        gpu->vertexCount_ = static_cast<int>(mesh.vertices.size());
        gpu->primitiveCount_ = static_cast<int>(mesh.TriangleCount());
        gpu->bounds_ = mesh.Bounds();
        gpu->sphere_ = mesh.BoundingSphereOf();

        switch (layout) {
            case VertexLayout::PositionColor: {
                std::vector<VertexPositionColor> v;
                v.reserve(mesh.vertices.size());
                for (const auto& m : mesh.vertices) {
                    v.emplace_back(m.position, m.color);
                }
                gpu->vertices_ = Upload(device, VertexPositionColor::getVertexDeclarationStatic(), v);
                break;
            }
            case VertexLayout::PositionTexture: {
                std::vector<VertexPositionTexture> v;
                v.reserve(mesh.vertices.size());
                for (const auto& m : mesh.vertices) {
                    v.emplace_back(m.position, m.uv);
                }
                gpu->vertices_ = Upload(device, VertexPositionTexture::getVertexDeclarationStatic(), v);
                break;
            }
            case VertexLayout::PositionColorTexture: {
                std::vector<VertexPositionColorTexture> v;
                v.reserve(mesh.vertices.size());
                for (const auto& m : mesh.vertices) {
                    v.emplace_back(m.position, m.color, m.uv);
                }
                gpu->vertices_ = Upload(device, VertexPositionColorTexture::getVertexDeclarationStatic(), v);
                break;
            }
            case VertexLayout::PositionNormalTexture: {
                std::vector<VertexPositionNormalTexture> v;
                v.reserve(mesh.vertices.size());
                for (const auto& m : mesh.vertices) {
                    v.emplace_back(m.position, m.normal, m.uv);
                }
                gpu->vertices_ = Upload(device, VertexPositionNormalTexture::getVertexDeclarationStatic(), v);
                break;
            }
            case VertexLayout::PositionNormalDualTexture: {
                std::vector<VertexPositionNormalDualTexture> v;
                v.reserve(mesh.vertices.size());
                for (const auto& m : mesh.vertices) {
                    v.push_back(VertexPositionNormalDualTexture{m.position, m.normal, m.uv, m.uv2});
                }
                gpu->vertices_ = Upload(device, VertexPositionNormalDualTexture::Declaration(), v);
                break;
            }
        }

        if (indexSource) {
            // Alternate vertex declarations for the very same triangles can reuse the index
            // allocation. Callers must pass the same MeshData; the counts catch most mistakes.
            if (indexSource->vertexCount_ != gpu->vertexCount_ || indexSource->primitiveCount_ != gpu->primitiveCount_) {
                throw std::invalid_argument("shared-index mesh dimensions do not match");
            }
            gpu->indices_ = indexSource->indices_;
        } else {
            gpu->indices_ = std::make_shared<IndexBuffer>(device, IndexElementSize::ThirtyTwoBits,
                                                          static_cast<int>(mesh.indices.size()), BufferUsage::WriteOnly);
            gpu->indices_->SetData(mesh.indices.data(), static_cast<int>(mesh.indices.size()));
        }
        return gpu;
    }

    void GpuMesh::Draw(GraphicsDevice& device) const
    {
        if (!vertices_ || !indices_ || primitiveCount_ == 0) {
            return;
        }
        device.SetVertexBuffer(vertices_.get());
        device.setIndicesProperty(indices_.get());
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, vertexCount_, 0, primitiveCount_);
    }
}
