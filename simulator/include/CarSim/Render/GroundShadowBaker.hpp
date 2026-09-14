// Bakes the sun shadows of buildings and trees into a ground shadow map (grey image, 255 = lit)
// covering the terrain extent. The terrain macro texture and the road vertex colours multiply
// it in, so the ground under avenues and beside houses reads shaded at no per-frame cost.
// Pure CPU; no graphics device involved.
#pragma once

#include "CarSim/Map/MapWorld.hpp"
#include "CarSim/Render/Image.hpp"

#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace CarSim::Render::GroundShadows
{
    /// Shadow factor for every building (swept footprint, factor 0.45: what the rig's ambient
    /// and sky leave without the sun) and tree (crown disc, factor 0.52) projected along
    /// `sunDirection` (unit vector from the sun towards the scene), softened by one blur pass
    /// (about one texel of penumbra).
    [[nodiscard]] Image Bake(const Map::MapWorld& world, const Microsoft::Xna::Framework::Vector3& sunDirection, int width, int height);

    /// Bilinear sample of a shadow map (0..1) at a world position over the terrain extent.
    [[nodiscard]] float Sample(const Image& map, const Map::MapWorld& world, float x, float z);

    /// Ground offset of a point at `height` above the ground: where its shadow lands relative
    /// to its foot, for a light travelling along `sunDirection`.
    [[nodiscard]] Microsoft::Xna::Framework::Vector3 ShadowOffset(const Microsoft::Xna::Framework::Vector3& sunDirection, float height);
}
