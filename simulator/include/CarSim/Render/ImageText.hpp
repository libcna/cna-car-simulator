// CPU text rendering with the bitmap fonts: blits glyphs from the font atlas into an Image.
// Used for textures generated at load time (road signs, registration plates).
#pragma once

#include "CarSim/Render/BitmapFont.hpp"
#include "CarSim/Render/Image.hpp"

#include <string>

namespace CarSim::Render::ImageText
{
    /// Draws `utf8` with the top-left of the line box at (x, y); `scale` multiplies the font size.
    void Draw(Image& target, const BitmapFont& font, const Image& atlas, const std::string& utf8, float x, float y, float scale,
              const Microsoft::Xna::Framework::Color& color, TextAlign align = TextAlign::Left);

    /// Width and line height of the text at the given scale.
    [[nodiscard]] Microsoft::Xna::Framework::Vector2 Measure(const BitmapFont& font, const std::string& utf8, float scale);

    /// Scale that fits the text into `maxWidth` pixels, capped at `maxScale`.
    [[nodiscard]] float FitScale(const BitmapFont& font, const std::string& utf8, float maxWidth, float maxScale);
}
