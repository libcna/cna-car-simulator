// Czech registration plate textures: 520 x 110 mm white plate, black border, blue EU band with
// twelve stars and "CZ", black DIN-style characters (D-DIN Bold via the plate font atlas).
#pragma once

#include "CarSim/Render/BitmapFont.hpp"
#include "CarSim/Render/Image.hpp"

#include <string>

namespace CarSim::Render
{
    class PlateRenderer
    {
    public:
        static constexpr int kWidth = 512;
        static constexpr int kHeight = 108;   // 512 * 110 / 520 rounded to an even number

        /// Renders the plate text ("1A2 3456") into an image with the plate's aspect ratio.
        [[nodiscard]] static Image Render(const std::string& text, const BitmapFont& font, const Image& atlas);
    };
}
