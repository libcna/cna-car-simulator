// Bitmap font drawn with SpriteBatch from an atlas produced by tools/fontatlas.py.
#pragma once

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace CarSim::Render
{
    enum class TextAlign
    {
        Left,
        Center,
        Right
    };

    class BitmapFont
    {
    public:
        struct Glyph
        {
            int x = 0, y = 0, w = 0, h = 0;
            int xoff = 0, yoff = 0;
            float advance = 0.0f;
        };

        /// Loads `<contentRoot>/<name>.json` and the texture `<name>` through the content manager.
        /// Returns nullptr and logs when either is missing.
        [[nodiscard]] static std::unique_ptr<BitmapFont> Load(Microsoft::Xna::Framework::Content::ContentManager& content,
                                                              const std::string& contentRoot, const std::string& name);

        /// Builds a tiny built-in 5x7 font so text works even without content files (debug fallback).
        [[nodiscard]] static std::unique_ptr<BitmapFont> CreateBuiltin(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        [[nodiscard]] float LineHeight() const { return static_cast<float>(lineHeight_); }
        [[nodiscard]] float Size() const { return static_cast<float>(size_); }

        /// Width and height (in unscaled pixels) of a UTF-8 string (single line).
        [[nodiscard]] Microsoft::Xna::Framework::Vector2 Measure(const std::string& utf8) const;

        /// Draws a UTF-8 string at `position` (top-left of the line box), scaled uniformly.
        void Draw(Microsoft::Xna::Framework::Graphics::SpriteBatch& batch, const std::string& utf8,
                  Microsoft::Xna::Framework::Vector2 position, const Microsoft::Xna::Framework::Color& color,
                  float scale = 1.0f, TextAlign align = TextAlign::Left) const;

        /// Draws with a one-pixel dark shadow for HUD legibility.
        void DrawShadowed(Microsoft::Xna::Framework::Graphics::SpriteBatch& batch, const std::string& utf8,
                          Microsoft::Xna::Framework::Vector2 position, const Microsoft::Xna::Framework::Color& color,
                          float scale = 1.0f, TextAlign align = TextAlign::Left) const;

        [[nodiscard]] const Microsoft::Xna::Framework::Graphics::Texture2D& Texture() const { return texture_; }

        /// Decodes UTF-8 into code points (invalid bytes become U+FFFD).
        [[nodiscard]] static std::vector<std::uint32_t> DecodeUtf8(const std::string& utf8);

    private:
        Microsoft::Xna::Framework::Graphics::Texture2D texture_;
        std::unordered_map<std::uint32_t, Glyph> glyphs_;
        int size_ = 0;
        int lineHeight_ = 0;
        int ascent_ = 0;
    };
}
