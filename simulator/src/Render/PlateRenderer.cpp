#include "CarSim/Render/PlateRenderer.hpp"

#include "CarSim/Render/ImageText.hpp"

#include <cmath>
#include <numbers>

namespace CarSim::Render
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Vector2;

    Image PlateRenderer::Render(const std::string& text, const BitmapFont& font, const Image& atlas)
    {
        const float scaleMm = static_cast<float>(kWidth) / 520.0f;   // pixels per millimetre
        Image img(kWidth, kHeight, Color(20, 20, 22, 255));
        // White face inside a 4 mm black border with a slightly rounded look.
        const int border = static_cast<int>(std::round(4.0f * scaleMm));
        img.FillRect(border, border, kWidth - border, kHeight - border, Color(246, 246, 242, 255));
        // Blue EU band: 40 mm wide at the left, full height inside the border.
        const int bandRight = static_cast<int>(std::round(44.0f * scaleMm));
        img.FillRect(border, border, bandRight, kHeight - border, Color(0, 51, 153, 255));
        // Twelve stars in a circle 15 mm in diameter, 8 mm below the top.
        const float cx = (border + bandRight) * 0.5f;
        const float cy = 8.0f * scaleMm + 7.5f * scaleMm + border;
        const float ring = 7.5f * scaleMm;
        for (int i = 0; i < 12; ++i) {
            const float a = static_cast<float>(i) / 12.0f * 2.0f * std::numbers::pi_v<float>;
            img.FillCircle(cx + std::cos(a) * ring, cy + std::sin(a) * ring, 1.6f * scaleMm * 0.55f, Color(255, 204, 0, 255));
        }
        // "CZ" in white below the stars.
        const float czScale = ImageText::FitScale(font, "CZ", (bandRight - border) * 0.8f, 0.5f);
        const Vector2 czSize = ImageText::Measure(font, "CZ", czScale);
        ImageText::Draw(img, font, atlas, "CZ", cx, kHeight - border - czSize.Y - 4.0f * scaleMm, czScale, Color(255, 255, 255, 255), TextAlign::Center);
        // Characters: 75 mm high in a 110 mm plate, centred in the remaining width.
        const float areaLeft = static_cast<float>(bandRight) + 10.0f * scaleMm;
        const float areaRight = static_cast<float>(kWidth - border) - 10.0f * scaleMm;
        const float targetHeight = 75.0f * scaleMm;
        const float capScale = targetHeight / std::max(1.0f, font.Size() * 0.72f);   // cap height ~ 0.72 em
        const float scale = std::min(capScale, ImageText::FitScale(font, text, areaRight - areaLeft, capScale));
        const Vector2 size = ImageText::Measure(font, text, scale);
        const float ascentPx = static_cast<float>(font.Ascent()) * scale;
        const float capTop = ascentPx - font.Size() * 0.72f * scale;   // line-box offset of the cap height
        const float y = (static_cast<float>(kHeight) - font.Size() * 0.72f * scale) * 0.5f - capTop;
        ImageText::Draw(img, font, atlas, text, (areaLeft + areaRight) * 0.5f, y, scale, Color(16, 16, 18, 255), TextAlign::Center);
        (void)size;
        return img;
    }
}
