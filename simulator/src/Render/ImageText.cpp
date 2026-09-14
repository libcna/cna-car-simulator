#include "CarSim/Render/ImageText.hpp"

#include <algorithm>
#include <cmath>

namespace CarSim::Render::ImageText
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Vector2;

    namespace
    {
        /// Bilinear alpha sample of the (premultiplied white) atlas.
        float Coverage(const Image& atlas, const float x, const float y)
        {
            const int x0 = static_cast<int>(std::floor(x));
            const int y0 = static_cast<int>(std::floor(y));
            const float tx = x - static_cast<float>(x0);
            const float ty = y - static_cast<float>(y0);
            const auto a = [&](const int px, const int py) {
                if (px < 0 || py < 0 || px >= atlas.Width() || py >= atlas.Height()) return 0.0f;
                return static_cast<float>(atlas.At(px, py).getAProperty()) / 255.0f;
            };
            const float top = a(x0, y0) + (a(x0 + 1, y0) - a(x0, y0)) * tx;
            const float bottom = a(x0, y0 + 1) + (a(x0 + 1, y0 + 1) - a(x0, y0 + 1)) * tx;
            return top + (bottom - top) * ty;
        }
    }

    Vector2 Measure(const BitmapFont& font, const std::string& utf8, const float scale)
    {
        return font.Measure(utf8) * scale;
    }

    float FitScale(const BitmapFont& font, const std::string& utf8, const float maxWidth, const float maxScale)
    {
        const float width = font.Measure(utf8).X;
        if (width <= 1e-3f) {
            return maxScale;
        }
        return std::min(maxScale, maxWidth / width);
    }

    void Draw(Image& target, const BitmapFont& font, const Image& atlas, const std::string& utf8, float x, const float y, const float scale,
              const Color& color, const TextAlign align)
    {
        if (align != TextAlign::Left) {
            const float width = font.Measure(utf8).X * scale;
            x -= align == TextAlign::Center ? width * 0.5f : width;
        }
        float penX = x;
        const int r = static_cast<int>(color.getRProperty());
        const int g = static_cast<int>(color.getGProperty());
        const int b = static_cast<int>(color.getBProperty());
        const float alpha = static_cast<float>(color.getAProperty());
        for (const std::uint32_t cp : BitmapFont::DecodeUtf8(utf8)) {
            const BitmapFont::Glyph* glyph = font.FindGlyph(cp);
            if (!glyph) {
                penX += font.Size() * 0.5f * scale;
                continue;
            }
            if (glyph->w > 0 && glyph->h > 0 && cp != ' ') {
                const float destX = penX + static_cast<float>(glyph->xoff) * scale;
                const float destY = y + static_cast<float>(glyph->yoff) * scale;
                const int px0 = std::max(0, static_cast<int>(std::floor(destX)));
                const int py0 = std::max(0, static_cast<int>(std::floor(destY)));
                const int px1 = std::min(target.Width(), static_cast<int>(std::ceil(destX + static_cast<float>(glyph->w) * scale)));
                const int py1 = std::min(target.Height(), static_cast<int>(std::ceil(destY + static_cast<float>(glyph->h) * scale)));
                for (int py = py0; py < py1; ++py) {
                    for (int px = px0; px < px1; ++px) {
                        // Supersample 2x2 for smoother downscaled glyphs.
                        float cov = 0.0f;
                        for (int sy = 0; sy < 2; ++sy) {
                            for (int sx = 0; sx < 2; ++sx) {
                                const float u = (static_cast<float>(px) + 0.25f + 0.5f * static_cast<float>(sx) - destX) / scale;
                                const float v = (static_cast<float>(py) + 0.25f + 0.5f * static_cast<float>(sy) - destY) / scale;
                                if (u < 0.0f || v < 0.0f || u >= static_cast<float>(glyph->w) || v >= static_cast<float>(glyph->h)) continue;
                                cov += Coverage(atlas, static_cast<float>(glyph->x) + u - 0.5f, static_cast<float>(glyph->y) + v - 0.5f);
                            }
                        }
                        cov *= 0.25f;
                        if (cov <= 0.002f) continue;
                        target.Blend(px, py, Color(r, g, b, static_cast<int>(std::min(255.0f, alpha * cov))));
                    }
                }
            }
            penX += glyph->advance * scale;
        }
    }
}
