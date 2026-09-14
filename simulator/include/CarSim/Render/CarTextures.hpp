// Procedural textures for the car: paint detail (shut lines, seams, ambient darkening) in the
// body's UV space, glass tint with frit bands, tyre tread, rim finish, lamp lenses, grille mesh,
// interior plastics and fabrics. Pure CPU images; the renderer uploads them.
#pragma once

#include "CarSim/Render/Image.hpp"
#include "CarSim/Render/ProceduralCar.hpp"

namespace CarSim::Render::CarTextures
{
    /// White base multiplied onto the paint colour: dark shut lines and soft darkening at sills,
    /// arches and seams. `size` x `size`, u across, v down.
    [[nodiscard]] Image PaintDetail(const BodyUvLayout& uv, int size);
    /// Premultiplied RGBA glass: tint alpha everywhere, opaque black frit band around the
    /// windshield and a darker top shade band.
    [[nodiscard]] Image GlassTint(const BodyUvLayout& uv, int size, float alpha);
    /// Tyre: rubber with circumferential grooves in the tread band and sidewall lettering ring.
    [[nodiscard]] Image TyreTread(int size);
    [[nodiscard]] Image RimFinish(int size);
    [[nodiscard]] Image HeadlampLens(int size);
    [[nodiscard]] Image TailLampLens(int size);
    [[nodiscard]] Image GrilleMesh(int size);
    [[nodiscard]] Image InteriorPlastic(int size, const Rgb& base, unsigned seed);
    [[nodiscard]] Image Fabric(int size, const Rgb& base, unsigned seed);
    [[nodiscard]] Image Headliner(int size);
    [[nodiscard]] Image VentSlats(int size);
    [[nodiscard]] Image Chrome(int size);
}
