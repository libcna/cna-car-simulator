#include "CarSim/Sim/Vehicle.hpp"

#include "CarSim/Sim/CarStyle.hpp"
#include "CarSim/Sim/Units.hpp"

#include <algorithm>
#include <cmath>

namespace CarSim::Sim
{
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Quaternion;
    using Microsoft::Xna::Framework::Vector3;

    namespace
    {
        float MoveToward(const float current, const float target, const float maxDelta)
        {
            if (target > current) {
                return std::min(current + maxDelta, target);
            }
            return std::max(current - maxDelta, target);
        }

        float Sign(const float v)
        {
            return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f);
        }

        float TopGearFactor(const TurboMode mode)
        {
            switch (mode) {
                case TurboMode::Off: return 1.0f;
                case TurboMode::Turbo: return 0.88f;
                case TurboMode::Ultra: return 0.53f;
                case TurboMode::UltraUltra: return 0.40f;
            }
            return 1.0f;
        }

        float DragFactor(const TurboMode mode)
        {
            switch (mode) {
                case TurboMode::Off: return 1.0f;
                case TurboMode::Turbo: return 0.65f;
                case TurboMode::Ultra: return 0.40f;
                case TurboMode::UltraUltra: return 0.40f;
            }
            return 1.0f;
        }

        int WheelIndex(const VehicleDefinition& def, const char* name)
        {
            for (std::size_t i = 0; i < def.wheels.size(); ++i) {
                if (def.wheels[i].name == name) {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }
    }

    Vehicle::Vehicle(const VehicleDefinition& definition, const TransmissionMode mode)
        : def_(definition),
          body_(def_.chassis.massKg, def_.chassis.inertia),
          engine_(def_.engine),
          clutch_(def_.clutch),
          transmission_(MakeTransmission(def_.gearbox, mode)),
          tyreModel_(def_.tyres),
          fuel_(def_.fuel, def_.engine.fuel),
          thermal_(def_.engine.thermal),
          electrics_(def_.electrics)
    {
        frontLeft_ = std::max(0, WheelIndex(def_, "FL"));
        frontRight_ = std::max(0, WheelIndex(def_, "FR"));
        rearLeft_ = std::max(0, WheelIndex(def_, "RL"));
        rearRight_ = std::max(0, WheelIndex(def_, "RR"));

        // Static load share from the centre of mass position along the wheelbase.
        const float wheelbase = std::max(0.5f, def_.WheelbaseM());
        const float frontZ = def_.wheels[static_cast<std::size_t>(frontLeft_)].position.Z;
        const float comZ = def_.chassis.centerOfMass.Z;
        const float frontShare = std::clamp(1.0f - (comZ - frontZ) / wheelbase, 0.2f, 0.8f);
        const float weight = def_.chassis.massKg * Units::kGravity;

        for (std::size_t i = 0; i < wheels_.size() && i < def_.wheels.size(); ++i) {
            auto& w = wheels_[i];
            w.def = &def_.wheels[i];
            const bool front = w.def->position.Z < comZ;
            const float share = (front ? frontShare : 1.0f - frontShare) * 0.5f;
            w.staticCompression = std::clamp(weight * share / def_.suspension.springRateNPerM, 0.0f,
                                             def_.suspension.travelM * 0.8f);
            const Vector3 rel = w.def->position - def_.chassis.centerOfMass;
            w.bodyMount = Vector3(rel.X, rel.Y + (def_.suspension.restLengthM - w.staticCompression), rel.Z);
            if (w.def->driven) {
                drivenWheels_.push_back(static_cast<int>(i));
            }
        }
        limitedSlip_ = def_.differential.limitedSlip;
        PlaceAt(Vector3(0.0f, 0.0f, 0.0f), 0.0f);
    }

    void Vehicle::PlaceAt(const Vector3& originPosition, const float yaw)
    {
        const Quaternion q = Quaternion::CreateFromAxisAngle(Vector3::Up, yaw);
        body_.SetOrientation(q);
        body_.SetPosition(originPosition + Vector3::Transform(def_.chassis.centerOfMass, q));
        body_.SetLinearVelocity(Vector3(0.0f, 0.0f, 0.0f));
        body_.SetAngularVelocity(Vector3(0.0f, 0.0f, 0.0f));
        body_.ClearAccumulators();
        for (auto& w : wheels_) {
            w.spinVelocity = 0.0f;
            w.compression = w.staticCompression;
            w.compressionVelocity = 0.0f;
            w.grounded = false;
        }
        clutchLocked_ = false;
        accumulator_ = 0.0f;
        lastSpeedMs_ = 0.0f;
    }

    void Vehicle::ApplyImpact(const Vector3& worldPoint, const Vector3& worldNormal, const float closingSpeed)
    {
        if (flightMode_) return;
        const Matrix world = body_.Rotation() * Matrix::CreateTranslation(OriginPosition());
        const Matrix toBody = Matrix::Invert(world);
        const Vector3 local = Vector3::Transform(worldPoint, toBody);
        const Vector3 inward = Vector3::TransformNormal(worldNormal, toBody);
        const CarStyle style = CarStyle::FromDefinition(def_);
        damage_.AddImpact(local, inward, closingSpeed, style.FrontZ(), style.RearZ());
    }

    void Vehicle::SetRoadSnow(const float snow)
    {
        roadSnow_ = std::clamp(snow, 0.0f, 1.0f);
    }

    void Vehicle::SetRoadWetness(const float wetness)
    {
        roadWetness_ = std::clamp(wetness, 0.0f, 1.0f);
    }

    void Vehicle::SetTransmissionMode(const TransmissionMode mode)
    {
        if (transmission_->Mode() == mode) {
            return;
        }
        transmission_ = MakeTransmission(def_.gearbox, mode);
        clutchLocked_ = false;
    }

    float Vehicle::ForwardSpeedMs() const
    {
        return Vector3::Dot(body_.LinearVelocity(), body_.Forward());
    }

    float Vehicle::SpeedKmh() const
    {
        return Units::MsToKmh(std::fabs(ForwardSpeedMs()));
    }

    Vector3 Vehicle::OriginPosition() const
    {
        return body_.ToWorldPoint(-def_.chassis.centerOfMass);
    }

    float Vehicle::SteeringWheelAngle() const
    {
        return steerAngle_ * def_.steering.steeringRatio;
    }

    void Vehicle::ForcePedals(const float throttle, const float brake, const float clutch)
    {
        throttlePedal_ = std::clamp(throttle, 0.0f, 1.0f);
        brakePedal_ = std::clamp(brake, 0.0f, 1.0f);
        clutchPedal_ = std::clamp(clutch, 0.0f, 1.0f);
    }

    void Vehicle::ForceSteerAngle(const float radians)
    {
        const float maxAngle = Units::DegToRad(def_.steering.maxWheelAngleDeg);
        steerAngle_ = std::clamp(radians, -maxAngle, maxAngle);
        steerInput_ = steerAngle_ / maxAngle;
    }

    void Vehicle::ForceForwardSpeed(const float speedMs)
    {
        body_.SetLinearVelocity(body_.Forward() * speedMs);
        for (auto& w : wheels_) {
            w.spinVelocity = speedMs / w.def->radiusM;
        }
        lastSpeedMs_ = speedMs;
    }

    // ------------------------------------------------------------------------------------------

    void Vehicle::Update(const DriverControls& controls, const float frameDt, const GroundSurface& ground)
    {
        if (controls.toggleFlight) {
            ToggleFlight(ground);
        }
        const float clamped = std::clamp(frameDt, 0.0f, 0.25f);
        DriverControls effective = controls;
        if (autoClutch_ && transmission_->Mode() == TransmissionMode::Manual && !flightMode_) {
            effective.clutch = std::max(effective.clutch, AutoClutchDemand(controls, clamped));
        }
        ApplyDiscreteControls(effective);
        accumulator_ += clamped;
        int steps = 0;
        while (accumulator_ >= kPhysicsStepSeconds && steps < 30) {
            if (flightMode_) {
                StepFlight(effective, kPhysicsStepSeconds, ground);
            } else {
                UpdatePedals(effective, kPhysicsStepSeconds);
                StepPhysics(kPhysicsStepSeconds, ground);
            }
            accumulator_ -= kPhysicsStepSeconds;
            ++steps;
        }
    }

    float Vehicle::AutoClutchDemand(const DriverControls& controls, const float dt)
    {
        // A shift asked for this frame: press the clutch for as long as the lever needs.
        if (controls.shiftUp || controls.shiftDown || controls.selectGear) {
            autoClutchShiftTimer_ = def_.gearbox.shiftTimeS + 0.15f;
        }
        autoClutchShiftTimer_ = std::max(0.0f, autoClutchShiftTimer_ - dt);
        if (autoClutchShiftTimer_ > 0.0f || transmission_->IsShifting()) {
            return 1.0f;
        }
        if (transmission_->Gear() == 0 || !engine_.IsRunning()) {
            return 0.0f;
        }
        // In gear: held down while standing without throttle, and whenever the engine is being
        // dragged towards a stall; released (through the pedal model's bite-point logic) as
        // soon as the driver asks for power.
        const float speed = std::fabs(ForwardSpeedMs());
        const float wheelRpm = Units::RadSToRpm(std::fabs(transmission_->TotalRatio() * AverageDrivenSpin()));
        const bool stalling = wheelRpm < def_.engine.idleRpm + 80.0f && throttlePedal_ < 0.1f;
        if (controls.throttle < 0.05f && (speed < 1.5f || stalling)) {
            return 1.0f;
        }
        return 0.0f;
    }

    void Vehicle::ApplyDiscreteControls(const DriverControls& controls)
    {
        startRefused_ = false;
        if (controls.toggleTurbo) {
            engine_.CycleTurboMode();
        }
        if (controls.toggleDifferential) {
            limitedSlip_ = !limitedSlip_;
        }
        if (controls.toggleAutoClutch) {
            autoClutch_ = !autoClutch_;
        }
        if (controls.toggleTransmissionMode) {
            SetTransmissionMode(transmission_->Mode() == TransmissionMode::Manual ? TransmissionMode::Automatic
                                                                                  : TransmissionMode::Manual);
        }

        if (controls.toggleEngine) {
            if (engine_.IgnitionOn()) {
                engine_.RequestStop();
            } else {
                // Starter interlock: neutral, clutch pressed, or an automatic in P/N.
                bool allowed = transmission_->Gear() == 0 && !transmission_->IsShifting();
                if (transmission_->Mode() == TransmissionMode::Manual) {
                    allowed = allowed || clutchPedal_ > 0.6f || controls.clutch > 0.6f;
                } else {
                    const auto* automatic = static_cast<const AutomaticTransmission*>(transmission_.get());
                    allowed = automatic->Selector() == AutomaticSelector::Park ||
                              automatic->Selector() == AutomaticSelector::Neutral;
                }
                if (allowed) {
                    engine_.RequestStart();
                } else {
                    startRefused_ = true;
                }
            }
        }

        if (transmission_->Mode() == TransmissionMode::Manual) {
            auto* manual = static_cast<ManualTransmission*>(transmission_.get());
            if (controls.selectGear) {
                manual->RequestGear(*controls.selectGear);
            } else if (controls.shiftUp) {
                manual->RequestShiftUp();
            } else if (controls.shiftDown) {
                manual->RequestShiftDown();
            }
        } else {
            auto* automatic = static_cast<AutomaticTransmission*>(transmission_.get());
            const float speed = ForwardSpeedMs();
            if (controls.selector) {
                automatic->RequestSelector(*controls.selector, speed);
            } else if (controls.shiftUp) {
                automatic->SelectorUp(speed);
            } else if (controls.shiftDown) {
                automatic->SelectorDown(speed);
            }
        }

        electrics_.ApplyIndicator(controls.indicator);
        if (controls.toggleHeadlights) {
            electrics_.ToggleHeadlights();
        }
        if (controls.toggleHighBeam) {
            electrics_.ToggleHighBeam();
        }
        if (controls.cycleWipers) {
            electrics_.CycleWipers();
        }
        electrics_.SetHorn(controls.horn);
        handbrake_ = controls.handbrake;
        steerInput_ = std::clamp(controls.steering, -1.0f, 1.0f);
        if (controls.resetTrip) {
            odometer_.ResetTrip();
            fuel_.ResetTrip();
        }
    }

    void Vehicle::UpdatePedals(const DriverControls& controls, const float dt)
    {
        // Pedal travel rates give keyboard input a human feel: the accelerator and brake move
        // quickly, the clutch releases slowly enough for a controlled engagement. The brake is
        // applied the way a driver stamps on it -- full pressure in a tenth of a second.
        const float throttleTarget = std::clamp(controls.throttle, 0.0f, 1.0f);
        throttlePedal_ = MoveToward(throttlePedal_, throttleTarget, (throttleTarget > throttlePedal_ ? 4.0f : 8.0f) * dt);
        const float brakeTarget = std::clamp(controls.brake, 0.0f, 1.0f);
        brakePedal_ = MoveToward(brakePedal_, brakeTarget, (brakeTarget > brakePedal_ ? 16.0f : 8.0f) * dt);
        // Clutch: pressed quickly; released quickly down to the bite point, then eased through
        // the engagement band the way a driver's foot does: the release slows to a trickle while
        // the clutch would demand more torque than the engine can give, and resumes once the
        // speeds have matched (locked) or the engine has torque in hand.
        const float clutchTarget = std::clamp(controls.clutch, 0.0f, 1.0f);
        clutchTarget_ = clutchTarget;
        float clutchRate = 8.0f;
        if (clutchTarget < clutchPedal_) {
            clutchRate = 4.0f;
            const auto& cd = def_.clutch;
            // Only a clutch that slows the engine down can stall it. In neutral it carries
            // nothing, and when the wheels turn the gearbox input faster than the engine (a gear
            // engaged on the move with the throttle closed, or a downshift) it pulls the engine
            // up instead.
            const float ratio = transmission_->IsShifting() ? 0.0f : transmission_->TotalRatio();
            const bool engineLoaded = std::fabs(ratio) > 1e-3f &&
                                      engine_.AngularVelocity() > std::fabs(ratio * AverageDrivenSpin());
            if (clutchPedal_ <= cd.engageEnd + 0.02f && clutchPedal_ > cd.engageStart - 0.02f) {
                clutchRate = 0.5f;
                if (engine_.IsRunning() && !clutchLocked_ && engineLoaded) {
                    // Do not let the clutch demand more than the engine can deliver: release
                    // down to the bite point for the available torque, then wait there while the
                    // engine is near idle. With revs in hand the foot keeps coming up -- a pedal
                    // parked below the engine's torque would slip at the limiter for ever.
                    const float available = std::max(0.0f, engine_.NetTorque(throttlePedal_));
                    const float floor = clutch_.PedalForCapacity(available * 0.9f / engine_.PowerMultiplier());
                    if (clutchPedal_ - clutchRate * dt <= floor) {
                        const float headroom = std::clamp((engine_.Rpm() - def_.engine.idleRpm - 300.0f) / 1500.0f, 0.0f, 1.0f);
                        clutchRate = 0.5f * headroom;
                    }
                }
            }
        }
        clutchPedal_ = MoveToward(clutchPedal_, clutchTarget, clutchRate * dt);
    }

    float Vehicle::SpeedFactor() const
    {
        const float t = std::clamp(SpeedKmh() / std::max(10.0f, def_.steering.highSpeedKmh), 0.0f, 1.0f);
        return 1.0f + (def_.steering.highSpeedFactor - 1.0f) * t;
    }

    void Vehicle::UpdateSteering(const float dt)
    {
        const float maxAngle = Units::DegToRad(def_.steering.maxWheelAngleDeg);
        const float factor = SpeedFactor();
        float allowedAngle = maxAngle * factor;
        if (engine_.TurboSetting() == TurboMode::UltraUltra) {
            // Keep keyboard steering controllable at high speed while allowing a useful
            // turning radius. The boosted mode's tyre forces are applied at the roll centre
            // below, so cornering need not be limited to ordinary-car lateral acceleration.
            const Vector3 velocity = body_.LinearVelocity();
            const float speed = std::hypot(velocity.X, velocity.Z);
            const float lateralLimit = 4.0f * Units::kGravity;
            const float safeAngle = std::atan(lateralLimit * std::max(0.5f, def_.WheelbaseM()) /
                                              std::max(speed * speed, 1.0f));
            allowedAngle = std::min(allowedAngle, safeAngle);
        }
        const float target = steerInput_ * allowedAngle;
        const float rate = Units::DegToRad(def_.steering.wheelTurnRateDegPerSec) * (0.6f + 0.4f * factor);
        const bool returning = std::fabs(target) < std::fabs(steerAngle_);
        steerAngle_ = MoveToward(steerAngle_, target, rate * (returning ? 1.6f : 1.0f) * dt);

        // Ackermann: the inner wheel turns more than the outer wheel.
        const float wheelbase = std::max(0.5f, def_.WheelbaseM());
        const float track = std::max(0.5f, def_.FrontTrackM());
        const float a = std::fabs(steerAngle_);
        float inner = a;
        float outer = a;
        if (a > 1e-4f) {
            const float radius = wheelbase / std::tan(a);
            inner = std::atan(wheelbase / std::max(0.2f, radius - track * 0.5f));
            outer = std::atan(wheelbase / (radius + track * 0.5f));
            const float k = std::clamp(def_.steering.ackermannFactor, 0.0f, 1.0f);
            inner = a + (inner - a) * k;
            outer = a + (outer - a) * k;
        }
        for (auto& w : wheels_) {
            if (!w.def->steered) {
                w.steerAngle = 0.0f;
                continue;
            }
            const bool leftWheel = w.def->position.X < 0.0f;
            const bool turningRight = steerAngle_ > 0.0f;
            const bool isInner = leftWheel != turningRight;
            w.steerAngle = Sign(steerAngle_) * (isInner ? inner : outer);
        }
    }

    void Vehicle::ResolveDriveline(const float dt)
    {
        // An automatic's controller withdraws engine torque while it changes gear; without that
        // a full-throttle upshift flares the unloaded engine into the limiter every time.
        const bool automaticShift =
            transmission_->Mode() == TransmissionMode::Automatic && transmission_->IsShifting();
        const float driverThrottle = automaticShift ? 0.0f : throttlePedal_;
        const bool fuel = fuel_.HasFuel();
        // Taller top gearing lets the extra output reach the requested speeds before the
        // stock engine's limiter. Lower gears keep their launch and overtaking leverage.
        const bool topGear = transmission_->Gear() == transmission_->ForwardGearCount();
        const float topGearFactor = topGear ? TopGearFactor(engine_.TurboSetting()) : 1.0f;
        const float ratio = transmission_->IsShifting() ? 0.0f : transmission_->TotalRatio() * topGearFactor;
        const bool engaged = std::fabs(ratio) > 1e-3f && !drivenWheels_.empty();
        const float capacity = engaged ? CouplingCapacity() : 0.0f;
        const float efficiency = def_.gearbox.efficiency;
        const auto n = static_cast<float>(std::max<std::size_t>(1, drivenWheels_.size()));

        if (!engaged || capacity <= 0.01f) {
            clutchLocked_ = false;
            engine_.Step(dt, driverThrottle, fuel, true, 0.0f);
            DriveWheels(dt, 0.0f, 0.0f);
            return;
        }

        const float omegaIn = ratio * AverageDrivenSpin();
        const float slip = engine_.AngularVelocity() - omegaIn;

        if (clutchLocked_) {
            const float engineTorque = engine_.NetTorque(driverThrottle);
            const float wheelTorque = engineTorque * ratio * efficiency / n;
            const float reflected = engine_.Inertia() * ratio * ratio / n;
            DriveWheels(dt, wheelTorque, reflected);
            const float newOmegaIn = ratio * AverageDrivenSpin();
            const float transmitted = engineTorque - engine_.Inertia() * (newOmegaIn - engine_.AngularVelocity()) / dt;
            engine_.SetAngularVelocity(std::max(0.0f, newOmegaIn));
            engine_.Step(dt, driverThrottle, fuel, false, 0.0f);
            if (std::fabs(transmitted) > capacity || newOmegaIn < 0.0f) {
                clutchLocked_ = false;
            }
            return;
        }

        // Slipping: Coulomb torque through the clutch, viscous inside a narrow band so the speeds
        // converge without chatter.
        const float viscous = 0.5f * engine_.Inertia() / dt;
        const float clutchTorque = std::clamp(viscous * slip, -capacity, capacity);
        // A torque converter multiplies the engine's torque while its turbine runs slower than its
        // impeller: stallTorqueRatio with the output held, falling to 1:1 at the coupling point.
        // Its efficiency (multiplication x speed ratio) stays at or below 0.85, so it trades slip
        // for torque and never adds energy. Only when driving -- on overrun it couples 1:1.
        float multiplication = 1.0f;
        const float impeller = omegaIn + slip;
        if (transmission_->Mode() == TransmissionMode::Automatic && clutchTorque > 0.0f && impeller > 1.0f) {
            constexpr float kCouplingPoint = 0.85f;
            const float speedRatio = std::clamp(omegaIn / impeller, 0.0f, 1.0f);
            multiplication = 1.0f + (def_.gearbox.automatic.stallTorqueRatio - 1.0f) *
                                        std::max(0.0f, 1.0f - speedRatio / kCouplingPoint);
        }
        engine_.Step(dt, driverThrottle, fuel, true, clutchTorque);
        const float wheelTorque = clutchTorque * multiplication * ratio * efficiency / n;
        DriveWheels(dt, wheelTorque, 0.0f);
        const float newSlip = engine_.AngularVelocity() - ratio * AverageDrivenSpin();
        const bool crossed = Sign(newSlip) != Sign(slip);
        // An automatic's converter never locks below idle; a manual clutch can (and then stalls).
        const bool speedOk = transmission_->Mode() == TransmissionMode::Manual ||
                             ratio * AverageDrivenSpin() > Units::RpmToRadS(def_.engine.idleRpm + 100.0f);
        if (speedOk && (std::fabs(newSlip) < 12.0f || crossed) && std::fabs(clutchTorque) < capacity * 0.98f) {
            clutchLocked_ = true;
            engine_.SetAngularVelocity(std::max(0.0f, ratio * AverageDrivenSpin()));
        }
    }

    void Vehicle::ApplyBodyForces(const float dt)
    {
        (void)dt;
        const auto& c = def_.chassis;
        body_.ApplyCentralForce(Vector3(0.0f, -c.massKg * Units::kGravity, 0.0f));
        const Vector3 v = body_.LinearVelocity();
        const float speed = v.Length();
        if (speed > 0.01f) {
            // These gameplay modes trim drag so the extra power reaches their target speeds.
            const float dragFactor = DragFactor(engine_.TurboSetting());
            const float drag = c.dragCoefficient * dragFactor;
            const float dragMagnitude = 0.5f * Units::kAirDensity * drag * c.frontalAreaM2 * speed * speed;
            body_.ApplyCentralForce(v * (-dragMagnitude / speed));
        }
    }

    void Vehicle::ApplySleep()
    {
        const Vector3 v = body_.LinearVelocity();
        const bool holding = brakePedal_ > 0.3f || handbrake_;
        if (holding && v.LengthSquared() < 0.03f * 0.03f && body_.AngularVelocity().LengthSquared() < 0.02f * 0.02f) {
            bool allGrounded = true;
            for (const auto& w : wheels_) {
                allGrounded = allGrounded && w.grounded;
            }
            if (allGrounded) {
                body_.SetLinearVelocity(Vector3(0.0f, 0.0f, 0.0f));
                body_.SetAngularVelocity(Vector3(0.0f, 0.0f, 0.0f));
                for (auto& w : wheels_) {
                    w.spinVelocity = 0.0f;
                }
            }
        }
    }

    void Vehicle::StepPhysics(const float dt, const GroundSurface& ground)
    {
        UpdateSteering(dt);
        UpdateSuspension(dt, ground);

        TransmissionContext context;
        context.dt = dt;
        context.engineRpm = engine_.Rpm();
        context.throttle = throttlePedal_;
        context.speedMs = ForwardSpeedMs();
        context.clutchPedal = clutchPedal_;
        context.clutchRequested = clutchTarget_ > 0.6f;
        context.engineRunning = engine_.IsRunning();
        context.brakePressed = brakePedal_ > 0.1f;
        context.topGearRatioFactor = TopGearFactor(engine_.TurboSetting());
        transmission_->Step(context);
        if (transmission_->GrindEvent()) ++grindCount_;

        ResolveDriveline(dt);
        ApplyBodyForces(dt);
        body_.Integrate(dt);
        ApplySleep();

        const float speed = ForwardSpeedMs();
        odometer_.Add(speed, dt);
        fuel_.Step(dt, engine_);
        thermal_.Step(dt, engine_, fuel_.MassFlowGramsPerSecond(engine_), speed);
        const float lock = Units::DegToRad(def_.steering.maxWheelAngleDeg);
        electrics_.TrackSteering(lock > 1e-4f ? steerAngle_ / lock : 0.0f);
        electrics_.Step(dt, engine_.IgnitionOn());
        lastSpeedMs_ = speed;
    }

    VehicleState Vehicle::Snapshot() const
    {
        VehicleState s;
        s.originPosition = OriginPosition();
        s.centerOfMass = body_.Position();
        s.orientation = body_.Orientation();
        s.worldMatrix = body_.Rotation() * Matrix::CreateTranslation(s.originPosition);
        s.velocity = body_.LinearVelocity();
        s.speedMs = ForwardSpeedMs();
        s.speedKmh = SpeedKmh();
        s.engineRpm = engine_.Rpm();
        s.engineLoad = std::clamp(engine_.LoadFraction(), 0.0f, 1.0f);
        s.engineState = engine_.State();
        s.ignitionOn = engine_.IgnitionOn();
        s.turboMode = engine_.TurboSetting();
        s.wiperMode = electrics_.Wipers();
        s.wiperPosition = electrics_.WiperPosition();
        s.limitedSlip = limitedSlip_;
        s.headlampsBroken = damage_.HeadlampsBroken();
        s.tailLampsBroken = damage_.TailLampsBroken();
        s.autoClutch = autoClutch_;
        s.flightMode = flightMode_;
        s.rotorAngle = rotorAngle_;
        s.throttlePedal = throttlePedal_;
        s.brakePedal = brakePedal_;
        s.clutchPedal = clutchPedal_;
        s.handbrake = handbrake_;
        s.steeringWheelAngle = SteeringWheelAngle();
        s.gearLabel = flightMode_ ? "FLIGHT" : transmission_->DisplayLabel();
        s.gear = transmission_->Gear();
        s.transmissionMode = transmission_->Mode();
        s.fuelLiters = fuel_.Liters();
        s.fuelFraction = fuel_.Fraction();
        s.reserveWarning = fuel_.ReserveWarning();
        s.coolantC = thermal_.CoolantC();
        s.temperatureWarning = thermal_.Warning();
        s.odometerKm = odometer_.TotalKm();
        s.tripKm = odometer_.TripKm();
        s.instantConsumptionLPerH = fuel_.LitersPerHour();
        s.leftIndicatorLit = electrics_.LeftIndicatorLit();
        s.rightIndicatorLit = electrics_.RightIndicatorLit();
        s.indicatorMode = electrics_.Indicator();
        s.lowBeam = flightMode_ || electrics_.LowBeamOn();
        s.highBeam = flightMode_ ? electrics_.Headlights() == HeadlightMode::High : electrics_.HighBeamOn();
        s.brakeLights = engine_.IgnitionOn() && brakePedal_ > 0.05f;
        s.reverseLights = engine_.IgnitionOn() && (transmission_->IsShifting() ? transmission_->TargetGear() : transmission_->Gear()) < 0;
        s.horn = electrics_.Horn();
        s.limiterActive = engine_.LimiterActive();
        s.clutchLocked = clutchLocked_;
        bool abs = false;
        for (std::size_t i = 0; i < wheels_.size(); ++i) {
            const auto& w = wheels_[i];
            auto& p = s.wheels[i];
            p.worldCenter = w.worldCenter;
            p.steerAngle = w.steerAngle;
            p.spinAngle = w.spinAngle;
            p.compression = w.compression;
            p.grounded = w.grounded;
            p.slipRatio = w.slipRatio;
            p.slipAngle = w.slipAngle;
            p.load = w.suspensionForce;
            p.surface = w.hit.surface;
            abs = abs || w.absActive;
        }
        s.absActive = abs;
        return s;
    }
}
