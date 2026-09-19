#include "CarSim/Sim/VehicleDefinition.hpp"

#include "CarSim/Core/JsonReader.hpp"

#include "CarSim/Sim/Units.hpp"

#include "System/Text/Json/JsonDocument.hpp"
#include "System/Text/Json/JsonElement.hpp"
#include "System/Text/Json/JsonValueKind.hpp"

#include <cmath>
#include <exception>
#include <fstream>
#include <functional>
#include <sstream>

namespace CarSim::Sim
{
    using Microsoft::Xna::Framework::Vector3;
    using System::Text::Json::JsonDocument;
    using System::Text::Json::JsonElement;
    using System::Text::Json::JsonValueKind;

    // ------------------------------------------------------------------------------------------
    // Derived quantities and validation
    // ------------------------------------------------------------------------------------------

    namespace
    {
        const WheelDefinition* FindWheel(const VehicleDefinition& def, const std::string& name)
        {
            for (const auto& w : def.wheels) {
                if (w.name == name) {
                    return &w;
                }
            }
            return nullptr;
        }
    }

    float VehicleDefinition::WheelbaseM() const
    {
        const auto* fl = FindWheel(*this, "FL");
        const auto* rl = FindWheel(*this, "RL");
        if (!fl || !rl) {
            return 0.0f;
        }
        return std::fabs(rl->position.Z - fl->position.Z);
    }

    float VehicleDefinition::FrontTrackM() const
    {
        const auto* fl = FindWheel(*this, "FL");
        const auto* fr = FindWheel(*this, "FR");
        return (fl && fr) ? std::fabs(fr->position.X - fl->position.X) : 0.0f;
    }

    float VehicleDefinition::RearTrackM() const
    {
        const auto* rl = FindWheel(*this, "RL");
        const auto* rr = FindWheel(*this, "RR");
        return (rl && rr) ? std::fabs(rr->position.X - rl->position.X) : 0.0f;
    }

