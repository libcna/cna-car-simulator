// Data-driven description of a vehicle. Loaded from content/vehicles/<id>.json.
#pragma once

#include "CarSim/Core/PiecewiseLinear.hpp"

#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <array>
#include <string>
#include <vector>

namespace CarSim::Sim
{
    inline constexpr int kVehicleDefinitionSchemaVersion = 1;

    struct ChassisDefinition
    {
        float massKg = 1100.0f;
        float lengthM = 4.0f;
        float widthM = 1.7f;
        float heightM = 1.45f;
        /// Centre of mass in the vehicle frame (x right, y up from the ground plane at rest,
        /// z positive towards the rear; forward is -z).
        Microsoft::Xna::Framework::Vector3 centerOfMass{0.0f, 0.5f, -0.05f};
        /// Principal moments of inertia about the centre of mass (kg m^2): x (pitch), y (yaw), z (roll).
        Microsoft::Xna::Framework::Vector3 inertia{1500.0f, 1800.0f, 400.0f};
        float dragCoefficient = 0.32f;
        float frontalAreaM2 = 2.1f;
        float rollingResistance = 0.012f;
    };

    struct WheelDefinition
    {
        std::string name;                                    // FL, FR, RL, RR
        Microsoft::Xna::Framework::Vector3 position{};       // wheel centre at rest, vehicle frame
        float radiusM = 0.30f;
        float widthM = 0.185f;
        bool steered = false;
        bool driven = false;
        float brakeTorqueNm = 1200.0f;                       // at full pedal
        float handbrakeTorqueNm = 0.0f;
    };

    struct SuspensionDefinition
    {
        float restLengthM = 0.32f;          // spring length at zero load (ray length above the wheel centre)
        float travelM = 0.16f;              // maximum compression
        float springRateNPerM = 26000.0f;
        float damperCompressionNsPerM = 2200.0f;
        float damperReboundNsPerM = 3200.0f;
        float antiRollStiffnessNPerM = 12000.0f;
    };

    struct TyreDefinition
    {
        float peakFriction = 1.0f;          // mu at nominal load
        float peakSlipRatio = 0.12f;        // slip ratio at peak longitudinal force
        float peakSlipAngleDeg = 8.0f;      // slip angle at peak lateral force
        float shapeFactor = 1.6f;           // Pacejka C
        float curvatureFactor = 0.97f;      // Pacejka E
        float loadSensitivity = 0.12f;      // mu drop per +100 % load
        float nominalLoadN = 3000.0f;
        float wheelInertiaKgM2 = 1.1f;
        float lowSpeedMs = 0.8f;            // below this the slip model blends to a viscous model
    };

    struct SteeringDefinition
    {
        float maxWheelAngleDeg = 34.0f;     // at the road wheels
        float steeringRatio = 15.5f;        // steering wheel degrees per road wheel degree
        float wheelTurnRateDegPerSec = 60.0f;   // road-wheel rate for keyboard input at standstill
        float highSpeedFactor = 0.35f;      // fraction of max angle available at highSpeedKmh
        float highSpeedKmh = 130.0f;
        float ackermannFactor = 0.8f;       // 0 = parallel steering, 1 = ideal Ackermann
    };

    struct StarterDefinition
    {
        float crankRpm = 280.0f;
        float crankSeconds = 0.8f;
        float catchRpm = 500.0f;
    };

    struct ThermalDefinition
    {
        float ambientC = 20.0f;
        float operatingC = 90.0f;
        float thermostatOpenC = 87.0f;
        float heatCapacityKjPerK = 25.0f;
        float heatFractionOfFuel = 0.30f;
        float radiatorWPerK = 900.0f;
        float radiatorClosedWPerK = 90.0f;
        float airflowWPerKPerMs = 12.0f;
        float offCoolingWPerK = 25.0f;
        float warningC = 115.0f;
    };

    struct FuelConsumptionDefinition
    {
        float idleLitersPerHour = 0.75f;
        /// Brake-specific fuel consumption (g/kWh) as a function of load fraction 0..1.
        Core::PiecewiseLinear bsfcGPerKwh;
        bool overrunCutoff = true;
    };

    struct EngineDefinition
    {
        float idleRpm = 850.0f;
        float redlineRpm = 6200.0f;
        float limiterRpm = 6500.0f;
        float stallRpm = 450.0f;
        float inertiaKgM2 = 0.18f;
        /// Friction/pumping torque = a + b*rpm + c*rpm^2 (Nm).
        std::array<float, 3> frictionTorque{8.0f, 0.005f, 0.0000006f};
        float displacementLiters = 1.2f;
        int cylinders = 4;
        /// Wide-open-throttle torque (Nm) vs rpm.
        Core::PiecewiseLinear torqueCurve;
        StarterDefinition starter;
        ThermalDefinition thermal;
        FuelConsumptionDefinition fuel;
    };

