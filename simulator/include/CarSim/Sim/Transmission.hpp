// Gearboxes: manual (driver-operated clutch and gears) and automatic (shift map, converter-like coupling).
#pragma once

#include "CarSim/Sim/VehicleDefinition.hpp"

#include <memory>
#include <string>

namespace CarSim::Sim
{
    /// Gear numbering shared by both gearboxes: -1 reverse, 0 neutral, 1..N forward.
    struct TransmissionContext
    {
        float dt = 0.0f;
        float engineRpm = 0.0f;
        float throttle = 0.0f;        // driver throttle 0..1
        float speedMs = 0.0f;         // vehicle forward speed (signed)
        float clutchPedal = 0.0f;     // 1 = fully pressed (manual only)
        bool clutchRequested = false; // the driver is pressing the clutch, the pedal may still be travelling
        bool engineRunning = false;
        bool brakePressed = false;
        float topGearRatioFactor = 1.0f; // gameplay boost stretches the highest ratio
    };

    enum class AutomaticSelector
    {
        Park,
        Reverse,
        Neutral,
        Drive
    };

    [[nodiscard]] const char* ToString(AutomaticSelector selector);

    class Transmission
    {
    public:
        explicit Transmission(const GearboxDefinition& definition) : def_(definition) {}
        virtual ~Transmission() = default;

        [[nodiscard]] virtual TransmissionMode Mode() const = 0;

        /// Current engaged gear (-1 reverse, 0 neutral, 1..N). During a shift this is 0.
        [[nodiscard]] int Gear() const { return gear_; }

        /// Gear the shift in progress will engage (equals Gear() when not shifting).
        [[nodiscard]] int TargetGear() const { return targetGear_; }

        /// Signed gearbox ratio (negative in reverse, 0 in neutral).
        [[nodiscard]] float Ratio() const;

        /// Total ratio including the final drive (signed).
        [[nodiscard]] float TotalRatio() const { return Ratio() * def_.finalDrive; }

        [[nodiscard]] bool IsShifting() const { return shiftTimer_ > 0.0f; }

        [[nodiscard]] int ForwardGearCount() const { return static_cast<int>(def_.ratios.size()); }

        /// Short label for the dashboard: "N", "R", "1".."6", or P/D for automatics.
        [[nodiscard]] virtual std::string DisplayLabel() const;

        /// Advances timers and, for automatics, decides shifts.
        virtual void Step(const TransmissionContext& context) = 0;

        /// True when a shift request was refused this step (manual: clutch not pressed).
        [[nodiscard]] bool GrindEvent() const { return grindEvent_; }

        /// True on the step a gear engaged (for audio/dashboard feedback).
        [[nodiscard]] bool EngagedEvent() const { return engagedEvent_; }

        [[nodiscard]] const GearboxDefinition& Definition() const { return def_; }

    protected:
        void BeginShift(int newGear);
        void AdvanceShift(float dt);
        void ClearEvents();

        const GearboxDefinition& def_;
        int gear_ = 0;
        int targetGear_ = 0;
        float shiftTimer_ = 0.0f;
        bool grindEvent_ = false;
        bool engagedEvent_ = false;
    };

    class ManualTransmission final : public Transmission
    {
    public:
        explicit ManualTransmission(const GearboxDefinition& definition);

        [[nodiscard]] TransmissionMode Mode() const override { return TransmissionMode::Manual; }
        void Step(const TransmissionContext& context) override;

        /// Requests are applied in the next Step; they need the clutch pressed (pedal above
        /// the clutch's open point) unless the engine is off and the car is stationary. While
        /// the driver is pressing the clutch but the pedal has not yet travelled past the open
        /// point, the request waits for it instead of grinding.
        void RequestShiftUp();
        void RequestShiftDown();
        void RequestGear(int gear);   // -1, 0, 1..N

    private:
        [[nodiscard]] bool ShiftAllowed(const TransmissionContext& context) const;

        int requestedGear_ = 0;
        bool hasRequest_ = false;
        float requestAge_ = 0.0f;
        float clutchOpenPoint_ = 0.6f;
        static constexpr float kRequestPatienceS = 0.4f;
    };

    class AutomaticTransmission final : public Transmission
    {
    public:
        explicit AutomaticTransmission(const GearboxDefinition& definition);

        [[nodiscard]] TransmissionMode Mode() const override { return TransmissionMode::Automatic; }
        void Step(const TransmissionContext& context) override;
        [[nodiscard]] std::string DisplayLabel() const override;

        [[nodiscard]] AutomaticSelector Selector() const { return selector_; }

        /// Selector changes into Reverse or Park are accepted only below walking pace.
        void RequestSelector(AutomaticSelector selector, float speedMs);
        void SelectorUp(float speedMs);    // D -> N -> R -> P
        void SelectorDown(float speedMs);  // P -> R -> N -> D

        /// Torque the internal coupling (torque converter with lock-up approximation) can
        /// transmit at a given engine rpm: creep at idle, full capacity once the engine spins
        /// lockupSlipRpm above idle.
        [[nodiscard]] float CouplingCapacity(float engineRpm, float idleRpm, float maxCapacity) const;

        /// Decision helper exposed for tests: the gear the shift logic wants for the context.
        [[nodiscard]] int DecideGear(const TransmissionContext& context) const;

    private:
        AutomaticSelector selector_ = AutomaticSelector::Park;
        float sinceLastShift_ = 10.0f;
    };

    /// Factory for the vehicle's configured or requested mode.
    [[nodiscard]] std::unique_ptr<Transmission> MakeTransmission(const GearboxDefinition& definition,
                                                                  TransmissionMode mode);
}
