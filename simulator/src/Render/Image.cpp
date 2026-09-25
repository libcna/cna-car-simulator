#include "CarSim/Render/Image.hpp"

#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace CarSim::Render
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    Rgb Lerp(const Rgb& a, const Rgb& b, const float t)
    {
        return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
    }

    Image::Image(const int width, const int height, const Color& fill)
        : width_(std::max(1, width)),
          height_(std::max(1, height)),
          pixels_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), fill)
    {
    }

    const Color& Image::Wrap(int x, int y) const
    {
        x = ((x % width_) + width_) % width_;
        y = ((y % height_) + height_) % height_;
        return At(x, y);
    }

    void Image::Set(const int x, const int y, const Rgb& rgb, const float alpha)
    {
        const auto toByte = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
        At(x, y) = Color(toByte(rgb.r), toByte(rgb.g), toByte(rgb.b), toByte(alpha));
    }

    void Image::Fill(const Color& color)
    {
        std::fill(pixels_.begin(), pixels_.end(), color);
    }

    void Image::Generate(const std::function<Color(int, int, float, float)>& f)
    {
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                At(x, y) = f(x, y, (static_cast<float>(x) + 0.5f) / static_cast<float>(width_),
                             (static_cast<float>(y) + 0.5f) / static_cast<float>(height_));
            }
        }
    }

    void Image::FillRect(const int x0, const int y0, const int x1, const int y1, const Color& color)
    {
        for (int y = std::max(0, y0); y < std::min(height_, y1); ++y) {
            for (int x = std::max(0, x0); x < std::min(width_, x1); ++x) {
                At(x, y) = color;
            }
        }
    }

    void Image::FillCircle(const float cx, const float cy, const float radius, const Color& color)
    {
        const int x0 = std::max(0, static_cast<int>(std::floor(cx - radius)));
        const int x1 = std::min(width_ - 1, static_cast<int>(std::ceil(cx + radius)));
        const int y0 = std::max(0, static_cast<int>(std::floor(cy - radius)));
        const int y1 = std::min(height_ - 1, static_cast<int>(std::ceil(cy + radius)));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float dx = static_cast<float>(x) + 0.5f - cx;
                const float dy = static_cast<float>(y) + 0.5f - cy;
                if (dx * dx + dy * dy <= radius * radius) {
                    At(x, y) = color;
                }
            }
        }
    }

    void Image::Blend(const int x, const int y, const Color& color)
    {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) {
            return;
        }
        const float a = static_cast<float>(color.getAProperty()) / 255.0f;
        if (a <= 0.0f) {
            return;
        }
        Color& dst = At(x, y);
        const auto mix = [&](const int d, const int s) { return static_cast<int>(static_cast<float>(d) * (1.0f - a) + static_cast<float>(s) * a + 0.5f); };
        const int da = static_cast<int>(dst.getAProperty());
        const int outA = std::min(255, static_cast<int>(static_cast<float>(da) * (1.0f - a) + 255.0f * a + 0.5f));
        dst = Color(mix(static_cast<int>(dst.getRProperty()), static_cast<int>(color.getRProperty())),
                    mix(static_cast<int>(dst.getGProperty()), static_cast<int>(color.getGProperty())),
                    mix(static_cast<int>(dst.getBProperty()), static_cast<int>(color.getBProperty())), outA);
    }

    void Image::FillPolygon(const std::vector<std::pair<float, float>>& points, const Color& color)
    {
        if (points.size() < 3) {
            return;
        }
        float minY = points[0].second;
        float maxY = minY;
        for (const auto& p : points) {
            minY = std::min(minY, p.second);
            maxY = std::max(maxY, p.second);
        }
        const int y0 = std::max(0, static_cast<int>(std::floor(minY)));
        const int y1 = std::min(height_ - 1, static_cast<int>(std::ceil(maxY)));
        std::vector<float> crossings;
        for (int y = y0; y <= y1; ++y) {
            const float sy = static_cast<float>(y) + 0.5f;
            crossings.clear();
            for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
                const auto& a = points[j];
                const auto& b = points[i];
                if ((a.second > sy) != (b.second > sy)) {
                    crossings.push_back(a.first + (sy - a.second) / (b.second - a.second) * (b.first - a.first));
                }
            }
            std::sort(crossings.begin(), crossings.end());
            for (std::size_t k = 0; k + 1 < crossings.size(); k += 2) {
                const int xa = std::max(0, static_cast<int>(std::round(crossings[k])));
                const int xb = std::min(width_, static_cast<int>(std::round(crossings[k + 1])));
                for (int x = xa; x < xb; ++x) {
                    Blend(x, y, color);
                }
            }
        }
    }

    void Image::FillTriangle(const float x0, const float y0, const float x1, const float y1, const float x2, const float y2, const Color& color)
    {
        FillPolygon({{x0, y0}, {x1, y1}, {x2, y2}}, color);
    }

    void Image::DrawLine(const float x0, const float y0, const float x1, const float y1, const float thickness, const Color& color)
    {
        const float dx = x1 - x0;
        const float dy = y1 - y0;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-4f) {
            FillCircle(x0, y0, thickness * 0.5f, color);
            return;
        }
        const float nx = -dy / len * thickness * 0.5f;
        const float ny = dx / len * thickness * 0.5f;
        FillPolygon({{x0 + nx, y0 + ny}, {x1 + nx, y1 + ny}, {x1 - nx, y1 - ny}, {x0 - nx, y0 - ny}}, color);
    }

    void Image::FillRing(const float cx, const float cy, const float outerRadius, const float innerRadius, const Color& color)
    {
        const int x0 = std::max(0, static_cast<int>(cx - outerRadius - 1.0f));
        const int x1 = std::min(width_ - 1, static_cast<int>(cx + outerRadius + 1.0f));
        const int y0 = std::max(0, static_cast<int>(cy - outerRadius - 1.0f));
        const int y1 = std::min(height_ - 1, static_cast<int>(cy + outerRadius + 1.0f));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float d = std::sqrt((static_cast<float>(x) + 0.5f - cx) * (static_cast<float>(x) + 0.5f - cx) +
                                          (static_cast<float>(y) + 0.5f - cy) * (static_cast<float>(y) + 0.5f - cy));
                const float coverage = std::clamp(outerRadius - d + 0.5f, 0.0f, 1.0f) * std::clamp(d - innerRadius + 0.5f, 0.0f, 1.0f);
                if (coverage <= 0.0f) continue;
                Blend(x, y, Color(static_cast<int>(color.getRProperty()), static_cast<int>(color.getGProperty()), static_cast<int>(color.getBProperty()),
                                  static_cast<int>(static_cast<float>(color.getAProperty()) * coverage)));
            }
        }
    }

    void Image::BlendOver(const Image& top, const int ox, const int oy)
    {
        for (int y = 0; y < top.Height(); ++y) {
            const int ty = oy + y;
            if (ty < 0 || ty >= height_) continue;
            for (int x = 0; x < top.Width(); ++x) {
                const int tx = ox + x;
                if (tx < 0 || tx >= width_) continue;
                const Color& s = top.At(x, y);
                Color& d = At(tx, ty);
                const float a = s.getAProperty() / 255.0f;
                const auto mix = [&](int sv, int dv) { return static_cast<int>(std::lround(sv * a + dv * (1.0f - a))); };
                d = Color(mix(s.getRProperty(), d.getRProperty()), mix(s.getGProperty(), d.getGProperty()),
                          mix(s.getBProperty(), d.getBProperty()),
                          static_cast<int>(std::max(s.getAProperty(), d.getAProperty())));
            }
        }
    }

    Image Image::Downsampled() const
    {
        const int w = std::max(1, width_ / 2);
        const int h = std::max(1, height_ / 2);
        Image out(w, h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int r = 0, g = 0, b = 0, a = 0;
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        const Color& c = At(std::min(width_ - 1, x * 2 + dx), std::min(height_ - 1, y * 2 + dy));
                        r += c.getRProperty();
                        g += c.getGProperty();
                        b += c.getBProperty();
                        a += c.getAProperty();
                    }
                }
                out.At(x, y) = Color(r / 4, g / 4, b / 4, a / 4);
            }
        }
        return out;
    }

    std::unique_ptr<Texture2D> UploadTexture(GraphicsDevice& device, const Image& image, const bool mipmaps)
    {
        auto texture = std::make_unique<Texture2D>(device, image.Width(), image.Height(), mipmaps, SurfaceFormat::Color);
        UpdateTexture(*texture, image);
        return texture;
    }

    void UpdateTexture(Texture2D& texture, const Image& image)
    {
        if (texture.getWidthProperty() != image.Width() || texture.getHeightProperty() != image.Height()) {
            throw std::invalid_argument("texture update dimensions do not match");
        }
        texture.SetData(0, nullptr, image.Pixels().data(), 0, static_cast<int>(image.Pixels().size()));
        if (texture.getLevelCountProperty() > 1) {
            Image level = image;
            int index = 1;
            while (level.Width() > 1 || level.Height() > 1) {
                level = level.Downsampled();
                texture.SetData(index, nullptr, level.Pixels().data(), 0, static_cast<int>(level.Pixels().size()));
                ++index;
            }
        }
    }

    std::unique_ptr<TextureCube> UploadCubeMap(GraphicsDevice& device, const std::vector<Image>& faces)
    {
        if (faces.size() != 6) {
            return nullptr;
        }
        const int size = faces.front().Width();
        auto cube = std::make_unique<TextureCube>(device, size, false, SurfaceFormat::Color);
        const CubeMapFace order[6] = {CubeMapFace::PositiveX, CubeMapFace::NegativeX, CubeMapFace::PositiveY,
                                      CubeMapFace::NegativeY, CubeMapFace::PositiveZ, CubeMapFace::NegativeZ};
        for (int i = 0; i < 6; ++i) {
            cube->SetData(order[i], faces[static_cast<std::size_t>(i)].Pixels().data(),
                          static_cast<int>(faces[static_cast<std::size_t>(i)].Pixels().size()));
        }
        return cube;
    }
}
