// CPU RGBA8 image with procedural noise helpers and GPU upload with a mip chain.
#pragma once

#include "CarSim/Core/Noise.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace CarSim::Render
{
    using Microsoft::Xna::Framework::Color;

    struct Rgb
    {
        float r = 0.0f, g = 0.0f, b = 0.0f;
        [[nodiscard]] static Rgb FromBytes(int r8, int g8, int b8) { return {r8 / 255.0f, g8 / 255.0f, b8 / 255.0f}; }
        [[nodiscard]] Rgb operator*(float s) const { return {r * s, g * s, b * s}; }
        [[nodiscard]] Rgb operator+(const Rgb& o) const { return {r + o.r, g + o.g, b + o.b}; }
    };

    [[nodiscard]] Rgb Lerp(const Rgb& a, const Rgb& b, float t);

    class Image
    {
    public:
        Image() = default;
        Image(int width, int height, const Color& fill = Color(0, 0, 0, 255));

        [[nodiscard]] int Width() const { return width_; }
        [[nodiscard]] int Height() const { return height_; }
        [[nodiscard]] const std::vector<Color>& Pixels() const { return pixels_; }
        [[nodiscard]] std::vector<Color>& Pixels() { return pixels_; }

        [[nodiscard]] const Color& At(int x, int y) const { return pixels_[static_cast<std::size_t>(y) * width_ + x]; }
        [[nodiscard]] Color& At(int x, int y) { return pixels_[static_cast<std::size_t>(y) * width_ + x]; }
        /// Wrapping fetch (tileable textures).
        [[nodiscard]] const Color& Wrap(int x, int y) const;

        void Set(int x, int y, const Rgb& rgb, float alpha = 1.0f);
        void Fill(const Color& color);
        /// Calls `f(x, y, u, v)` for every pixel and stores the returned colour (u, v in 0..1).
        void Generate(const std::function<Color(int, int, float, float)>& f);
        /// Draws an axis-aligned filled rectangle (pixel coordinates, end exclusive).
        void FillRect(int x0, int y0, int x1, int y1, const Color& color);
        /// Draws a filled disc.
        void FillCircle(float cx, float cy, float radius, const Color& color);
        /// Draws a filled triangle (pixel coordinates).
        void FillTriangle(float x0, float y0, float x1, float y1, float x2, float y2, const Color& color);
        /// Draws a filled convex or concave polygon (even-odd rule).
        void FillPolygon(const std::vector<std::pair<float, float>>& points, const Color& color);
        /// Draws a line of the given thickness.
        void DrawLine(float x0, float y0, float x1, float y1, float thickness, const Color& color);
        /// Draws a ring (annulus) between two radii.
        void FillRing(float cx, float cy, float outerRadius, float innerRadius, const Color& color);
        /// Alpha-blends `color` over the pixel (colour has straight alpha).
        void Blend(int x, int y, const Color& color);
        /// Blends `top` over this image at offset (x, y) using top's alpha.
        void BlendOver(const Image& top, int x, int y);

        /// Half-size box-filtered copy (mip level generation), sizes rounded down to >= 1.
        [[nodiscard]] Image Downsampled() const;

    private:
        int width_ = 0;
        int height_ = 0;
        std::vector<Color> pixels_;
    };

    /// Uploads an image as a Texture2D with a full mip chain generated on the CPU.
    [[nodiscard]] std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D>
    UploadTexture(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Image& image, bool mipmaps = true);

    /// Uploads six square faces (+X, -X, +Y, -Y, +Z, -Z) as a cube map without mips.
    [[nodiscard]] std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube>
    UploadCubeMap(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const std::vector<Image>& faces);

    // ---------------------------------------------------------------- noise
    /// Noise helpers live in Core::Noise (shared with terrain generation); kept reachable here.
    namespace Noise
    {
        using Core::Noise::Hash;
        using Core::Noise::Value;
        using Core::Noise::Fbm;
        using Core::Noise::Cellular;
    }
}