    std::vector<std::string> VehicleDefinition::Validate() const
    {
        std::vector<std::string> errors;
        const auto require = [&](bool condition, const std::string& message) {
            if (!condition) {
                errors.push_back(message);
            }
        };

        require(schemaVersion == kVehicleDefinitionSchemaVersion,
                "schemaVersion must be " + std::to_string(kVehicleDefinitionSchemaVersion));
        require(!id.empty(), "id must not be empty");
        require(chassis.massKg > 300.0f && chassis.massKg < 6000.0f, "chassis.mass must be between 300 and 6000 kg");
        require(chassis.lengthM > 2.0f && chassis.widthM > 1.0f && chassis.heightM > 1.0f,
                "chassis dimensions must be plausible (length > 2 m, width > 1 m, height > 1 m)");
        require(chassis.inertia.X > 0.0f && chassis.inertia.Y > 0.0f && chassis.inertia.Z > 0.0f,
                "chassis.inertia components must be positive");
        require(chassis.centerOfMass.Y > 0.2f && chassis.centerOfMass.Y < 1.2f,
                "chassis.centerOfMass height must be between 0.2 and 1.2 m");
        require(chassis.dragCoefficient > 0.1f && chassis.dragCoefficient < 1.0f, "chassis.dragCoefficient out of range");
        require(chassis.frontalAreaM2 > 1.0f && chassis.frontalAreaM2 < 6.0f, "chassis.frontalArea out of range");
        require(chassis.rollingResistance >= 0.005f && chassis.rollingResistance <= 0.03f,
                "chassis.rollingResistance out of range");

        require(wheels.size() == 4, "exactly four wheels (FL, FR, RL, RR) are required");
        for (const char* name : {"FL", "FR", "RL", "RR"}) {
            require(FindWheel(*this, name) != nullptr, std::string("wheel '") + name + "' is missing");
        }
        bool anyDriven = false;
        bool anySteered = false;
        for (const auto& w : wheels) {
            require(w.radiusM > 0.2f && w.radiusM < 0.6f, "wheel " + w.name + " radius out of range");
            require(w.brakeTorqueNm > 0.0f, "wheel " + w.name + " brakeTorque must be positive");
            anyDriven = anyDriven || w.driven;
            anySteered = anySteered || w.steered;
        }
        require(anyDriven, "at least one wheel must be driven");
        require(anySteered, "at least one wheel must be steered");
        if (wheels.size() == 4) {
            require(WheelbaseM() > 1.8f && WheelbaseM() < 4.0f, "wheelbase must be between 1.8 and 4.0 m");
            require(FrontTrackM() > 1.0f && RearTrackM() > 1.0f, "track widths must exceed 1.0 m");
        }

        require(suspension.travelM > 0.05f && suspension.travelM < 0.4f, "suspension.travel out of range");
        require(suspension.restLengthM > suspension.travelM, "suspension.restLength must exceed travel");
        require(suspension.springRateNPerM > 5000.0f, "suspension.springRate too low");
        require(suspension.damperCompressionNsPerM > 0.0f && suspension.damperReboundNsPerM > 0.0f,
                "suspension dampers must be positive");

        require(tyres.peakFriction > 0.3f && tyres.peakFriction < 2.0f, "tyres.peakFriction out of range");
        require(tyres.peakSlipRatio > 0.03f && tyres.peakSlipRatio < 0.5f, "tyres.peakSlipRatio out of range");
        require(tyres.peakSlipAngleDeg > 2.0f && tyres.peakSlipAngleDeg < 20.0f, "tyres.peakSlipAngle out of range");
        require(tyres.shapeFactor > 1.0f && tyres.shapeFactor < 2.5f, "tyres.shapeFactor out of range");
        require(tyres.wheelInertiaKgM2 > 0.1f, "tyres.wheelInertia too small");

        require(steering.maxWheelAngleDeg > 15.0f && steering.maxWheelAngleDeg < 50.0f, "steering.maxWheelAngle out of range");
        require(steering.steeringRatio > 8.0f && steering.steeringRatio < 30.0f, "steering.steeringRatio out of range");

        require(engine.idleRpm > 400.0f && engine.idleRpm < 1500.0f, "engine.idleRpm out of range");
        require(engine.redlineRpm > engine.idleRpm + 1000.0f, "engine.redlineRpm must exceed idle by 1000 rpm");
        require(engine.limiterRpm >= engine.redlineRpm, "engine.limiterRpm must be >= redline");
        require(engine.stallRpm > 100.0f && engine.stallRpm < engine.idleRpm, "engine.stallRpm must be below idle");
        require(engine.inertiaKgM2 > 0.02f && engine.inertiaKgM2 < 2.0f, "engine.inertia out of range");
        require(engine.cylinders >= 1 && engine.cylinders <= 16, "engine.cylinders out of range");
        require(engine.torqueCurve.Size() >= 3, "engine.torqueCurve needs at least three points");
        require(engine.torqueCurve.MaxY() > 20.0f, "engine.torqueCurve peak too low");
        require(engine.torqueCurve.MinX() <= engine.idleRpm && engine.torqueCurve.MaxX() >= engine.redlineRpm,
                "engine.torqueCurve must span idle to redline");
        require(engine.starter.crankSeconds > 0.1f && engine.starter.crankSeconds < 5.0f, "engine.starter.crankSeconds out of range");
        require(engine.starter.catchRpm > engine.starter.crankRpm, "engine.starter.catchRpm must exceed crankRpm");
        require(engine.fuel.bsfcGPerKwh.Size() >= 2, "engine.fuel.bsfcGPerKwh needs at least two points");
        require(engine.fuel.idleLitersPerHour > 0.0f, "engine.fuel.idleLitersPerHour must be positive");
        require(engine.thermal.operatingC > engine.thermal.ambientC + 20.0f, "engine.thermal.operatingC too low");

        require(clutch.maxTorqueNm > engine.torqueCurve.MaxY(), "clutch.maxTorque must exceed peak engine torque");
        require(clutch.engageStart >= 0.0f && clutch.engageStart < clutch.engageEnd && clutch.engageEnd <= 1.0f,
                "clutch engage range must satisfy 0 <= start < end <= 1");

        require(gearbox.ratios.size() >= 3 && gearbox.ratios.size() <= 10, "gearbox.ratios needs 3 to 10 gears");
        for (std::size_t i = 1; i < gearbox.ratios.size(); ++i) {
            require(gearbox.ratios[i] < gearbox.ratios[i - 1], "gearbox.ratios must decrease with gear number");
        }
        for (const float r : gearbox.ratios) {
            require(r > 0.3f && r < 6.0f, "gearbox ratio out of range");
        }
        require(gearbox.reverseRatio > 0.5f, "gearbox.reverse must be positive");
        require(gearbox.finalDrive > 2.0f && gearbox.finalDrive < 7.0f, "gearbox.finalDrive out of range");
        require(gearbox.efficiency > 0.7f && gearbox.efficiency <= 1.0f, "gearbox.efficiency out of range");
        require(gearbox.automatic.upshiftRpm.Size() >= 2 && gearbox.automatic.downshiftRpm.Size() >= 2,
                "gearbox.automatic shift maps need at least two points");
        for (float throttle = 0.0f; throttle <= 1.0f; throttle += 0.25f) {
            require(gearbox.automatic.downshiftRpm.Evaluate(throttle) < gearbox.automatic.upshiftRpm.Evaluate(throttle),
                    "gearbox.automatic downshift line must stay below the upshift line at every throttle");
        }
        require(gearbox.automatic.upshiftRpm.Evaluate(1.0f) <= engine.limiterRpm,
                "gearbox.automatic full-throttle upshift must not exceed the limiter");
        require(gearbox.automatic.stallTorqueRatio >= 1.0f && gearbox.automatic.stallTorqueRatio <= 3.0f,
                "gearbox.automatic.stallTorqueRatio out of range");

        require(fuel.tankLiters > 5.0f && fuel.tankLiters < 200.0f, "fuel.tankLiters out of range");
        require(fuel.reserveLiters > 0.0f && fuel.reserveLiters < fuel.tankLiters, "fuel.reserveLiters must be below tank size");
        require(fuel.refillAtReserveFraction > 0.0f && fuel.refillAtReserveFraction < 1.0f,
                "fuel.refillAtReserveFraction must be in (0, 1)");
        require(fuel.refillToFraction > 0.0f && fuel.refillToFraction <= 1.0f, "fuel.refillToFraction must be in (0, 1]");
        require(fuel.initialLiters >= 0.0f && fuel.initialLiters <= fuel.tankLiters, "fuel.initialLiters out of range");

        require(visual.driverEye.Y > 0.8f && visual.driverEye.Y < 2.0f, "visual.driverEye height must be plausible");
        require(visual.cockpitFovDeg > 40.0f && visual.cockpitFovDeg < 110.0f, "visual.cockpitFovDeg out of range");
        require(visual.steeringWheelDiameterM > 0.25f && visual.steeringWheelDiameterM < 0.5f, "visual.steeringWheelDiameterM out of range");
        require(electrics.indicatorPeriodS >= 0.5f && electrics.indicatorPeriodS <= 1.0f,
                "electrics.indicatorPeriod must be between 0.5 s (120/min) and 1.0 s (60/min)");
        require(dashboard.speedometerMaxKmh > 100.0f && dashboard.tachometerMaxRpm >= engine.limiterRpm,
                "dashboard scales must cover the vehicle's range");
        return errors;
    }