    struct ClutchDefinition
    {
        float maxTorqueNm = 260.0f;
        float engageStart = 0.25f;          // pedal travel (1 = fully pressed) below which the clutch starts to bite
        float engageEnd = 0.75f;            // pedal travel above which the clutch is fully open
    };

    enum class TransmissionMode
    {
        Manual,
        Automatic
    };

    struct AutomaticDefinition
    {
        Core::PiecewiseLinear upshiftRpm;    // rpm vs throttle
        Core::PiecewiseLinear downshiftRpm;  // rpm vs throttle
        float minShiftIntervalS = 1.5f;
        float creepTorqueNm = 60.0f;
        float lockupSlipRpm = 1400.0f;       // converter model: full capacity this far above idle
    };

    struct GearboxDefinition
    {
        TransmissionMode defaultMode = TransmissionMode::Manual;
        std::vector<float> ratios{3.77f, 2.05f, 1.32f, 0.97f, 0.78f};
        float reverseRatio = 3.60f;
        float finalDrive = 4.06f;
        float efficiency = 0.92f;
        float shiftTimeS = 0.35f;
        AutomaticDefinition automatic;
    };

    struct FuelTankDefinition
    {
        float tankLiters = 45.0f;
        float reserveLiters = 7.0f;
        float refillAtReserveFraction = 0.5f;   // refill when fuel <= reserve * this
        float refillToFraction = 1.0f;          // refill to tank * this
        float initialLiters = 30.0f;
    };

    struct ElectricsDefinition
    {
        float indicatorPeriodS = 0.75f;         // full on/off cycle
    };

    /// Appearance and cockpit geometry used by the renderer and cameras (vehicle frame,
    /// metres, origin at the wheelbase centre on the ground).
    struct VisualDefinition
    {
        std::string bodyStyle = "hatchback";                       // procedural generator preset
        Microsoft::Xna::Framework::Vector3 paintColor{0.62f, 0.10f, 0.12f};   // linear RGB
        Microsoft::Xna::Framework::Vector3 interiorColor{0.16f, 0.16f, 0.17f};
        Microsoft::Xna::Framework::Vector3 driverEye{-0.37f, 1.15f, 0.12f};
        float cockpitFovDeg = 68.0f;
        Microsoft::Xna::Framework::Vector3 steeringWheelCenter{-0.37f, 0.80f, -0.40f};
        float steeringWheelTiltDeg = 24.0f;                        // rim tilted back from vertical
        float steeringWheelDiameterM = 0.37f;
        Microsoft::Xna::Framework::Vector3 mirrorCenter{0.0f, 1.22f, -0.50f};   // interior rear-view mirror
        Microsoft::Xna::Framework::Vector3 clusterCenter{-0.37f, 0.96f, -0.62f};
        std::string plate;   // registration plate text ("1A2 3456"); generated when empty
    };

    struct DashboardDefinition
    {
        float speedometerMaxKmh = 220.0f;
        float tachometerMaxRpm = 7000.0f;
        float temperatureMinC = 50.0f;
        float temperatureMaxC = 130.0f;
    };

    struct VehicleDefinition
    {
        int schemaVersion = kVehicleDefinitionSchemaVersion;
        std::string id;
        std::string displayName;
        ChassisDefinition chassis;
        std::vector<WheelDefinition> wheels;
        SuspensionDefinition suspension;
        TyreDefinition tyres;
        SteeringDefinition steering;
        EngineDefinition engine;
        ClutchDefinition clutch;
        GearboxDefinition gearbox;
        FuelTankDefinition fuel;
        ElectricsDefinition electrics;
        VisualDefinition visual;
        DashboardDefinition dashboard;

        [[nodiscard]] float WheelbaseM() const;
        [[nodiscard]] float FrontTrackM() const;
        [[nodiscard]] float RearTrackM() const;

        /// Returns human-readable problems; empty means the definition is usable.
        [[nodiscard]] std::vector<std::string> Validate() const;
    };

    /// Reference compact hatchback used by tests and as the built-in fallback vehicle.
    [[nodiscard]] VehicleDefinition MakeReferenceVehicle();

    struct VehicleDefinitionLoadResult
    {
        VehicleDefinition definition;
        std::vector<std::string> errors;
        [[nodiscard]] bool ok() const { return errors.empty(); }
    };

    /// Parses a vehicle definition from JSON text. Validation errors are included.
    [[nodiscard]] VehicleDefinitionLoadResult ParseVehicleDefinition(const std::string& jsonText);

    /// Loads and parses `path`. A missing or unreadable file yields an error.
    [[nodiscard]] VehicleDefinitionLoadResult LoadVehicleDefinitionFile(const std::string& path);
}
