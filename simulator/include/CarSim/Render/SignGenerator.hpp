// Czech road signs (vyhláška č. 294/2015 Sb. subset): faces drawn procedurally into images,
// text set in the DIN-style bitmap font, mounted on grey posts.
#pragma once

#include "CarSim/Map/ObjectPlacement.hpp"
#include "CarSim/Render/BitmapFont.hpp"
#include "CarSim/Render/Image.hpp"
#include "CarSim/Render/MeshData.hpp"

#include <string>

namespace CarSim::Render
{
    struct SignFace
    {
        Image image;            // RGBA, transparent outside the sign shape
        Image back;             // same alpha mask, plain grey (the unprinted back of the plate)
        float widthM = 0.7f;    // physical size of the face
        float heightM = 0.7f;
        float bottomUrbanM = 2.2f;   // lower edge above ground inside built-up areas
        float bottomRuralM = 1.5f;
    };

    class SignGenerator
    {
    public:
        /// Key that identifies identical faces (code + text + value) for texture sharing.
        [[nodiscard]] static std::string FaceKey(const Map::SignSpec& spec);

        /// Renders the face image. `font` may be the built-in fallback; `atlas` is its atlas image.
        [[nodiscard]] static SignFace Face(const Map::SignSpec& spec, const BitmapFont& font, const Image& atlas);

        /// Appends the face quads (front and back use separate textures) and the post.
        static void AppendSign(const Map::PlacedSign& sign, const SignFace& face, MeshData& faces, MeshData& backs, MeshData& posts);

        [[nodiscard]] static bool IsKnown(const std::string& code);
    };
}
