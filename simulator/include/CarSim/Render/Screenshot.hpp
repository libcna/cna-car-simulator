// Back-buffer capture through the XNA 4.0 API (GetBackBufferData + Texture2D::SaveAsPng).
#pragma once

#include <string>

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
    class Texture2D;
}

namespace CarSim::Render
{
    /// Reads the current back buffer and writes it as a PNG file.
    /// Returns false (and logs to stderr) when the device or file refuses.
    bool SaveBackBufferPng(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                           const std::string& path);

    /// Writes any texture (including render targets) as a PNG file.
    bool SaveTexturePng(Microsoft::Xna::Framework::Graphics::Texture2D& texture, const std::string& path);
}
