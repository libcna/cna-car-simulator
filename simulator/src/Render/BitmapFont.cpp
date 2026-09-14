#include "CarSim/Render/BitmapFont.hpp"

#include "CarSim/Render/Image.hpp"

#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "System/Text/Json/JsonDocument.hpp"
#include "System/Text/Json/JsonElement.hpp"
#include "System/Text/Json/JsonValueKind.hpp"

#include <array>
#include <exception>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>

namespace CarSim::Render
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    using System::Text::Json::JsonDocument;
    using System::Text::Json::JsonElement;
    using System::Text::Json::JsonValueKind;

    std::vector<std::uint32_t> BitmapFont::DecodeUtf8(const std::string& s)
    {
        std::vector<std::uint32_t> out;
        out.reserve(s.size());
        for (std::size_t i = 0; i < s.size();) {
            const auto c = static_cast<unsigned char>(s[i]);
            std::uint32_t cp = 0xFFFD;
            std::size_t len = 1;
            if (c < 0x80) {
                cp = c;
            } else if ((c >> 5) == 0x6 && i + 1 < s.size()) {
                cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
                len = 2;
            } else if ((c >> 4) == 0xE && i + 2 < s.size()) {
                cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) |
                     (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
                len = 3;
            } else if ((c >> 3) == 0x1E && i + 3 < s.size()) {
                cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12) |
                     ((static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6) | (static_cast<unsigned char>(s[i + 3]) & 0x3Fu);
                len = 4;
            }
            out.push_back(cp);
            i += len;
        }
        return out;
    }

    std::unique_ptr<BitmapFont> BitmapFont::Load(Content::ContentManager& content, const std::string& contentRoot,
                                                 const std::string& name)
    {
        const std::string jsonPath = contentRoot + "/" + name + ".json";
        std::ifstream file(jsonPath, std::ios::binary);
        if (!file) {
            std::cerr << "font: cannot open " << jsonPath << "\n";
            return nullptr;
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();

        auto font = std::unique_ptr<BitmapFont>(new BitmapFont());
        try {
            const auto document = JsonDocument::Parse(buffer.str());
            const JsonElement root = document->getRootElementProperty();
            font->size_ = root.GetProperty("size").GetInt32();
            font->lineHeight_ = root.GetProperty("lineHeight").GetInt32();
            font->ascent_ = root.GetProperty("ascent").GetInt32();
            for (const auto& g : root.GetProperty("glyphs").EnumerateArray()) {
                Glyph glyph;
                glyph.x = g.GetProperty("x").GetInt32();
                glyph.y = g.GetProperty("y").GetInt32();
                glyph.w = g.GetProperty("w").GetInt32();
                glyph.h = g.GetProperty("h").GetInt32();
                glyph.xoff = g.GetProperty("xoff").GetInt32();
                glyph.yoff = g.GetProperty("yoff").GetInt32();
                glyph.advance = static_cast<float>(g.GetProperty("advance").GetDouble());
                font->glyphs_[static_cast<std::uint32_t>(g.GetProperty("code").GetInt32())] = glyph;
            }
            font->texture_ = content.Load<Texture2D>(name);
        } catch (const std::exception& error) {
            std::cerr << "font: failed to load '" << name << "': " << error.what() << "\n";
            return nullptr;
        }
        return font;
    }

    namespace
    {
        using GlyphRows = std::array<std::uint8_t, 7>;

        GlyphRows BuiltinRows(const char ch)
        {
            switch (ch) {
                case 'A': return {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
                case 'B': return {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e};
                case 'C': return {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e};
                case 'D': return {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e};
                case 'E': return {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f};
                case 'F': return {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10};
                case 'G': return {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0e};
                case 'H': return {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
                case 'I': return {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f};
                case 'J': return {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c};
                case 'K': return {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
                case 'L': return {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f};
                case 'M': return {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11};
                case 'N': return {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
                case 'O': return {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
                case 'P': return {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10};
                case 'Q': return {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d};
                case 'R': return {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11};
                case 'S': return {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e};
                case 'T': return {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
                case 'U': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
                case 'V': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04};
                case 'W': return {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a};
                case 'X': return {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11};
                case 'Y': return {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04};
                case 'Z': return {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f};
                case '0': return {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e};
                case '1': return {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e};
                case '2': return {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f};
                case '3': return {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e};
                case '4': return {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02};
                case '5': return {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e};
                case '6': return {0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e};
                case '7': return {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
                case '8': return {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e};
                case '9': return {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e};
                case '.': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c};
                case ',': return {0x00, 0x00, 0x00, 0x00, 0x0c, 0x04, 0x08};
                case ':': return {0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00};
                case '-': return {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00};
                case '/': return {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10};
                case '%': return {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03};
                case '(': return {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02};
                case ')': return {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08};
                case '+': return {0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00};
                case '=': return {0x00, 0x00, 0x1f, 0x00, 0x1f, 0x00, 0x00};
                case '_': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f};
                case ' ': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
                default:  return {0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};
            }
        }
    }

    std::unique_ptr<BitmapFont> BitmapFont::CreateBuiltin(GraphicsDevice& device)
    {
        const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,:-/%()+=_ ?";
        const int cell = 8;
        const int columns = 16;
        const int rows = (static_cast<int>(chars.size()) + columns - 1) / columns;
        Image atlas(columns * cell, rows * cell, Color(255, 255, 255, 0));
        auto font = std::unique_ptr<BitmapFont>(new BitmapFont());
        for (std::size_t i = 0; i < chars.size(); ++i) {
            const int cx = static_cast<int>(i % columns) * cell;
            const int cy = static_cast<int>(i / columns) * cell;
            const GlyphRows rowsBits = BuiltinRows(chars[i]);
            for (int y = 0; y < 7; ++y) {
                for (int x = 0; x < 5; ++x) {
                    if (rowsBits[static_cast<std::size_t>(y)] & (0x10 >> x)) {
                        atlas.At(cx + x, cy + y) = Color(255, 255, 255, 255);
                    }
                }
            }
            Glyph g;
            g.x = cx;
            g.y = cy;
            g.w = 5;
            g.h = 7;
            g.xoff = 0;
            g.yoff = 0;
            g.advance = 6.0f;
            font->glyphs_[static_cast<std::uint32_t>(chars[i])] = g;
            if (chars[i] >= 'A' && chars[i] <= 'Z') {
                font->glyphs_[static_cast<std::uint32_t>(chars[i] - 'A' + 'a')] = g;
            }
        }
        font->size_ = 7;
        font->lineHeight_ = 9;
        font->ascent_ = 7;
        auto texture = UploadTexture(device, atlas, false);
        font->texture_ = *texture;
        return font;
    }

    Vector2 BitmapFont::Measure(const std::string& utf8) const
    {
        float width = 0.0f;
        for (const std::uint32_t cp : DecodeUtf8(utf8)) {
            const auto it = glyphs_.find(cp);
            if (it != glyphs_.end()) {
                width += it->second.advance;
            } else if (cp == '\t') {
                width += Size() * 2.0f;
            } else {
                width += Size() * 0.5f;
            }
        }
        return Vector2(width, static_cast<float>(lineHeight_));
    }

    void BitmapFont::Draw(SpriteBatch& batch, const std::string& utf8, Vector2 position, const Color& color,
                          const float scale, const TextAlign align) const
    {
        if (align != TextAlign::Left) {
            const float width = Measure(utf8).X * scale;
            position.X -= align == TextAlign::Center ? width * 0.5f : width;
        }
        float penX = position.X;
        for (const std::uint32_t cp : DecodeUtf8(utf8)) {
            const auto it = glyphs_.find(cp);
            if (it == glyphs_.end()) {
                penX += Size() * 0.5f * scale;
                continue;
            }
            const Glyph& g = it->second;
            if (g.w > 0 && g.h > 0 && cp != ' ') {
                const Rectangle source(g.x, g.y, g.w, g.h);
                const Vector2 dest(penX + static_cast<float>(g.xoff) * scale, position.Y + static_cast<float>(g.yoff) * scale);
                batch.Draw(texture_, dest, std::optional<Rectangle>(source), color, 0.0f, Vector2(0.0f, 0.0f), scale,
                           SpriteEffects::None, 0.0f);
            }
            penX += g.advance * scale;
        }
    }

    void BitmapFont::DrawShadowed(SpriteBatch& batch, const std::string& utf8, const Vector2 position, const Color& color,
                                  const float scale, const TextAlign align) const
    {
        const float offset = std::max(1.0f, scale);
        Draw(batch, utf8, position + Vector2(offset, offset), Color(0, 0, 0, static_cast<int>(color.getAProperty()) * 3 / 4), scale, align);
        Draw(batch, utf8, position, color, scale, align);
    }

    const BitmapFont::Glyph* BitmapFont::FindGlyph(const std::uint32_t codePoint) const
    {
        const auto it = glyphs_.find(codePoint);
        return it == glyphs_.end() ? nullptr : &it->second;
    }

    Image BitmapFont::AtlasImage() const
    {
        const int w = texture_.getWidthProperty();
        const int h = texture_.getHeightProperty();
        Image img(w, h);
        texture_.GetData(img.Pixels().data(), w * h);
        return img;
    }
}
