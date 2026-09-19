#include "CarSim/Sim/Vehicle.hpp"

#include "CarSim/Sim/Units.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

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
          body_(definition.chassis.massKg, definition.chassis.inertia),
          engine_(definition.engine),
          clutch_(definition.clutch),
          transmission_(MakeTransmission(definition.gearbox, mode)),
          tyreModel_(definition.tyres),
          fuel_(definition.fuel, definition.engine.fuel),
          thermal_(definition.engine.thermal),
          electrics_(definition.electrics)
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
            flightMode_ = !flightMode_;
            if (flightMode_) {
                const Vector3 origin = OriginPosition();
                const float lift = std::max(2.5f, ground.HeightAt(origin.X, origin.Z) + 2.5f - origin.Y);
                body_.SetPosition(body_.Position() + Vector3(0.0f, lift, 0.0f));
                body_.SetLinearVelocity(Vector3(0.0f, 0.0f, 0.0f));
                body_.SetAngularVelocity(Vector3(0.0f, 0.0f, 0.0f));
                body_.ClearAccumulators();
                flightYaw_ = std::atan2(-body_.Forward().X, -body_.Forward().Z);
                for (auto& wheel : wheels_) wheel.grounded = false;
            } else {
                const Vector3 origin = OriginPosition();
                PlaceAt(Vector3(origin.X, ground.HeightAt(origin.X, origin.Z), origin.Z), flightYaw_);
            }
        }
        ApplyDiscreteControls(controls);
        const float clamped = std::clamp(frameDt, 0.0f, 0.25f);
        accumulator_ += clamped;
        int steps = 0;
        while (accumulator_ >= kPhysicsStepSeconds && steps < 30) {
            if (flightMode_) {
                StepFlight(controls, kPhysicsStepSeconds, ground);
            } else {
                UpdatePedals(controls, kPhysicsStepSeconds);
                StepPhysics(kPhysicsStepSeconds, ground);
            }
            accumulator_ -= kPhysicsStepSeconds;
            ++steps;
        }
    }

    void Vehicle::StepFlight(const DriverControls& controls, const float dt, const GroundSurface& ground)
    {
        const TurboMode mode = engine_.TurboSetting();
        const float maxSpeed = mode == TurboMode::UltraUltra ? 500.0f / 3.6f :
                               mode == TurboMode::Ultra ? 111.0f : mode == TurboMode::Turbo ? 69.0f : 36.0f;
        const float acceleration = mode == TurboMode::UltraUltra ? 44.0f :
                                   mode == TurboMode::Ultra ? 42.0f : mode == TurboMode::Turbo ? 22.0f : 10.0f;
        const float climbSpeed = mode == TurboMode::UltraUltra ? 40.0f :
                                mode == TurboMode::Ultra ? 35.0f : mode == TurboMode::Turbo ? 20.0f : 10.0f;
        flightYaw_ -= std::clamp(controls.steering, -1.0f, 1.0f) * 1.4f * dt;
        body_.SetOrientation(Quaternion::CreateFromAxisAngle(Vector3::Up, flightYaw_));
        const Vector3 desired = body_.Forward() * ((std::clamp(controls.throttle, 0.0f, 1.0f) -
                                                     std::clamp(controls.brake, 0.0f, 1.0f)) * maxSpeed);
        Vector3 velocity = body_.LinearVelocity();
        Vector3 horizontal(velocity.X, 0.0f, velocity.Z);
        Vector3 change = desired - horizontal;
        const float changeLength = change.Length();
        if (changeLength > acceleration * dt) change = change * (acceleration * dt / changeLength);
        horizontal = horizontal + change;
        const float verticalTarget = (controls.flightClimb ? climbSpeed : 0.0f) -
                                     (controls.flightDescend ? climbSpeed : 0.0f);
        velocity = Vector3(horizontal.X, std::clamp(verticalTarget - velocity.Y, -acceleration * dt, acceleration * dt) + velocity.Y,
                           horizontal.Z);
        Vector3 position = body_.Position() + velocity * dt;
        const Vector3 origin = OriginPosition();
        const float floor = ground.HeightAt(origin.X, origin.Z) + 1.8f;
        const float originHeight = origin.Y + velocity.Y * dt;
        if (originHeight < floor) {
            position.Y += floor - originHeight;
            velocity.Y = std::max(0.0f, velocity.Y);
        }
        body_.SetPosition(position);
        body_.SetLinearVelocity(velocity);
        rotorAngle_ = std::fmod(rotorAngle_ + dt * (mode == TurboMode::UltraUltra ? 53.0f :
                                                  mode == TurboMode::Ultra ? 43.0f : mode == TurboMode::Turbo ? 33.0f : 25.0f),
                                2.0f * std::numbers::pi_v<float>);
        lastSpeedMs_ = Vector3::Dot(velocity, body_.Forward());
    }

    void Vehicle::ApplyDiscreteControls(const DriverControls& controls)
    {
        startRefused_ = false;
        if (controls.toggleTurbo) {
            engine_.CycleTurboMode();
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
        float clutchRate = 8.0f;
        if (clutchTarget < clutchPedal_) {
            clutchRate = 4.0f;
            const auto& cd = def_.clutch;
            if (clutchPedal_ <= cd.engageEnd + 0.02f && clutchPedal_ > cd.engageStart - 0.02f) {
                clutchRate = 0.5f;
                if (engine_.IsRunning() && !clutchLocked_) {
                    // Do not let the clutch demand more than the engine can deliver: release
                    // down to the bite point for the available torque, then only trickle.
                    const float available = std::max(0.0f, engine_.NetTorque(throttlePedal_));
                    const float floor = clutch_.PedalForCapacity(available * 0.9f / engine_.PowerMultiplier());
                    if (clutchPedal_ - clutchRate * dt <= floor) {
                        clutchRate = engine_.Rpm() > def_.engine.idleRpm + 300.0f ? 0.01f : 0.0f;
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

    void Vehicle::UpdateSuspension(const float dt, const GroundSurface& ground)
    {
        const auto& s = def_.suspension;
        const Vector3 up = body_.Up();
        const Vector3 down = -up;

        for (auto& w : wheels_) {
            const Vector3 mount = body_.ToWorldPoint(w.bodyMount);
            const float rayLength = s.restLengthM + w.def->radiusM;
            GroundHit hit;
            const bool hitGround = ground.Raycast(mount, down, rayLength + 0.05f, hit);
            const float previous = w.compression;
            if (hitGround && hit.distance <= rayLength) {
                w.grounded = true;
                w.hit = hit;
                w.compression = std::clamp(rayLength - hit.distance, 0.0f, s.travelM);
            } else {
                w.grounded = false;
                w.compression = 0.0f;
            }
            w.compressionVelocity = (w.compression - previous) / dt;
            w.worldCenter = mount + down * (s.restLengthM - w.compression);
        }

        // Anti-roll bars transfer load between the wheels of an axle.
        const auto arb = [&](int left, int right) {
            const float diff = wheels_[static_cast<std::size_t>(left)].compression -
                               wheels_[static_cast<std::size_t>(right)].compression;
            return s.antiRollStiffnessNPerM * diff;
        };
        const float frontArb = arb(frontLeft_, frontRight_);
        const float rearArb = arb(rearLeft_, rearRight_);

        for (std::size_t i = 0; i < wheels_.size(); ++i) {
            auto& w = wheels_[i];
            if (!w.grounded) {
                w.suspensionForce = 0.0f;
                continue;
            }
            const float damping = w.compressionVelocity > 0.0f ? s.damperCompressionNsPerM : s.damperReboundNsPerM;
            float force = s.springRateNPerM * w.compression + damping * w.compressionVelocity;
            const int idx = static_cast<int>(i);
            if (idx == frontLeft_) force += frontArb;
            if (idx == frontRight_) force -= frontArb;
            if (idx == rearLeft_) force += rearArb;
            if (idx == rearRight_) force -= rearArb;
            // Bump stop: stiff extra spring in the last 15 % of travel.
            const float bumpStart = s.travelM * 0.85f;
            if (w.compression > bumpStart) {
                force += (w.compression - bumpStart) * s.springRateNPerM * 12.0f;
            }
            w.suspensionForce = std::max(0.0f, force);
            body_.ApplyForce(up * w.suspensionForce, w.hit.point);
        }
    }

    float Vehicle::AverageDrivenSpin() const
    {
        if (drivenWheels_.empty()) {
            return 0.0f;
        }
        float sum = 0.0f;
        for (const int i : drivenWheels_) {
            sum += wheels_[static_cast<std::size_t>(i)].spinVelocity;
        }
        return sum / static_cast<float>(drivenWheels_.size());
    }

    float Vehicle::CouplingCapacity() const
    {
        const float multiplier = engine_.PowerMultiplier();
        if (transmission_->Mode() == TransmissionMode::Manual) {
            return clutch_.Capacity(clutchPedal_) * multiplier;
        }
        const auto* automatic = static_cast<const AutomaticTransmission*>(transmission_.get());
        return automatic->CouplingCapacity(engine_.Rpm(), def_.engine.idleRpm, def_.clutch.maxTorqueNm * multiplier);
    }

    void Vehicle::IntegrateWheel(WheelRuntime& w, const float dt, const float driveTorque, const float extraInertia)
    {
        const float inertia = def_.tyres.wheelInertiaKgM2 + extraInertia;
        const float radius = w.def->radiusM;
        w.driveTorque = driveTorque;

        float brakeTorqueMax = brakePedal_ * w.def->brakeTorqueNm;
        if (handbrake_) {
            brakeTorqueMax += w.def->handbrakeTorqueNm;
        }
        w.brakeTorque = brakeTorqueMax;
        w.absActive = false;

        if (!w.grounded || w.suspensionForce <= 0.0f) {
            // Airborne: spin freely under drive and brake torque.
            float spin = w.spinVelocity + driveTorque / inertia * dt;
            const float brakeDelta = brakeTorqueMax / inertia * dt;
            spin -= Sign(spin) * std::min(std::fabs(spin), brakeDelta);
            w.spinVelocity = spin;
            w.slipRatio = 0.0f;
            w.slipAngle = 0.0f;
            w.tyre = TyreForces{};
            w.longitudinalSpeed = 0.0f;
            w.lateralSpeed = 0.0f;
            w.spinAngle = std::fmod(w.spinAngle + w.spinVelocity * dt, 6.2831853f);
            return;
        }

        // Contact frame: wheel heading projected onto the ground plane.
        const Vector3 normal = w.hit.normal;
        const Vector3 heading = body_.ToWorldDirection(
            Vector3(std::sin(w.steerAngle), 0.0f, -std::cos(w.steerAngle)));
        Vector3 forward = heading - normal * Vector3::Dot(heading, normal);
        if (forward.LengthSquared() < 1e-6f) {
            forward = body_.Forward();
        }
        forward.Normalize();
        Vector3 right = Vector3::Cross(forward, normal);
        right.Normalize();
        w.contactForward = forward;
        w.contactRight = right;

        const Vector3 contactVelocity = body_.VelocityAtWorldPoint(w.hit.point);
        const float vLong = Vector3::Dot(contactVelocity, forward);
        const float vLat = Vector3::Dot(contactVelocity, right);
        w.longitudinalSpeed = vLong;
        w.lateralSpeed = vLat;

        const float load = w.suspensionForce;
        // The boosted driveline needs matching tyre traction to turn its extra torque into
        // acceleration instead of permanent wheelspin.
        const float baseSurfaceFactor = SurfaceFrictionFactor(w.hit.surface) * (1.0f - 0.30f * roadWetness_);
        const float ultraUltraGrip = engine_.TurboSetting() == TurboMode::UltraUltra ? 8.0f : 1.0f;
        const float longitudinalSurfaceFactor = baseSurfaceFactor * ultraUltraGrip;
        const float lowSpeed = std::max(0.1f, def_.tyres.lowSpeedMs);
        const float vDen = std::max(std::fabs(vLong), lowSpeed);
        const float slipAngle = std::atan2(vLat, std::fabs(vLong) + 0.05f);
        // Share of the body mass the contact patch accelerates longitudinally.
        const float massShare = def_.chassis.massKg * 0.25f;

        // ---- Longitudinal: implicit update of the relative contact speed u = omega*r - v.
        // Both the wheel (through r^2/I) and the body share (through 1/M) respond to the tyre
        // force, so the effective compliance is c = r^2/I + 1/M. The tyre force is linearised
        // with its secant stiffness (always positive), which makes the step unconditionally
        // stable at low speed where the slip gain is enormous.
        const float uOld = w.spinVelocity * radius - vLong;
        const float kappaOld = uOld / vDen;
        const TyreForces f0 = tyreModel_.Compute(kappaOld, slipAngle, load, longitudinalSurfaceFactor);
        const float originStiffness = tyreModel_.LongitudinalStiffness(load, longitudinalSurfaceFactor) / vDen;   // N per m/s
        float secant = std::fabs(uOld) > 1e-4f ? std::fabs(f0.longitudinal / uOld) : originStiffness;
        secant = std::clamp(secant, 0.02f * originStiffness, originStiffness);
        const float compliance = radius * radius / inertia + 1.0f / massShare;

        // Brake torque opposes rotation; when the wheel is (nearly) stopped it opposes the
        // torque that would start it turning (static friction).
        // Ultra turbo overwhelms the driven tyres in the lower gears. Its traction controller
        // trims delivered wheel torque near the available grip so the added engine power becomes
        // acceleration instead of sustained wheelspin.
        const float tractionCap = load * f0.friction * radius * 0.93f;
        const bool tractionControlled = engine_.TurboSetting() == TurboMode::Ultra ||
                                        engine_.TurboSetting() == TurboMode::UltraUltra;
        const float usableDriveTorque = tractionControlled && driveTorque > tractionCap
                                            ? tractionCap : driveTorque;
        w.driveTorque = usableDriveTorque;
        float torque = usableDriveTorque;
        float brakeApplied = 0.0f;
        if (brakeTorqueMax > 0.0f) {
            const float direction = std::fabs(w.spinVelocity) > 0.05f ? Sign(w.spinVelocity) : Sign(driveTorque + vLong);
            brakeApplied = -direction * brakeTorqueMax;
            torque += brakeApplied;
        }

        const float uNew = uOld + dt * (torque * radius / inertia - f0.longitudinal * compliance) /
                                      (1.0f + dt * secant * compliance);
        // The force the implicit step assumed: the linearised tyre force, kept inside the friction
        // circle. Using exactly this force for both the wheel and the body keeps the step consistent.
        const float frictionLimit = load * f0.friction;
        const auto linearForce = [&](float u) {
            return std::clamp(f0.longitudinal + secant * (u - uOld), -frictionLimit, frictionLimit);
        };
        float longitudinalForce = linearForce(uNew);
        float spin = w.spinVelocity + dt * (torque - longitudinalForce * radius) / inertia;

        if (brakeTorqueMax > 0.0f) {
            bool clamped = false;
            // A brake never reverses the wheel: if the sign flipped because of the brake, stop it.
            if (Sign(spin) != Sign(w.spinVelocity) && std::fabs(w.spinVelocity) > 1e-4f) {
                spin = 0.0f;
                clamped = true;
            } else if (std::fabs(w.spinVelocity) <= 1e-4f && std::fabs(spin) < brakeTorqueMax * dt / inertia) {
                spin = 0.0f;
                clamped = true;
            }
            // Simple ABS: keep braking slip near the tyre's peak above walking pace.
            if (std::fabs(vLong) > 1.5f) {
                const float peak = def_.tyres.peakSlipRatio * 1.15f;
                const float limitSpin = (vLong - Sign(vLong) * peak * vDen) / radius;
                if ((vLong > 0.0f && spin < limitSpin) || (vLong < 0.0f && spin > limitSpin)) {
                    spin = limitSpin;
                    w.absActive = true;
                    clamped = true;
                }
            }
            if (clamped) {
                longitudinalForce = linearForce(spin * radius - vLong);
            }
        }
        const float kappaNew = (spin * radius - vLong) / vDen;
        TyreForces forces = tyreModel_.Compute(kappaNew, slipAngle, load, longitudinalSurfaceFactor);
        forces.longitudinal = longitudinalForce;

        // ---- Lateral: never let the lateral force reverse the contact patch's lateral velocity
        // within one step (the same implicit idea for the body-only lateral degree of freedom).
        const float lateralLimit = massShare * std::fabs(vLat) / dt;
        if (std::fabs(forces.lateral) > lateralLimit) {
            forces.lateral = Sign(forces.lateral) * lateralLimit;
        }
        if (engine_.TurboSetting() == TurboMode::UltraUltra) {
            // Four tyre contact patches can supply up to four g of sideways acceleration.
            // The cap keeps short slip spikes from initiating an uncontrollable spin.
            const float corneringCap = massShare * 4.0f * Units::kGravity;
            forces.lateral = std::clamp(forces.lateral, -corneringCap, corneringCap);
        }

        w.spinVelocity = spin;
        w.slipRatio = kappaNew;
        w.slipAngle = slipAngle;
        w.spinAngle = std::fmod(w.spinAngle + spin * dt, 6.2831853f);
        w.tyre = forces;

        // Rolling resistance opposes rolling and vanishes at rest.
        float rolling = 0.0f;
        if (std::fabs(vLong) > 0.1f) {
            rolling = -Sign(vLong) * def_.chassis.rollingResistance * SurfaceRollingFactor(w.hit.surface) * load;
        }
        const Vector3 force = forward * (forces.longitudinal + rolling) + right * forces.lateral;
        if (engine_.TurboSetting() == TurboMode::UltraUltra) {
            // Keep the front/rear force arms that turn the car, but lift their application
            // to the roll centre. Otherwise the multiplied tyre grip overturns the chassis.
            Vector3 rollCentre = w.hit.point;
            rollCentre.Y = body_.Position().Y;
            body_.ApplyForce(force, rollCentre);
        } else {
            body_.ApplyForce(force, w.hit.point);
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
            for (auto& w : wheels_) {
                IntegrateWheel(w, dt, 0.0f, 0.0f);
            }
            return;
        }

        const float omegaIn = ratio * AverageDrivenSpin();
        const float slip = engine_.AngularVelocity() - omegaIn;

        if (clutchLocked_) {
            const float engineTorque = engine_.NetTorque(driverThrottle);
            const float wheelTorque = engineTorque * ratio * efficiency / n;
            const float reflected = engine_.Inertia() * ratio * ratio / n;
            for (auto& w : wheels_) {
                IntegrateWheel(w, dt, w.def->driven ? wheelTorque : 0.0f, w.def->driven ? reflected : 0.0f);
            }
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
        for (auto& w : wheels_) {
            IntegrateWheel(w, dt, w.def->driven ? wheelTorque : 0.0f, 0.0f);
        }
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
        context.engineRunning = engine_.IsRunning();
        context.brakePressed = brakePedal_ > 0.1f;
        context.topGearRatioFactor = TopGearFactor(engine_.TurboSetting());
        transmission_->Step(context);

        ResolveDriveline(dt);
        ApplyBodyForces(dt);
        body_.Integrate(dt);
        ApplySleep();

        const float speed = ForwardSpeedMs();
        odometer_.Add(speed, dt);
        fuel_.Step(dt, engine_);
        thermal_.Step(dt, engine_, fuel_.MassFlowGramsPerSecond(engine_), speed);
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