    // ------------------------------------------------------------------------------------------
    // Reference vehicle: a fictional B-segment hatchback with a 1.2 l four-cylinder petrol engine.
    // Values are within the ranges of publicly documented cars of that class (see
    // docs/research/assets.md); they do not copy any single manufacturer's data sheet.
    // ------------------------------------------------------------------------------------------

    VehicleDefinition MakeReferenceVehicle()
    {
        VehicleDefinition def;
        def.id = "lipan_12";
        def.displayName = "Lipan 1.2";

        def.chassis.massKg = 1120.0f;
        def.chassis.lengthM = 4.05f;
        def.chassis.widthM = 1.73f;
        def.chassis.heightM = 1.47f;
        def.chassis.centerOfMass = Vector3(0.0f, 0.52f, -0.10f);
        def.chassis.inertia = Vector3(1500.0f, 1750.0f, 420.0f);
        def.chassis.dragCoefficient = 0.32f;
        def.chassis.frontalAreaM2 = 2.12f;
        def.chassis.rollingResistance = 0.012f;

        const float track = 1.46f;
        const float wheelbase = 2.56f;
        const float radius = 0.302f;   // 185/60 R15
        def.wheels = {
            {"FL", Vector3(-track * 0.5f, radius, -wheelbase * 0.5f), radius, 0.185f, true, true, 1900.0f, 0.0f},
            {"FR", Vector3(track * 0.5f, radius, -wheelbase * 0.5f), radius, 0.185f, true, true, 1900.0f, 0.0f},
            {"RL", Vector3(-track * 0.5f, radius, wheelbase * 0.5f), radius, 0.185f, false, false, 1000.0f, 900.0f},
            {"RR", Vector3(track * 0.5f, radius, wheelbase * 0.5f), radius, 0.185f, false, false, 1000.0f, 900.0f},
        };

        def.suspension = {0.36f, 0.22f, 26000.0f, 2200.0f, 3200.0f, 12000.0f};
        def.tyres = {1.12f, 0.12f, 6.0f, 1.6f, 0.97f, 0.12f, 3000.0f, 1.1f, 0.8f};
        def.steering = {34.0f, 15.5f, 100.0f, 0.5f, 130.0f, 0.8f};

        def.engine.idleRpm = 850.0f;
        def.engine.redlineRpm = 6200.0f;
        def.engine.limiterRpm = 6500.0f;
        def.engine.stallRpm = 450.0f;
        def.engine.inertiaKgM2 = 0.18f;
        def.engine.frictionTorque = {8.0f, 0.005f, 0.0000006f};
        def.engine.displacementLiters = 1.2f;
        def.engine.cylinders = 4;
        def.engine.torqueCurve = Core::PiecewiseLinear({{500.0f, 60.0f}, {800.0f, 85.0f}, {1200.0f, 105.0f},
                                                        {1800.0f, 118.0f}, {2500.0f, 122.0f}, {3500.0f, 121.0f},
                                                        {4500.0f, 115.0f}, {5500.0f, 103.0f}, {6200.0f, 88.0f},
                                                        {6500.0f, 70.0f}});
        def.engine.starter = {280.0f, 0.8f, 500.0f};
        def.engine.thermal = {20.0f, 90.0f, 87.0f, 25.0f, 0.30f, 900.0f, 90.0f, 12.0f, 25.0f, 115.0f};
        def.engine.fuel.idleLitersPerHour = 0.75f;
        def.engine.fuel.bsfcGPerKwh = Core::PiecewiseLinear({{0.1f, 420.0f}, {0.3f, 320.0f}, {0.6f, 265.0f}, {1.0f, 250.0f}});
        def.engine.fuel.overrunCutoff = true;

        def.clutch = {260.0f, 0.25f, 0.75f};

        def.gearbox.defaultMode = TransmissionMode::Manual;
        def.gearbox.ratios = {3.77f, 2.05f, 1.32f, 0.97f, 0.78f};
        def.gearbox.reverseRatio = 3.60f;
        def.gearbox.finalDrive = 4.06f;
        def.gearbox.efficiency = 0.92f;
        def.gearbox.shiftTimeS = 0.35f;
        def.gearbox.automatic.upshiftRpm = Core::PiecewiseLinear({{0.0f, 2100.0f}, {0.5f, 3200.0f}, {1.0f, 6000.0f}});
        def.gearbox.automatic.downshiftRpm = Core::PiecewiseLinear({{0.0f, 1100.0f}, {0.5f, 1700.0f}, {1.0f, 4200.0f}});
        def.gearbox.automatic.minShiftIntervalS = 1.5f;
        def.gearbox.automatic.creepTorqueNm = 18.0f;
        def.gearbox.automatic.lockupSlipRpm = 1400.0f;
        def.gearbox.automatic.stallTorqueRatio = 1.9f;

        def.fuel = {45.0f, 7.0f, 0.5f, 1.0f, 30.0f};
        def.electrics = {0.75f};
        def.dashboard = {420.0f, 7000.0f, 50.0f, 130.0f};
        return def;
    }

