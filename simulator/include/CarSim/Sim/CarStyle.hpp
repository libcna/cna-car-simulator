// Body preset of a car: dimensions and proportions shared by the procedural mesh generator
// (Render) and the traffic system (collision boxes, masses). Pure data, no graphics.
#pragma once

#include "CarSim/Sim/VehicleDefinition.hpp"

namespace CarSim::Sim
{
    /// All lengths in metres, vehicle frame (+X right, +Y up, -Z forward, origin on the ground
    /// under the wheelbase centre).
    struct CarStyle
    {
        enum class Body
        {
            Hatchback,
            Sedan,
            Estate,
            Suv,
            Van
        };

        Body body = Body::Hatchback;
        float length = 4.05f;
        float width = 1.73f;
        float height = 1.47f;
        float wheelbase = 2.56f;
        float frontOverhangFraction = 0.55f;   // share of (length - wheelbase) in front of the front axle
        float wheelRadius = 0.302f;
        float tyreWidth = 0.185f;
        float track = 1.46f;                   // wheel centre to wheel centre
        float rideHeight = 0.17f;              // underbody above the ground
        float beltHeight = 0.93f;              // shoulder line at the B-pillar
        float cowlFromFrontAxle = 0.43f;       // windshield base behind the front axle
        float windshieldLength = 0.83f;        // along the car
        float roofRearFromRearAxle = 0.07f;    // where the roof ends (hatchback / estate: rear window top)
        float rearWindowDrop = 0.45f;          // height lost by the rear window (hatchback) or its length share (sedan)
        float bootDeckHeight = 0.0f;           // sedan: boot lid height (0 = no deck)
        float noseHeight = 0.70f;              // hood leading edge
        float hoodRise = 0.14f;                // hood height gained from the nose to the cowl
        float roofCrown = 0.05f;               // roof centre above the roof rail
        float tumblehome = 0.22f;              // roof rail inset from the belt line (per side)
        float sillTuck = 0.05f;                // rocker panel inset from the belt (per side)
        float archFlare = 0.025f;              // fender bulge over the wheel arches
        float noseRounding = 0.32f;            // plan-view corner radius at the nose
        float tailRounding = 0.24f;            // plan-view corner radius at the tail
        bool blackCladding = false;            // SUV: unpainted arches and sills
        int rimSpokes = 5;                     // twin spokes per wheel
        float rimRadius = 0.20f;               // 15/16 inch alloy
        unsigned seed = 1;

        [[nodiscard]] float FrontZ() const { return -(wheelbase * 0.5f + (length - wheelbase) * frontOverhangFraction); }
        [[nodiscard]] float RearZ() const { return wheelbase * 0.5f + (length - wheelbase) * (1.0f - frontOverhangFraction); }
        [[nodiscard]] float FrontAxleZ() const { return -wheelbase * 0.5f; }
        [[nodiscard]] float RearAxleZ() const { return wheelbase * 0.5f; }

        /// Style of the player's vehicle: dimensions and wheels from the definition.
        [[nodiscard]] static CarStyle FromDefinition(const VehicleDefinition& definition);
        /// Traffic preset with typical dimensions of the class; `seed` picks small variations.
        [[nodiscard]] static CarStyle Preset(Body body, unsigned seed);
        [[nodiscard]] static const char* ToString(Body body);
    };

    /// Typical kerb mass of the body class (kg), for traffic collisions.
    [[nodiscard]] float TypicalMassKg(CarStyle::Body body);
}
