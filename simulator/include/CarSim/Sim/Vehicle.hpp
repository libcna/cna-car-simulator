// Vehicle facade: composes body, wheels, suspension, tyres, engine, clutch, gearbox, fuel,
// thermal, odometer and electrics into one deterministic fixed-step simulation.
#pragma once

#include "CarSim/Sim/Clutch.hpp"
#include "CarSim/Sim/DriverControls.hpp"
#include "CarSim/Sim/Electrics.hpp"
#include "CarSim/Sim/VehicleDamage.hpp"
#include "CarSim/Sim/Engine.hpp"
#include "CarSim/Sim/EngineThermal.hpp"
#include "CarSim/Sim/FuelSystem.hpp"
#include "CarSim/Sim/Ground.hpp"
#include "CarSim/Sim/Odometer.hpp"
#include "CarSim/Sim/RigidBody.hpp"
#include "CarSim/Sim/Transmission.hpp"
#include "CarSim/Sim/Tyre.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace CarSim::Sim
{
    inline constexpr float kPhysicsStepSeconds = 1.0f / 120.0f;

    struct WheelRuntime
    {
        using Vector3 = Microsoft::Xna::Framework::Vector3;

        const WheelDefinition* def = nullptr;
        Vector3 bodyMount{};                 // spring mount in body (centre-of-mass) frame
        float staticCompression = 0.0f;      // compression under the static load share

        float steerAngle = 0.0f;             // radians, positive = turned right
        float spinVelocity = 0.0f;           // rad/s, positive = rolling forward
        float spinAngle = 0.0f;              // accumulated rotation for rendering
        float compression = 0.0f;            // current spring compression (m)
        float compressionVelocity = 0.0f;
        float suspensionForce = 0.0f;        // N along body up
        bool grounded = false;
        GroundHit hit;
        Vector3 contactForward{};
        Vector3 contactRight{};
        Vector3 worldCenter{};               // wheel centre in world space
        float longitudinalSpeed = 0.0f;
        float lateralSpeed = 0.0f;
        float slipRatio = 0.0f;
        float slipAngle = 0.0f;
        TyreForces tyre;
        float driveTorque = 0.0f;
        float brakeTorque = 0.0f;
        bool absActive = false;
    };

    /// Read-only snapshot used by rendering, audio, HUD and traffic.
    struct VehicleState
    {
        using Vector3 = Microsoft::Xna::Framework::Vector3;
        using Quaternion = Microsoft::Xna::Framework::Quaternion;
        using Matrix = Microsoft::Xna::Framework::Matrix;

        Vector3 originPosition{};            // vehicle origin (wheelbase centre on the ground plane at rest)
        Vector3 centerOfMass{};
        Quaternion orientation{0.0f, 0.0f, 0.0f, 1.0f};
        Matrix worldMatrix;                  // body frame to world at the vehicle origin
        Vector3 velocity{};
        float speedMs = 0.0f;                // signed forward speed
        float speedKmh = 0.0f;               // absolute
        float engineRpm = 0.0f;
        float engineLoad = 0.0f;             // delivered torque fraction 0..1 (0 on overrun)
        EngineState engineState = EngineState::Off;
        bool ignitionOn = false;
        TurboMode turboMode = TurboMode::Off;
        bool limitedSlip = false;
        bool autoClutch = false;
        bool headlampsBroken = false;
        bool tailLampsBroken = false;
        bool flightMode = false;
        float rotorAngle = 0.0f;
        float throttlePedal = 0.0f;
        float brakePedal = 0.0f;
        float clutchPedal = 0.0f;
        bool handbrake = false;
        float steeringWheelAngle = 0.0f;     // radians, positive = turned right (clockwise from the driver)
        std::string gearLabel = "N";
        int gear = 0;
        TransmissionMode transmissionMode = TransmissionMode::Manual;
        float fuelLiters = 0.0f;
        float fuelFraction = 0.0f;
        bool reserveWarning = false;
        float coolantC = 20.0f;
        bool temperatureWarning = false;
        double odometerKm = 0.0;
        double tripKm = 0.0;
        float instantConsumptionLPerH = 0.0f;
        bool leftIndicatorLit = false;
        bool rightIndicatorLit = false;
        IndicatorMode indicatorMode = IndicatorMode::Off;
        bool lowBeam = false;
        WiperMode wiperMode = WiperMode::Off;
        float wiperPosition = 0.0f;          // 0 parked .. 1 far end of the sweep
        bool highBeam = false;
        bool brakeLights = false;
        bool reverseLights = false;
        bool horn = false;
        bool limiterActive = false;
        bool absActive = false;
        bool clutchLocked = false;
        struct WheelPose
        {
            Vector3 worldCenter{};
            float steerAngle = 0.0f;
            float spinAngle = 0.0f;
            float compression = 0.0f;
            bool grounded = false;
            float slipRatio = 0.0f;
            float slipAngle = 0.0f;
            float load = 0.0f;
            SurfaceType surface = SurfaceType::Asphalt;   // ground under the contact patch
        };
        std::array<WheelPose, 4> wheels{};
    };

    class Vehicle
    {
    public:
        using Vector3 = Microsoft::Xna::Framework::Vector3;
        using Quaternion = Microsoft::Xna::Framework::Quaternion;

        Vehicle(const VehicleDefinition& definition, TransmissionMode mode);

        /// Places the vehicle at rest with its origin at `originPosition` and the given yaw
        /// (radians about +Y, 0 = facing -Z).
        void PlaceAt(const Vector3& originPosition, float yaw);

        /// Processes the frame's driver intent and advances the simulation by `frameDt` seconds
        /// in fixed sub-steps.
        void Update(const DriverControls& controls, float frameDt, const GroundSurface& ground);

        /// One physics sub-step with the current pedal/steer state (public for tests and traffic).
        void StepPhysics(float dt, const GroundSurface& ground);

        void SetTransmissionMode(TransmissionMode mode);
        /// How wet the road is (0..1). Wet asphalt loses roughly a third of its peak grip, so
        /// the car slides earlier and takes longer to stop.
        void SetRoadWetness(float wetness);
        /// Snow lying on the road (0..1): packed snow and slush take up to half the grip.
        void SetRoadSnow(float snow);
        [[nodiscard]] float RoadWetness() const { return roadWetness_; }

        [[nodiscard]] const VehicleDefinition& Definition() const { return def_; }
        [[nodiscard]] const RigidBody& Body() const { return body_; }
        [[nodiscard]] RigidBody& Body() { return body_; }
        [[nodiscard]] const Engine& GetEngine() const { return engine_; }
        [[nodiscard]] Engine& GetEngine() { return engine_; }
        [[nodiscard]] const Transmission& GetTransmission() const { return *transmission_; }
        [[nodiscard]] Transmission& GetTransmission() { return *transmission_; }
        [[nodiscard]] const FuelSystem& Fuel() const { return fuel_; }
        [[nodiscard]] FuelSystem& Fuel() { return fuel_; }
        [[nodiscard]] const EngineThermal& Thermal() const { return thermal_; }
        [[nodiscard]] EngineThermal& Thermal() { return thermal_; }
        [[nodiscard]] const Odometer& GetOdometer() const { return odometer_; }
        [[nodiscard]] Odometer& GetOdometer() { return odometer_; }
        [[nodiscard]] const Electrics& GetElectrics() const { return electrics_; }
        [[nodiscard]] Electrics& GetElectrics() { return electrics_; }
        [[nodiscard]] const std::array<WheelRuntime, 4>& Wheels() const { return wheels_; }

        [[nodiscard]] float ForwardSpeedMs() const;
        [[nodiscard]] float SpeedKmh() const;
        [[nodiscard]] Vector3 OriginPosition() const;
        [[nodiscard]] float ThrottlePedal() const { return throttlePedal_; }
        [[nodiscard]] float BrakePedal() const { return brakePedal_; }
        [[nodiscard]] float ClutchPedal() const { return clutchPedal_; }
        [[nodiscard]] bool LimitedSlip() const { return limitedSlip_; }
        /// A collision contact (world point, normal pointing into the car, closing speed) that
        /// may dent the body or break a lamp.
        void ApplyImpact(const Vector3& worldPoint, const Vector3& worldNormal, float closingSpeed);
        [[nodiscard]] const VehicleDamage& Damage() const { return damage_; }
        void Repair() { damage_.Repair(); }
        /// Manual gearbox with the clutch worked for the driver: it goes down for every shift,
        /// at a standstill in gear and before the engine would stall, and comes up through the
        /// bite point on its own. The driver's clutch key still works on top.
        [[nodiscard]] bool AutoClutch() const { return autoClutch_; }
        void SetAutoClutch(bool on) { autoClutch_ = on; }
        /// Counts refused shifts (lever moved without the clutch); a HUD hint watches it.
        [[nodiscard]] int GrindCount() const { return grindCount_; }
        void SetLimitedSlip(bool on) { limitedSlip_ = on; }
        [[nodiscard]] bool Handbrake() const { return handbrake_; }
        [[nodiscard]] float SteeringWheelAngle() const;
        [[nodiscard]] bool ClutchLocked() const { return clutchLocked_; }
        [[nodiscard]] bool StartRefused() const { return startRefused_; }
        [[nodiscard]] bool FlightMode() const { return flightMode_; }

        /// Direct pedal override (tests and scripted drives); values are clamped to 0..1.
        void ForcePedals(float throttle, float brake, float clutch);
        /// Sets the wheel steer target immediately (radians at the road wheels).
        void ForceSteerAngle(float radians);
        /// Sets a forward speed and matching wheel spins (tests).
        void ForceForwardSpeed(float speedMs);

        [[nodiscard]] VehicleState Snapshot() const;

    private:
        void ApplyDiscreteControls(const DriverControls& controls);
        void StepFlight(const DriverControls& controls, float dt, const GroundSurface& ground);
        void UpdatePedals(const DriverControls& controls, float dt);
        void UpdateSteering(float dt);
        void UpdateSuspension(float dt, const GroundSurface& ground);
        void ResolveDriveline(float dt);
        void IntegrateWheel(WheelRuntime& wheel, float dt, float driveTorque, float extraInertia);
        /// Integrates every wheel, splitting `torquePerWheel` over the driven axle through the
        /// differential (open: equal; limited slip: biased towards the slower wheel).
        void DriveWheels(float dt, float torquePerWheel, float reflectedInertia);
        void ApplyBodyForces(float dt);
        void ApplySleep();
        [[nodiscard]] float AverageDrivenSpin() const;
        [[nodiscard]] float CouplingCapacity() const;
        [[nodiscard]] float SpeedFactor() const;

        const VehicleDefinition& def_;
        RigidBody body_;
        Engine engine_;
        Clutch clutch_;
        std::unique_ptr<Transmission> transmission_;
        TyreModel tyreModel_;
        FuelSystem fuel_;
        EngineThermal thermal_;
        Odometer odometer_;
        Electrics electrics_;
        std::array<WheelRuntime, 4> wheels_{};
        std::vector<int> drivenWheels_;
        bool limitedSlip_ = false;
        bool autoClutch_ = false;
        VehicleDamage damage_;
        float autoClutchShiftTimer_ = 0.0f;
        int grindCount_ = 0;
        /// Clutch demand of the automatic clutch for this frame (0..1).
        [[nodiscard]] float AutoClutchDemand(const DriverControls& controls, float dt);
        int frontLeft_ = 0, frontRight_ = 1, rearLeft_ = 2, rearRight_ = 3;

        float throttlePedal_ = 0.0f;
        float brakePedal_ = 0.0f;
        float clutchPedal_ = 0.0f;
        float clutchTarget_ = 0.0f;
        bool handbrake_ = false;
        float steerInput_ = 0.0f;
        float steerAngle_ = 0.0f;           // average road-wheel angle (rad, + right)
        bool clutchLocked_ = false;
        bool startRefused_ = false;
        float roadWetness_ = 0.0f;
        float roadSnow_ = 0.0f;
        float accumulator_ = 0.0f;
        float lastSpeedMs_ = 0.0f;
        bool flightMode_ = false;
        float flightYaw_ = 0.0f;
        float rotorAngle_ = 0.0f;
    };
}
