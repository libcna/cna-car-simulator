// Suspension, tyre contact and wheel torque integration for the existing Vehicle.
// These methods retain the regular and special-mode physics algorithms unchanged.
#include "CarSim/Sim/Vehicle.hpp"

#include "CarSim/Sim/Units.hpp"

#include <algorithm>
#include <cmath>

namespace CarSim::Sim
{
    using Microsoft::Xna::Framework::Vector3;

    namespace
    {
        float Sign(const float v)
        {
            return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f);
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

    void Vehicle::DriveWheels(const float dt, const float torquePerWheel, const float reflectedInertia)
    {
        // Limited slip: the clutch packs carry torque from the faster driven wheel to the slower
        // one, up to the preload plus a share of the torque through the differential. Viscous
        // inside that limit so the two speeds converge without chatter.
        float transferToLeft = 0.0f;
        int left = -1, right = -1;
        if (limitedSlip_ && drivenWheels_.size() == 2) {
            left = drivenWheels_[0];
            right = drivenWheels_[1];
            if (wheels_[static_cast<std::size_t>(left)].def->position.X > 0.0f) std::swap(left, right);
            const auto& dd = def_.differential;
            const float through = 2.0f * torquePerWheel;
            const float lock = dd.preloadNm + (through >= 0.0f ? dd.powerLock : dd.coastLock) * std::fabs(through);
            const float difference = wheels_[static_cast<std::size_t>(left)].spinVelocity -
                                     wheels_[static_cast<std::size_t>(right)].spinVelocity;
            const float stiffness = 0.5f * (def_.tyres.wheelInertiaKgM2 + reflectedInertia) / dt;
            transferToLeft = std::clamp(-stiffness * difference, -lock, lock);
        }
        for (std::size_t i = 0; i < wheels_.size(); ++i) {
            auto& w = wheels_[i];
            float torque = w.def->driven ? torquePerWheel : 0.0f;
            if (static_cast<int>(i) == left) torque += 0.5f * transferToLeft;
            if (static_cast<int>(i) == right) torque -= 0.5f * transferToLeft;
            IntegrateWheel(w, dt, torque, w.def->driven ? reflectedInertia : 0.0f);
        }
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
        const float baseSurfaceFactor = SurfaceFrictionFactor(w.hit.surface) * (1.0f - 0.30f * roadWetness_) * (1.0f - 0.5f * roadSnow_);
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

}
