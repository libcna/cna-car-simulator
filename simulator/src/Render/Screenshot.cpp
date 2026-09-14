#include "CarSim/Render/Screenshot.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/IO/FileMode.hpp"
#include "System/IO/FileStream.hpp"

#include <exception>
#include <iostream>
#include <vector>

namespace CarSim::Render
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    bool SaveBackBufferPng(GraphicsDevice& device, const std::string& path)
    {
        try {
            const auto& pp = device.getPresentationParametersProperty();
            const int width = pp.getBackBufferWidthProperty();
            const int height = pp.getBackBufferHeightProperty();
            if (width <= 0 || height <= 0) {
                std::cerr << "screenshot: back buffer has no size\n";
                return false;
            }

            std::vector<Color> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
            device.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size()));

            Texture2D texture(device, width, height);
            texture.SetData(pixels.data(), static_cast<int>(pixels.size()));

            System::IO::FileStream stream(path, System::IO::FileMode::Create);
            texture.SaveAsPng(&stream, width, height);
            return true;
        } catch (const std::exception& error) {
            std::cerr << "screenshot: failed to save '" << path << "': " << error.what() << "\n";
            return false;
        }
    }
}