    // ------------------------------------------------------------------------------------------
    // JSON parsing
    // ------------------------------------------------------------------------------------------

    namespace
    {
        void ParseInto(const JsonElement& root, VehicleDefinition& def, std::vector<std::string>& errors)
        {
            Core::JsonReader r(errors);
            if (root.getValueKindProperty() != JsonValueKind::Object) {
                errors.push_back("root must be an object");
                return;
            }

            r.Int(root, "schemaVersion", def.schemaVersion, "");
            r.String(root, "id", def.id, "", true);
            r.String(root, "displayName", def.displayName, "");

            const JsonElement chassis = r.RequireObject(root, "chassis", "");
            if (chassis.getValueKindProperty() == JsonValueKind::Object) {
                auto& c = def.chassis;
                r.Float(chassis, "mass", c.massKg, "chassis", true);
                r.Float(chassis, "length", c.lengthM, "chassis", true);
                r.Float(chassis, "width", c.widthM, "chassis", true);
                r.Float(chassis, "height", c.heightM, "chassis", true);
                r.Vec3(chassis, "centerOfMass", c.centerOfMass, "chassis");
                r.Vec3(chassis, "inertia", c.inertia, "chassis");
                r.Float(chassis, "dragCoefficient", c.dragCoefficient, "chassis");
                r.Float(chassis, "frontalArea", c.frontalAreaM2, "chassis");
                r.Float(chassis, "rollingResistance", c.rollingResistance, "chassis");
            }

            JsonElement wheels;
            if (!root.TryGetProperty("wheels", wheels) || wheels.getValueKindProperty() != JsonValueKind::Array) {
                errors.push_back("wheels: array is required");
            } else {
                def.wheels.clear();
                int index = 0;
                for (const auto& item : wheels.EnumerateArray()) {
                    const std::string path = "wheels[" + std::to_string(index++) + "]";
                    if (item.getValueKindProperty() != JsonValueKind::Object) {
                        errors.push_back(path + ": must be an object");
                        continue;
                    }
                    WheelDefinition w;
                    r.String(item, "name", w.name, path, true);
                    r.Vec3(item, "position", w.position, path);
                    r.Float(item, "radius", w.radiusM, path, true);
                    r.Float(item, "width", w.widthM, path);
                    r.Bool(item, "steered", w.steered, path);
                    r.Bool(item, "driven", w.driven, path);
                    r.Float(item, "brakeTorque", w.brakeTorqueNm, path);
                    r.Float(item, "handbrakeTorque", w.handbrakeTorqueNm, path);
                    def.wheels.push_back(std::move(w));
                }
            }

            JsonElement obj;
            if (r.HasObject(root, "suspension", obj)) {
                auto& s = def.suspension;
                r.Float(obj, "restLength", s.restLengthM, "suspension");
                r.Float(obj, "travel", s.travelM, "suspension");
                r.Float(obj, "springRate", s.springRateNPerM, "suspension");
                r.Float(obj, "damperCompression", s.damperCompressionNsPerM, "suspension");
                r.Float(obj, "damperRebound", s.damperReboundNsPerM, "suspension");
                r.Float(obj, "antiRollStiffness", s.antiRollStiffnessNPerM, "suspension");
            }
            if (r.HasObject(root, "tyres", obj)) {
                auto& t = def.tyres;
                r.Float(obj, "peakFriction", t.peakFriction, "tyres");
                r.Float(obj, "peakSlipRatio", t.peakSlipRatio, "tyres");
                r.Float(obj, "peakSlipAngleDeg", t.peakSlipAngleDeg, "tyres");
                r.Float(obj, "shapeFactor", t.shapeFactor, "tyres");
                r.Float(obj, "curvatureFactor", t.curvatureFactor, "tyres");
                r.Float(obj, "loadSensitivity", t.loadSensitivity, "tyres");
                r.Float(obj, "nominalLoad", t.nominalLoadN, "tyres");
                r.Float(obj, "wheelInertia", t.wheelInertiaKgM2, "tyres");
                r.Float(obj, "lowSpeedMs", t.lowSpeedMs, "tyres");
            }
            if (r.HasObject(root, "steering", obj)) {
                auto& s = def.steering;
                r.Float(obj, "maxWheelAngleDeg", s.maxWheelAngleDeg, "steering");
                r.Float(obj, "steeringRatio", s.steeringRatio, "steering");
                r.Float(obj, "wheelTurnRateDegPerSec", s.wheelTurnRateDegPerSec, "steering");
                r.Float(obj, "highSpeedFactor", s.highSpeedFactor, "steering");
                r.Float(obj, "highSpeedKmh", s.highSpeedKmh, "steering");
                r.Float(obj, "ackermannFactor", s.ackermannFactor, "steering");
            }

            const JsonElement engine = r.RequireObject(root, "engine", "");
            if (engine.getValueKindProperty() == JsonValueKind::Object) {
                auto& e = def.engine;
                r.Float(engine, "idleRpm", e.idleRpm, "engine", true);
                r.Float(engine, "redlineRpm", e.redlineRpm, "engine", true);
                r.Float(engine, "limiterRpm", e.limiterRpm, "engine");
                r.Float(engine, "stallRpm", e.stallRpm, "engine");
                r.Float(engine, "inertia", e.inertiaKgM2, "engine");
                std::vector<float> friction;
                r.FloatArray(engine, "frictionTorque", friction, "engine");
                if (!friction.empty()) {
                    if (friction.size() != 3) {
                        errors.push_back("engine.frictionTorque must have three coefficients");
                    } else {
                        e.frictionTorque = {friction[0], friction[1], friction[2]};
                    }
                }
                r.Float(engine, "displacementLiters", e.displacementLiters, "engine");
                r.Int(engine, "cylinders", e.cylinders, "engine");
                r.Curve(engine, "torqueCurve", e.torqueCurve, "engine");
                if (e.torqueCurve.Empty()) {
                    errors.push_back("engine.torqueCurve is required");
                }
                if (r.HasObject(engine, "starter", obj)) {
                    r.Float(obj, "crankRpm", e.starter.crankRpm, "engine.starter");
                    r.Float(obj, "crankSeconds", e.starter.crankSeconds, "engine.starter");
                    r.Float(obj, "catchRpm", e.starter.catchRpm, "engine.starter");
                }
                if (r.HasObject(engine, "thermal", obj)) {
                    auto& t = e.thermal;
                    r.Float(obj, "ambientC", t.ambientC, "engine.thermal");
                    r.Float(obj, "operatingC", t.operatingC, "engine.thermal");
                    r.Float(obj, "thermostatOpenC", t.thermostatOpenC, "engine.thermal");
                    r.Float(obj, "heatCapacityKjPerK", t.heatCapacityKjPerK, "engine.thermal");
                    r.Float(obj, "heatFractionOfFuel", t.heatFractionOfFuel, "engine.thermal");
                    r.Float(obj, "radiatorWPerK", t.radiatorWPerK, "engine.thermal");
                    r.Float(obj, "radiatorClosedWPerK", t.radiatorClosedWPerK, "engine.thermal");
                    r.Float(obj, "airflowWPerKPerMs", t.airflowWPerKPerMs, "engine.thermal");
                    r.Float(obj, "offCoolingWPerK", t.offCoolingWPerK, "engine.thermal");
                    r.Float(obj, "warningC", t.warningC, "engine.thermal");
                }
                if (r.HasObject(engine, "fuel", obj)) {
                    r.Float(obj, "idleLitersPerHour", e.fuel.idleLitersPerHour, "engine.fuel");
                    r.Curve(obj, "bsfcGPerKwh", e.fuel.bsfcGPerKwh, "engine.fuel");
                    r.Bool(obj, "overrunCutoff", e.fuel.overrunCutoff, "engine.fuel");
                }
            }

            if (r.HasObject(root, "clutch", obj)) {
                r.Float(obj, "maxTorque", def.clutch.maxTorqueNm, "clutch");
                r.Float(obj, "engageStart", def.clutch.engageStart, "clutch");
                r.Float(obj, "engageEnd", def.clutch.engageEnd, "clutch");
            }

            const JsonElement gearbox = r.RequireObject(root, "gearbox", "");
            if (gearbox.getValueKindProperty() == JsonValueKind::Object) {
                auto& g = def.gearbox;
                std::string mode;
                r.String(gearbox, "defaultMode", mode, "gearbox");
                if (mode == "automatic") {
                    g.defaultMode = TransmissionMode::Automatic;
                } else if (mode == "manual" || mode.empty()) {
                    g.defaultMode = TransmissionMode::Manual;
                } else {
                    errors.push_back("gearbox.defaultMode must be 'manual' or 'automatic'");
                }
                std::vector<float> ratios;
                r.FloatArray(gearbox, "ratios", ratios, "gearbox");
                if (!ratios.empty()) {
                    g.ratios = std::move(ratios);
                }
                r.Float(gearbox, "reverse", g.reverseRatio, "gearbox");
                r.Float(gearbox, "finalDrive", g.finalDrive, "gearbox", true);
                r.Float(gearbox, "efficiency", g.efficiency, "gearbox");
                r.Float(gearbox, "shiftTime", g.shiftTimeS, "gearbox");
                if (r.HasObject(gearbox, "automatic", obj)) {
                    r.Curve(obj, "upshiftRpm", g.automatic.upshiftRpm, "gearbox.automatic");
                    r.Curve(obj, "downshiftRpm", g.automatic.downshiftRpm, "gearbox.automatic");
                    r.Float(obj, "minShiftInterval", g.automatic.minShiftIntervalS, "gearbox.automatic");
                    r.Float(obj, "creepTorque", g.automatic.creepTorqueNm, "gearbox.automatic");
                    r.Float(obj, "lockupSlipRpm", g.automatic.lockupSlipRpm, "gearbox.automatic");
                    r.Float(obj, "stallTorqueRatio", g.automatic.stallTorqueRatio, "gearbox.automatic");
                }
            }

            if (r.HasObject(root, "fuel", obj)) {
                auto& f = def.fuel;
                r.Float(obj, "tankLiters", f.tankLiters, "fuel", true);
                r.Float(obj, "reserveLiters", f.reserveLiters, "fuel", true);
                r.Float(obj, "refillAtReserveFraction", f.refillAtReserveFraction, "fuel");
                r.Float(obj, "refillToFraction", f.refillToFraction, "fuel");
                r.Float(obj, "initialLiters", f.initialLiters, "fuel");
            } else {
                errors.push_back("fuel: object is required");
            }
            if (r.HasObject(root, "electrics", obj)) {
                r.Float(obj, "indicatorPeriod", def.electrics.indicatorPeriodS, "electrics");
            }
            if (r.HasObject(root, "visual", obj)) {
                auto& vis = def.visual;
                r.String(obj, "bodyStyle", vis.bodyStyle, "visual");
                r.Vec3(obj, "paintColor", vis.paintColor, "visual");
                r.Vec3(obj, "interiorColor", vis.interiorColor, "visual");
                r.Vec3(obj, "driverEye", vis.driverEye, "visual");
                r.Float(obj, "cockpitFovDeg", vis.cockpitFovDeg, "visual");
                r.Vec3(obj, "steeringWheelCenter", vis.steeringWheelCenter, "visual");
                r.Float(obj, "steeringWheelTiltDeg", vis.steeringWheelTiltDeg, "visual");
                r.Float(obj, "steeringWheelDiameterM", vis.steeringWheelDiameterM, "visual");
                r.Vec3(obj, "mirrorCenter", vis.mirrorCenter, "visual");
                r.Vec3(obj, "clusterCenter", vis.clusterCenter, "visual");
                r.String(obj, "plate", vis.plate, "visual");
            }
            if (r.HasObject(root, "dashboard", obj)) {
                auto& d = def.dashboard;
                r.Float(obj, "speedometerMaxKmh", d.speedometerMaxKmh, "dashboard");
                r.Float(obj, "tachometerMaxRpm", d.tachometerMaxRpm, "dashboard");
                r.Float(obj, "temperatureMinC", d.temperatureMinC, "dashboard");
                r.Float(obj, "temperatureMaxC", d.temperatureMaxC, "dashboard");
            }
        }
    }

