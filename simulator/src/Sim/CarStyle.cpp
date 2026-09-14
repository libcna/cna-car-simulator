#include "CarSim/Sim/CarStyle.hpp"

#include <algorithm>
#include <cmath>

namespace CarSim::Sim
{
    const char* CarStyle::ToString(const Body body)
    {
        switch (body) {
            case Body::Hatchback: return "hatchback";
            case Body::Sedan: return "sedan";
            case Body::Estate: return "estate";
            case Body::Suv: return "suv";
            case Body::Van: return "van";
        }
        return "?";
    }

    bool CarStyle::ParseBody(const std::string& text, Body& out)
    {
        if (text == "hatchback") { out = Body::Hatchback; return true; }
        if (text == "sedan") { out = Body::Sedan; return true; }
        if (text == "estate") { out = Body::Estate; return true; }
        if (text == "suv") { out = Body::Suv; return true; }
        if (text == "van") { out = Body::Van; return true; }
        return false;
    }

    CarStyle CarStyle::FromDefinition(const VehicleDefinition& d)
    {
        CarStyle s;
        s.body = d.visual.bodyStyle == "sedan" ? Body::Sedan : d.visual.bodyStyle == "estate" ? Body::Estate
               : d.visual.bodyStyle == "suv" ? Body::Suv : d.visual.bodyStyle == "van" ? Body::Van : Body::Hatchback;
        s.length = d.chassis.lengthM;
        s.width = d.chassis.widthM;
        s.height = d.chassis.heightM;
        s.wheelbase = std::max(2.0f, d.WheelbaseM());
        s.wheelRadius = d.wheels.empty() ? 0.3f : d.wheels.front().radiusM;
        s.tyreWidth = d.wheels.empty() ? 0.185f : d.wheels.front().widthM;
        s.track = d.wheels.size() >= 2 ? std::fabs(d.wheels[1].position.X - d.wheels[0].position.X) : 1.46f;
        if (!d.wheels.empty()) {
            // Front overhang from the wheel positions: the definition places wheels relative to the wheelbase centre.
            s.frontOverhangFraction = 0.55f;
        }
        s.seed = 1;
        return s;
    }

    CarStyle CarStyle::Preset(const Body body, const unsigned seed)
    {
        CarStyle s;
        s.body = body;
        s.seed = seed;
        const float j = (static_cast<float>(seed % 7u) / 6.0f - 0.5f);   // -0.5..0.5 variation
        switch (body) {
            case Body::Hatchback:
                s.length = 4.02f + 0.16f * j; s.width = 1.72f + 0.04f * j; s.height = 1.46f + 0.03f * j; s.wheelbase = 2.55f + 0.06f * j;
                s.beltHeight = 0.93f; s.noseHeight = 0.70f; s.rideHeight = 0.17f;
                break;
            case Body::Sedan:
                s.length = 4.55f + 0.20f * j; s.width = 1.79f + 0.04f * j; s.height = 1.45f + 0.03f * j; s.wheelbase = 2.68f + 0.06f * j;
                s.frontOverhangFraction = 0.50f; s.beltHeight = 0.94f; s.noseHeight = 0.70f; s.bootDeckHeight = 0.96f;
                s.cowlFromFrontAxle = 0.50f; s.windshieldLength = 0.90f; s.roofRearFromRearAxle = -0.10f; s.tailRounding = 0.20f;
                break;
            case Body::Estate:
                s.length = 4.60f + 0.16f * j; s.width = 1.79f + 0.04f * j; s.height = 1.50f + 0.03f * j; s.wheelbase = 2.68f + 0.06f * j;
                s.frontOverhangFraction = 0.48f; s.beltHeight = 0.95f; s.noseHeight = 0.71f; s.roofRearFromRearAxle = 0.62f;
                s.rearWindowDrop = 0.30f; s.tailRounding = 0.18f; s.cowlFromFrontAxle = 0.50f; s.windshieldLength = 0.90f;
                break;
            case Body::Suv:
                s.length = 4.30f + 0.20f * j; s.width = 1.82f + 0.04f * j; s.height = 1.62f + 0.04f * j; s.wheelbase = 2.62f + 0.06f * j;
                s.wheelRadius = 0.33f; s.tyreWidth = 0.215f; s.rideHeight = 0.24f; s.beltHeight = 1.05f; s.noseHeight = 0.82f;
                s.hoodRise = 0.13f; s.archFlare = 0.04f; s.blackCladding = true; s.roofRearFromRearAxle = 0.35f; s.rearWindowDrop = 0.40f;
                s.rimRadius = 0.215f; s.tumblehome = 0.20f; s.cowlFromFrontAxle = 0.48f; s.windshieldLength = 0.78f;
                break;
            case Body::Van:
                s.length = 4.95f + 0.20f * j; s.width = 1.92f + 0.03f * j; s.height = 1.92f + 0.05f * j; s.wheelbase = 3.10f + 0.10f * j;
                s.frontOverhangFraction = 0.55f; s.wheelRadius = 0.32f; s.tyreWidth = 0.205f; s.rideHeight = 0.22f; s.beltHeight = 1.12f;
                s.noseHeight = 0.86f; s.hoodRise = 0.16f; s.cowlFromFrontAxle = 0.10f; s.windshieldLength = 0.72f;
                s.roofRearFromRearAxle = 0.95f; s.rearWindowDrop = 0.05f; s.roofCrown = 0.04f; s.tumblehome = 0.09f;
                s.sillTuck = 0.03f; s.archFlare = 0.015f; s.noseRounding = 0.26f; s.tailRounding = 0.14f; s.rimSpokes = 6;
                break;
        }
        s.track = s.width - 0.27f;
        return s;
    }

    float TypicalMassKg(const CarStyle::Body body)
    {
        switch (body) {
            case CarStyle::Body::Hatchback: return 1150.0f;
            case CarStyle::Body::Sedan: return 1380.0f;
            case CarStyle::Body::Estate: return 1420.0f;
            case CarStyle::Body::Suv: return 1520.0f;
            case CarStyle::Body::Van: return 1950.0f;
        }
        return 1250.0f;
    }
}
