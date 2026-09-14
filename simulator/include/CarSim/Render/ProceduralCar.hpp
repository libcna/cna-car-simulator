// Procedural passenger-car mesh generator: a fictional compact hatchback built from lofted
// cross-sections, with separate parts for wheels, glass, lamps, mirrors and the interior so the
// renderer can animate and light them independently. Everything is derived from the vehicle
// definition (dimensions, wheel positions, cockpit geometry) plus a small style preset.
#pragma once

#include "CarSim/Render/MeshData.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <array>
#include <string>
#include <vector>

namespace CarSim::Render
{
    /// Material slot of a car part; the renderer maps slots to effects.
    enum class CarMaterial
    {
        Paint,          // body colour, environment-mapped
        Glass,          // tinted, alpha blended
        BlackTrim,      // bumpers, sills, arches, mirrors housings
        Chrome,         // small bright metal parts
        Tyre,           // rubber
        Rim,            // alloy wheel
        Interior,       // dashboard, door cards, seats (dark plastic/fabric)
        InteriorLight,  // headliner, pillars (lighter)
        LampHead,       // headlight lens (emissive when on)
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
            Interior                              // drawn only from the cockpit (and in mirrors)
        };
        std::string name;
        CarMaterial material = CarMaterial::Paint;
        Role role = Role::Static;
        MeshData mesh;                                   // in the part's local frame
        Microsoft::Xna::Framework::Vector3 pivot{};      // local origin of the part in vehicle space
        Microsoft::Xna::Framework::Vector3 axis{1.0f, 0.0f, 0.0f};   // rotation axis for animated parts
        bool exteriorOnly = false;                       // hidden from the cockpit camera (e.g. roof)
    };

    struct CarModel
    {
        std::vector<CarPart> parts;
        Microsoft::Xna::Framework::Vector3 frontPlateCenter{};
        Microsoft::Xna::Framework::Vector3 rearPlateCenter{};
        float wheelRadius = 0.3f;
    };

    /// Generates the model for `definition`. Deterministic.
    [[nodiscard]] CarModel GenerateCar(const Sim::VehicleDefinition& definition);

    /// Body profile helpers exposed for tests (height of the body top at a given z, etc.).
    struct HatchbackProfile
    {
        float length, width, height, wheelbase, frontOverhang, rearOverhang;
        float sillHeight = 0.19f;        // ground clearance to the sill
        float beltline = 0.86f;          // top of the doors
        float roofFront = 0.0f;          // z where the roof starts (windshield top)
        float roofRear = 0.0f;           // z where the roof ends (hatch top)
        float hoodHeight = 0.74f;        // hood at the base of the windshield

        explicit HatchbackProfile(const Sim::VehicleDefinition& definition);

        /// Body top height at longitudinal position z (vehicle frame, -z forward).
        [[nodiscard]] float TopHeight(float z) const;
        /// Half width of the body at height y and position z (includes tumblehome above the beltline).
        [[nodiscard]] float HalfWidth(float z, float y) const;
        /// True when (z, y) lies in the greenhouse (glass) region.
        [[nodiscard]] bool IsGlass(float z, float y) const;
        [[nodiscard]] float FrontZ() const { return -(wheelbase * 0.5f + frontOverhang); }
        [[nodiscard]] float RearZ() const { return wheelbase * 0.5f + rearOverhang; }
    };
}