    VehicleDefinitionLoadResult ParseVehicleDefinition(const std::string& jsonText)
    {
        VehicleDefinitionLoadResult result;
        // Start from the reference so optional sections have sane defaults; required sections
        // are still reported when missing.
        result.definition = MakeReferenceVehicle();
        result.definition.id.clear();
        result.definition.displayName.clear();
        result.definition.wheels.clear();
        result.definition.engine.torqueCurve = Core::PiecewiseLinear();

        std::shared_ptr<JsonDocument> document;
        try {
            document = JsonDocument::Parse(jsonText);
        } catch (const std::exception& error) {
            result.errors.push_back(std::string("invalid JSON: ") + error.what());
            return result;
        }
        if (!document) {
            result.errors.push_back("invalid JSON: parser returned no document");
            return result;
        }

        ParseInto(document->getRootElementProperty(), result.definition, result.errors);
        if (result.errors.empty()) {
            auto validation = result.definition.Validate();
            result.errors.insert(result.errors.end(), validation.begin(), validation.end());
        }
        return result;
    }

    VehicleDefinitionLoadResult LoadVehicleDefinitionFile(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            VehicleDefinitionLoadResult result;
            result.errors.push_back("cannot open vehicle definition '" + path + "'");
            return result;
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();
        auto result = ParseVehicleDefinition(buffer.str());
        for (auto& error : result.errors) {
            error = path + ": " + error;
        }
        return result;
    }
}
