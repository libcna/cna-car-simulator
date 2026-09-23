// Procedural passenger-car mesh generator.
//
// The body is a dense loft of cross-section rings along the length of the car. Every ring has
// the same topology (underbody, rocker, door band, belt, greenhouse tumblehome, roof crown), so
// panel features live at fixed ring indices and the whole skin is UV-mapped as (u = position
// around the ring, v = position along the car). Longitudinal shape comes from smooth curves
// (top line, belt line, plan-view widths); nose and tail are rounded in plan and elevation by a
// rounded-box sweep and finished with a sculpted face. Lamps, grille and glass outlines are
// cut from the skin in UV space and re-drawn as decal meshes on the body surface so their
// edges are exact. Wheels are revolved profiles. Everything is derived from a CarStyle, which is
// built from the vehicle definition (player car) or from a body preset (traffic variants).
#pragma once

#include "CarSim/Render/MeshData.hpp"
#include "CarSim/Sim/CarStyle.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <array>
#include <string>
#include <vector>

namespace CarSim::Render
{
    using Sim::CarStyle;

    /// Material slot of a car part; the renderer maps slots to effects and textures.
    enum class CarMaterial
    {
        Paint,          // body colour, environment-mapped, detail texture with shut lines
        Glass,          // tinted, alpha blended, frit band texture
        BlackTrim,      // bumper lower skins, sills, arch liners, mirror housings, wipers
        GlossBlack,     // B-pillars, window surrounds
        Chrome,         // badges, handles, exhaust tip
        MirrorGlass,    // door mirror glass: dark reflective, low fresnel
        Tyre,           // rubber with tread texture
        Rim,            // alloy wheel
        BrakeDisc,      // disc and caliper behind the spokes
        Grille,         // black mesh texture, recessed
        Interior,       // dashboard, door cards (dark grained plastic)
        InteriorMid,    // lower dashboard, lower door panels (mid grey plastic)
        InteriorLight,  // headliner, pillar trim (light fabric)
        Fabric,         // seats
        Vent,           // air vent slats
        LampHead,       // headlamp lens (emissive when on)
        LampTail,       // red rear lamp (emissive when braking)
        LampIndicator,  // amber indicator lens
        LampReverse,    // white reverse lens
        Plate,          // registration plate face (textured per vehicle)
        Cluster,        // instrument cluster face (textured dashboard)
        Needle          // gauge needles (emissive red/white)
    };

    /// A rigid part of the car with its own material and optional animation role.
    struct CarPart
    {
        enum class Role
        {
            Static,
            WheelFL, WheelFR, WheelRL, WheelRR,   // spin about the axle, steer for the fronts
            SteeringWheel,                        // rotate about the tilted column axis
            NeedleSpeed, NeedleRpm, NeedleFuel, NeedleTemp,
            GearLever,                            // tilts with the selected gear
            Interior                              // drawn only from the cockpit (and in mirrors)
        };
        std::string name;
        CarMaterial material = CarMaterial::Paint;
        Role role = Role::Static;
        MeshData mesh;                                   // in the part's local frame
        Microsoft::Xna::Framework::Vector3 pivot{};      // local origin of the part in vehicle space
        Microsoft::Xna::Framework::Vector3 axis{1.0f, 0.0f, 0.0f};   // rotation axis for animated parts
        bool exteriorOnly = false;                       // hidden from the cockpit camera (e.g. roof)
        bool detail = false;                             // small part: dropped at the far vehicle LOD
        bool cabin = false;                              // interior part that is also drawn from outside (seen through the glass)
    };

    /// Texture-space landmarks of the body skin (u around the ring, v along the car) used to
    /// draw the paint detail texture (shut lines, seams, ambient darkening).
    struct BodyUvLayout
    {
        // u values on the right side (mirror: 1 - u on the left); 0.5 is the top centre.
        float uUnderbody = 0.0f;
        float uRockerBottom = 0.05f;
        float uRockerTop = 0.09f;
        float uDoorMid = 0.15f;
        float uBelt = 0.22f;
        float uRoofRail = 0.36f;
        float uTop = 0.5f;
        // v values along the car (0 = nose tip, 1 = tail tip).
        float vNose = 0.0f;
        float vFrontBumper = 0.12f;
        float vHoodStart = 0.05f;
        float vCowl = 0.30f;
        float vDoorFront = 0.38f;
        float vBPillar = 0.62f;
        float vDoorRear = 0.84f;
        float vTailgate = 0.93f;
        float vRearBumper = 0.89f;
        float vFrontArch = 0.20f;
        float vRearArch = 0.83f;
        float archHalfV = 0.09f;
        float fuelFlapU = 0.18f;
        float fuelFlapV = 0.86f;
    };

    /// A lit lamp position for the glow sprites (world-space offset from the body origin).
    struct LampGlow
    {
        Microsoft::Xna::Framework::Vector3 position{};
        Microsoft::Xna::Framework::Vector3 normal{};
        CarMaterial kind = CarMaterial::LampHead;
        bool left = false;
    };

    /// A wing mirror's glass: centre in the body frame and its outboard yaw (radians).
    struct WingMirror
    {
        Microsoft::Xna::Framework::Vector3 centre{};
        float yaw = 0.0f;
    };

    struct CarModel
    {
        std::vector<CarPart> parts;
        std::vector<LampGlow> lamps;
        Microsoft::Xna::Framework::Vector3 frontPlateCenter{};
        Microsoft::Xna::Framework::Vector3 rearPlateCenter{};
        float wheelRadius = 0.3f;
        CarStyle style;
        BodyUvLayout uv;
        std::array<Microsoft::Xna::Framework::Vector3, 4> wheelCenters{};   // FL, FR, RL, RR at rest
        /// The wiped part of the windscreen as a flat quad in the body frame, inset from the
        /// pillars: bottom left, bottom right, top right, top left (left = -X).
        std::array<Microsoft::Xna::Framework::Vector3, 4> windscreen{};
        std::array<WingMirror, 2> wingMirrors{};   // left, right
        int bodyTriangles = 0;
    };

    /// Player car: style from the definition, full cockpit interior.
    [[nodiscard]] CarModel GenerateCar(const Sim::VehicleDefinition& definition);

    /// Generic generator. `definition` (optional) supplies the cockpit placement (driver eye,
    /// steering wheel, cluster, mirror); `interior` selects whether cabin parts are built.
    [[nodiscard]] CarModel GenerateCar(const CarStyle& style, const Sim::VehicleDefinition* definition, bool interior);
}
