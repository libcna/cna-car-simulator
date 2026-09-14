// Back-buffer capture through the XNA 4.0 API (GetBackBufferData + Texture2D::SaveAsPng).
#pragma once

#include <string>

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
}

namespace CarSim::Render
{
    /// Reads the current back buffer and writes it as a PNG file.
    /// Returns false (and logs to stderr) when the device or file refuses.
    bool SaveBackBufferPng(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                           const std::string& path);
}
