#include "CarSim/Render/SignGenerator.hpp"

#include "CarSim/Render/ImageText.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <sstream>
#include <vector>

namespace CarSim::Render
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;

    namespace
    {
        constexpr int kFace = 512;
        const Color kRed(200, 16, 30, 255);
        const Color kWhite(250, 250, 248, 255);
        const Color kBlack(20, 20, 22, 255);
        const Color kYellow(250, 200, 20, 255);
        const Color kBlue(0, 82, 170, 255);
        const Color kGrey(128, 128, 128, 255);
        const Color kNone(0, 0, 0, 0);

        using Poly = std::vector<std::pair<float, float>>;

        Poly TriangleUp(const float cx, const float cy, const float side)
        {
            const float h = side * std::sqrt(3.0f) * 0.5f;
            return {{cx, cy - h * 2.0f / 3.0f}, {cx + side * 0.5f, cy + h / 3.0f}, {cx - side * 0.5f, cy + h / 3.0f}};
        }

        Poly TriangleDown(const float cx, const float cy, const float side)
        {
            const float h = side * std::sqrt(3.0f) * 0.5f;
            return {{cx, cy + h * 2.0f / 3.0f}, {cx - side * 0.5f, cy - h / 3.0f}, {cx + side * 0.5f, cy - h / 3.0f}};
        }

        Poly Octagon(const float cx, const float cy, const float radius)
        {
            Poly p;
            for (int i = 0; i < 8; ++i) {
                const float a = (static_cast<float>(i) + 0.5f) * std::numbers::pi_v<float> / 4.0f;
                p.emplace_back(cx + std::cos(a) * radius, cy + std::sin(a) * radius);
            }
            return p;
        }

        Poly Diamond(const float cx, const float cy, const float half)
        {
            return {{cx, cy - half}, {cx + half, cy}, {cx, cy + half}, {cx - half, cy}};
        }

        Poly Rounded(const float x0, const float y0, const float x1, const float y1, const float r)
        {
            Poly p;
            const auto arc = [&](const float cx, const float cy, const float a0) {
                for (int i = 0; i <= 6; ++i) {
                    const float a = a0 + static_cast<float>(i) / 6.0f * std::numbers::pi_v<float> * 0.5f;
                    p.emplace_back(cx + std::cos(a) * r, cy + std::sin(a) * r);
                }
            };
            arc(x1 - r, y0 + r, -std::numbers::pi_v<float> * 0.5f);
            arc(x1 - r, y1 - r, 0.0f);
            arc(x0 + r, y1 - r, std::numbers::pi_v<float> * 0.5f);
            arc(x0 + r, y0 + r, std::numbers::pi_v<float>);
            return p;
        }

        /// Warning triangle (point up) with red border; returns the inner area for the pictogram.
        void WarningTriangle(Image& img)
        {
            const float c = kFace * 0.5f;
            img.FillPolygon(TriangleUp(c, c + 20.0f, kFace * 0.92f), kRed);
            img.FillPolygon(TriangleUp(c, c + 20.0f, kFace * 0.66f), kWhite);
        }

        void Circle(Image& img, const Color& ring, const Color& fill, const float ringWidth)
        {
            const float c = kFace * 0.5f;
            img.FillCircle(c, c, kFace * 0.48f, ring);
            img.FillCircle(c, c, kFace * 0.48f - ringWidth, fill);
        }

        void Strike(Image& img, const Color& color, const float thickness)
        {
            img.DrawLine(kFace * 0.14f, kFace * 0.86f, kFace * 0.86f, kFace * 0.14f, thickness, color);
        }

        void Text(Image& img, const BitmapFont& font, const Image& atlas, const std::string& text, const float cx, const float cy,
                  const float maxWidth, const float maxScale, const Color& color)
        {
            const float scale = ImageText::FitScale(font, text, maxWidth, maxScale);
            const Vector2 size = ImageText::Measure(font, text, scale);
            ImageText::Draw(img, font, atlas, text, cx, cy - size.Y * 0.5f, scale, color, TextAlign::Center);
        }

        void Pedestrian(Image& img, const float cx, const float cy, const float h, const Color& color)
        {
            // Walking figure: head, body, legs, arm.
            img.FillCircle(cx, cy - h * 0.40f, h * 0.09f, color);
            img.DrawLine(cx, cy - h * 0.30f, cx - h * 0.04f, cy + h * 0.02f, h * 0.12f, color);
            img.DrawLine(cx - h * 0.04f, cy + 0.0f, cx - h * 0.22f, cy + h * 0.42f, h * 0.09f, color);
            img.DrawLine(cx - h * 0.04f, cy + 0.0f, cx + h * 0.16f, cy + h * 0.40f, h * 0.09f, color);
            img.DrawLine(cx, cy - h * 0.25f, cx + h * 0.20f, cy - h * 0.05f, h * 0.07f, color);
        }

        void Deer(Image& img, const float cx, const float cy, const float h, const Color& color)
        {
            // Leaping deer silhouette: body, neck/head, legs, antlers.
            img.FillPolygon({{cx - h * 0.30f, cy + h * 0.02f}, {cx + h * 0.18f, cy - h * 0.10f}, {cx + h * 0.24f, cy + h * 0.06f}, {cx - h * 0.26f, cy + h * 0.16f}}, color);
            img.DrawLine(cx + h * 0.16f, cy - h * 0.06f, cx + h * 0.30f, cy - h * 0.32f, h * 0.07f, color);
            img.FillPolygon({{cx + h * 0.24f, cy - h * 0.38f}, {cx + h * 0.42f, cy - h * 0.34f}, {cx + h * 0.36f, cy - h * 0.26f}, {cx + h * 0.24f, cy - h * 0.28f}}, color);
            img.DrawLine(cx + h * 0.30f, cy - h * 0.36f, cx + h * 0.22f, cy - h * 0.52f, h * 0.03f, color);
            img.DrawLine(cx + h * 0.30f, cy - h * 0.36f, cx + h * 0.36f, cy - h * 0.54f, h * 0.03f, color);
            img.DrawLine(cx + h * 0.24f, cy - h * 0.44f, cx + h * 0.12f, cy - h * 0.50f, h * 0.03f, color);
            img.DrawLine(cx - h * 0.22f, cy + h * 0.10f, cx - h * 0.36f, cy + h * 0.40f, h * 0.05f, color);
            img.DrawLine(cx - h * 0.14f, cy + h * 0.12f, cx - h * 0.02f, cy + h * 0.40f, h * 0.05f, color);
            img.DrawLine(cx + h * 0.12f, cy + h * 0.02f, cx + h * 0.02f, cy + h * 0.36f, h * 0.05f, color);
            img.DrawLine(cx + h * 0.20f, cy + h * 0.00f, cx + h * 0.34f, cy + h * 0.30f, h * 0.05f, color);
        }

        void Bus(Image& img, const float cx, const float cy, const float w, const Color& color)
        {
            const float h = w * 0.55f;
            img.FillPolygon(Rounded(cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.35f, w * 0.06f), color);
            img.FillCircle(cx - w * 0.30f, cy + h * 0.42f, w * 0.08f, color);
            img.FillCircle(cx + w * 0.30f, cy + h * 0.42f, w * 0.08f, color);
            for (int i = 0; i < 4; ++i) {
                const float x = cx - w * 0.38f + static_cast<float>(i) * w * 0.25f;
                img.FillRect(static_cast<int>(x), static_cast<int>(cy - h * 0.38f), static_cast<int>(x + w * 0.16f), static_cast<int>(cy - h * 0.05f), kBlue);
            }
        }
    }

    bool SignGenerator::IsKnown(const std::string& code)
    {
        static const char* known[] = {"P1", "P2", "P3", "P4", "P6", "B1", "B2", "B20a", "B20b", "IZ4a", "IZ4b", "IS3a", "IS3b", "IS3c", "IS3d",
                                      "IP6", "IJ4c", "A7a", "A12a", "A14", "A22"};
        return std::any_of(std::begin(known), std::end(known), [&](const char* k) { return code == k; });
    }

    std::string SignGenerator::FaceKey(const Map::SignSpec& spec)
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "|%.0f", static_cast<double>(spec.value));
        return spec.code + "|" + spec.text + buffer;
    }

    SignFace SignGenerator::Face(const Map::SignSpec& spec, const BitmapFont& font, const Image& atlas)
    {
        SignFace face;
        const std::string& code = spec.code;
        const float c = kFace * 0.5f;
        if (code == "IZ4a" || code == "IZ4b") {
            face.image = Image(kFace, kFace / 2, kNone);
            face.widthM = 1.0f;
            face.heightM = 0.5f;
            face.image.FillRect(0, 0, kFace, kFace / 2, kBlack);
            face.image.FillRect(10, 10, kFace - 10, kFace / 2 - 10, kWhite);
            Text(face.image, font, atlas, spec.text.empty() ? "Obec" : spec.text, c, kFace * 0.25f, kFace * 0.86f, 4.2f, kBlack);
            if (code == "IZ4b") {
                face.image.DrawLine(kFace * 0.08f, kFace * 0.44f, kFace * 0.92f, kFace * 0.06f, 22.0f, kRed);
            }
        } else if (code == "IS3a" || code == "IS3b" || code == "IS3c" || code == "IS3d") {
            face.image = Image(kFace, kFace / 4, kNone);
            face.widthM = 1.6f;
            face.heightM = 0.4f;
            face.image.FillRect(0, 0, kFace, kFace / 4, kBlack);
            face.image.FillRect(6, 6, kFace - 6, kFace / 4 - 6, kWhite);
            // Arrow at the left (IS3a/c) or right (IS3b/d).
            const bool right = code == "IS3b" || code == "IS3d";
            const float ax = right ? kFace * 0.93f : kFace * 0.07f;
            const float dir = right ? 1.0f : -1.0f;
            face.image.FillPolygon({{ax, kFace * 0.125f}, {ax - dir * 40.0f, kFace * 0.05f}, {ax - dir * 40.0f, kFace * 0.20f}}, kBlack);
            face.image.FillRect(static_cast<int>(std::min(ax - dir * 40.0f, ax - dir * 70.0f)), static_cast<int>(kFace * 0.105f),
                                static_cast<int>(std::max(ax - dir * 40.0f, ax - dir * 70.0f)), static_cast<int>(kFace * 0.145f), kBlack);
            std::string text = spec.text.empty() ? "Bor" : spec.text;
            // Two destinations separated by " / " share the line.
            std::replace(text.begin(), text.end(), '/', ' ');
            Text(face.image, font, atlas, text, right ? c - 40.0f : c + 40.0f, kFace * 0.125f, kFace * 0.70f, 2.2f, kBlack);
        } else if (code == "IP6" || code == "IJ4c") {
            face.image = Image(kFace, kFace, kNone);
            face.widthM = face.heightM = 0.5f;
            face.image.FillPolygon(Rounded(4.0f, 4.0f, kFace - 4.0f, kFace - 4.0f, 30.0f), kWhite);
            face.image.FillPolygon(Rounded(14.0f, 14.0f, kFace - 14.0f, kFace - 14.0f, 24.0f), kBlue);
            if (code == "IP6") {
                face.image.FillPolygon(TriangleUp(c, c + 30.0f, kFace * 0.62f), kWhite);
                Pedestrian(face.image, c + 10.0f, c + 30.0f, kFace * 0.40f, kBlack);
            } else {
                Bus(face.image, c, c, kFace * 0.62f, kWhite);
            }
        } else {
            face.image = Image(kFace, kFace, kNone);
            if (code == "P1" || code == "A7a" || code == "A12a" || code == "A14" || code == "A22") {
                face.widthM = face.heightM = 0.9f;
                face.image = Image(kFace, kFace, kNone);
                WarningTriangle(face.image);
                if (code == "P1") {
                    face.image.FillRect(static_cast<int>(c - 16.0f), static_cast<int>(c - 60.0f), static_cast<int>(c + 16.0f), static_cast<int>(c + 110.0f), kBlack);
                    face.image.FillRect(static_cast<int>(c - 70.0f), static_cast<int>(c + 10.0f), static_cast<int>(c + 70.0f), static_cast<int>(c + 30.0f), kBlack);
                } else if (code == "A7a") {
                    for (const float x : {c - 55.0f, c + 25.0f}) {
                        face.image.FillCircle(x + 15.0f, c + 78.0f, 34.0f, kBlack);
                    }
                    face.image.FillRect(static_cast<int>(c - 110.0f), static_cast<int>(c + 78.0f), static_cast<int>(c + 110.0f), static_cast<int>(c + 112.0f), kBlack);
                    face.image.FillRect(static_cast<int>(c - 110.0f), static_cast<int>(c + 78.0f), static_cast<int>(c + 110.0f), static_cast<int>(c + 96.0f), kWhite);
                    face.image.FillRect(static_cast<int>(c - 110.0f), static_cast<int>(c + 96.0f), static_cast<int>(c + 110.0f), static_cast<int>(c + 112.0f), kBlack);
                } else if (code == "A12a") {
                    Pedestrian(face.image, c, c + 40.0f, kFace * 0.40f, kBlack);
                } else if (code == "A14") {
                    Deer(face.image, c, c + 40.0f, kFace * 0.42f, kBlack);
                } else {
                    face.image.FillRect(static_cast<int>(c - 18.0f), static_cast<int>(c - 50.0f), static_cast<int>(c + 18.0f), static_cast<int>(c + 70.0f), kBlack);
                    face.image.FillCircle(c, c + 108.0f, 22.0f, kBlack);
                }
            } else if (code == "P2" || code == "P3") {
                face.widthM = face.heightM = 0.7f;
                face.image.FillPolygon(Diamond(c, c, kFace * 0.48f), kWhite);
                face.image.FillPolygon(Diamond(c, c, kFace * 0.34f), kYellow);
                if (code == "P3") Strike(face.image, kBlack, 26.0f);
            } else if (code == "P4") {
                face.widthM = face.heightM = 0.9f;
                face.image.FillPolygon(TriangleDown(c, c - 20.0f, kFace * 0.92f), kRed);
                face.image.FillPolygon(TriangleDown(c, c - 20.0f, kFace * 0.62f), kWhite);
            } else if (code == "P6") {
                face.widthM = face.heightM = 0.7f;
                face.image.FillPolygon(Octagon(c, c, kFace * 0.50f), kWhite);
                face.image.FillPolygon(Octagon(c, c, kFace * 0.46f), kRed);
                Text(face.image, font, atlas, "STOP", c, c, kFace * 0.66f, 3.6f, kWhite);
            } else if (code == "B20a" || code == "B20b") {
                face.widthM = face.heightM = 0.7f;
                if (code == "B20a") {
                    Circle(face.image, kRed, kWhite, kFace * 0.09f);
                } else {
                    Circle(face.image, kWhite, kWhite, kFace * 0.09f);
                    face.image.FillRing(c, c, kFace * 0.48f, kFace * 0.46f, kGrey);
                }
                char number[16];
                std::snprintf(number, sizeof(number), "%.0f", static_cast<double>(spec.value > 0.0f ? spec.value : 50.0f));
                Text(face.image, font, atlas, number, c, c, kFace * 0.60f, 4.4f, code == "B20a" ? kBlack : kGrey);
                if (code == "B20b") {
                    for (const float off : {-40.0f, 0.0f, 40.0f}) {
                        face.image.DrawLine(kFace * 0.20f + off, kFace * 0.80f, kFace * 0.80f + off, kFace * 0.20f, 10.0f, kGrey);
                    }
                }
            } else if (code == "B1") {
                face.widthM = face.heightM = 0.7f;
                Circle(face.image, kWhite, kRed, kFace * 0.02f);
                face.image.FillRect(static_cast<int>(c - 150.0f), static_cast<int>(c - 32.0f), static_cast<int>(c + 150.0f), static_cast<int>(c + 32.0f), kWhite);
            } else if (code == "B2") {
                face.widthM = face.heightM = 0.7f;
                Circle(face.image, kRed, kWhite, kFace * 0.09f);
            } else {
                // Unknown code: grey placeholder disc.
                face.widthM = face.heightM = 0.5f;
                Circle(face.image, kGrey, kWhite, kFace * 0.05f);
                Text(face.image, font, atlas, code, c, c, kFace * 0.7f, 2.0f, kBlack);
            }
        }
        // Back of the plate: the same silhouette in galvanised grey.
        face.back = Image(face.image.Width(), face.image.Height(), kNone);
        for (int y = 0; y < face.image.Height(); ++y) {
            for (int x = 0; x < face.image.Width(); ++x) {
                const int a = static_cast<int>(face.image.At(x, y).getAProperty());
                if (a > 0) {
                    face.back.At(x, y) = Color(122, 124, 126, a);
                }
            }
        }
        return face;
    }

    void SignGenerator::AppendSign(const Map::PlacedSign& sign, const SignFace& face, MeshData& faces, MeshData& backs, MeshData& posts)
    {
        // Local frame: face normal +z (towards the approaching driver); +x is the driver's right.
        const float bottom = sign.urban ? face.bottomUrbanM : face.bottomRuralM;
        const float top = bottom + face.heightM;
        const float hw = face.widthM * 0.5f;
        MeshData local;
        const Color front(255, 255, 255, 255);
        const Color back(255, 255, 255, 255);
        const Vector3 n(0.0f, 0.0f, 1.0f);
        const float zf = 0.045f;
        // Front face (textured), back face (grey, same alpha mask so the shape matches).
        local.AddQuad(Vector3(-hw, bottom, zf), Vector3(hw, bottom, zf), Vector3(hw, top, zf), Vector3(-hw, top, zf), n,
                      Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0), front);
        MeshData localBack;
        localBack.AddQuad(Vector3(hw, bottom, zf - 0.004f), Vector3(-hw, bottom, zf - 0.004f), Vector3(-hw, top, zf - 0.004f), Vector3(hw, top, zf - 0.004f), n * -1.0f,
                          Vector2(1, 1), Vector2(0, 1), Vector2(0, 0), Vector2(1, 0), back);
        MeshData post;
        post.AddCylinder(Vector3(0.0f, -0.25f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), 0.032f, top + 0.3f, 8, true);
        const Matrix world = Matrix::CreateRotationY(-sign.headingRad + 3.14159265f) * Matrix::CreateTranslation(sign.position);
        faces.Append(local, world);
        backs.Append(localBack, world);
        posts.Append(post, world);
    }
}
