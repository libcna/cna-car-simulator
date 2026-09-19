// carsim-simtrace: run a named scenario headlessly and print a trace of the vehicle state.
#include "CarSim/Map/MapDocument.hpp"
#include "CarSim/Map/MapWorld.hpp"
#include "CarSim/Sim/Ground.hpp"
#include "CarSim/Sim/Units.hpp"
#include "CarSim/Sim/Vehicle.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"
#include "CarSim/Traffic/RouteDriver.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <numbers>
#include <string>
#include <vector>

using namespace CarSim::Sim;
using Microsoft::Xna::Framework::Vector3;

namespace
{
    constexpr float kFrame = 1.0f / 60.0f;

    void PrintHeader()
    {
        std::printf("%6s %7s %7s %5s %-8s %5s %5s %5s %6s %5s %7s %7s %7s %7s %8s %8s %8s %8s %7s %7s\n",
                    "t", "kmh", "rpm", "gear", "state", "thr", "brk", "clu", "lock", "slip",
                    "wFL", "wRL", "srFL", "srRL", "loadFL", "loadRL", "FxFL", "FxRL", "vFL", "Tdrv");
    }

    void PrintRow(float t, const Vehicle& v)
    {
        const auto s = v.Snapshot();
        const auto& wh = v.Wheels();
        std::printf("%6.2f %7.2f %7.0f %5s %-8s %5.2f %5.2f %5.2f %6d %5.2f %7.2f %7.2f %7.3f %7.3f %8.0f %8.0f %8.0f %8.0f %7.3f %7.1f\n",
                    t, s.speedKmh, s.engineRpm, s.gearLabel.c_str(), ToString(s.engineState),
                    s.throttlePedal, s.brakePedal, s.clutchPedal, s.clutchLocked ? 1 : 0,
                    wh[0].tyre.combinedSlip, wh[0].spinVelocity, wh[2].spinVelocity,
                    wh[0].slipRatio, wh[2].slipRatio, wh[0].suspensionForce, wh[2].suspensionForce,
                    wh[0].tyre.longitudinal, wh[2].tyre.longitudinal, wh[0].longitudinalSpeed, wh[0].driveTorque);
    }

    void Run(Vehicle& v, const GroundSurface& ground, float seconds, float printEvery,
             const std::function<DriverControls(float)>& controlsAt)
    {
        const int frames = static_cast<int>(std::round(seconds / kFrame));
        float sincePrint = printEvery;
        for (int i = 0; i < frames; ++i) {
            const float t = static_cast<float>(i) * kFrame;
            v.Update(controlsAt(t), kFrame, ground);
            sincePrint += kFrame;
            if (sincePrint >= printEvery - 1e-4f) {
                PrintRow(t + kFrame, v);
                sincePrint = 0.0f;
            }
        }
    }

    // ---------------------------------------------------------------------------------------
    // "metrics": the handful of numbers a driver feels, measured the way a keyboard drives --
    // pedals and steering are all-or-nothing, the automatic gearbox chooses its own gears.

    constexpr float kGravity = 9.81f;

    // Engine running, selector in Drive, parked on the brake.
    void ReadyInDrive(Vehicle& v, const GroundSurface& ground)
    {
        DriverControls c;
        c.brake = 1.0f;
        v.Update(c, kFrame, ground);
        c.toggleEngine = true;
        v.Update(c, kFrame, ground);
        c.toggleEngine = false;
        for (int i = 0; i < 150; ++i) v.Update(c, kFrame, ground);
        c.selector = AutomaticSelector::Drive;
        v.Update(c, kFrame, ground);
        c.selector.reset();
        for (int i = 0; i < 60; ++i) v.Update(c, kFrame, ground);
    }

    float Speed(const Vehicle& v) { return v.ForwardSpeedMs(); }

    DriverControls HoldSpeed(const Vehicle& v, float targetMs)
    {
        DriverControls c;
        c.throttle = std::clamp((targetMs - Speed(v)) * 0.8f, 0.0f, 1.0f);
        return c;
    }

    // Full throttle until the target speed, then hold it long enough for the gearbox to settle.
    void DriveUpTo(Vehicle& v, const GroundSurface& ground, float targetKmh)
    {
        const float target = Units::KmhToMs(targetKmh);
        DriverControls full;
        full.throttle = 1.0f;
        for (int i = 0; i < 60 * 40 && Speed(v) < target; ++i) v.Update(full, kFrame, ground);
        for (int i = 0; i < 60 * 3; ++i) v.Update(HoldSpeed(v, target), kFrame, ground);
    }

    void MeasureAcceleration(const VehicleDefinition& def, const GroundSurface& ground)
    {
        Vehicle v(def, TransmissionMode::Automatic);
        ReadyInDrive(v, ground);
        DriverControls full;
        full.throttle = 1.0f;
        float t = 0.0f, t50 = -1.0f, t100 = -1.0f, shiftStart = -1.0f, shiftPeak = 0.0f;
        std::string gear = v.Snapshot().gearLabel;
        std::printf("acceleration (automatic, full throttle)\n");
        while (t < 30.0f && t100 < 0.0f) {
            const float rpmBefore = v.Snapshot().engineRpm;
            v.Update(full, kFrame, ground);
            t += kFrame;
            const auto s = v.Snapshot();
            if (s.gearLabel != gear) {
                std::printf("  %5.2f s  %s -> %s at %4.0f rpm, %5.1f km/h", t, gear.c_str(), s.gearLabel.c_str(),
                            rpmBefore, s.speedKmh);
                gear = s.gearLabel;
                shiftStart = t;
                shiftPeak = s.engineRpm;
            }
            if (shiftStart >= 0.0f) {
                shiftPeak = std::max(shiftPeak, s.engineRpm);
                if (t - shiftStart >= 0.6f) {
                    std::printf(", peak %4.0f rpm while shifting\n", shiftPeak);
                    shiftStart = -1.0f;
                }
            }
            if (t50 < 0.0f && s.speedKmh >= 50.0f) t50 = t;
            if (t100 < 0.0f && s.speedKmh >= 100.0f) t100 = t;
        }
        if (shiftStart >= 0.0f) std::printf("\n");
        std::printf("  0-50 km/h %.2f s, 0-100 km/h %.2f s\n", t50, t100);
    }

    void MeasureBraking(const VehicleDefinition& def, const GroundSurface& ground, float fromKmh)
    {
        Vehicle v(def, TransmissionMode::Automatic);
        ReadyInDrive(v, ground);
        DriveUpTo(v, ground, fromKmh);
        const float v0 = Speed(v);
        const Vector3 start = v.OriginPosition();
        DriverControls brake;
        brake.brake = 1.0f;
        float t = 0.0f, peak = 0.0f, tTo7 = -1.0f, previous = v0;
        while (Speed(v) > 0.05f && t < 15.0f) {
            v.Update(brake, kFrame, ground);
            t += kFrame;
            const float decel = (previous - Speed(v)) / kFrame;
            previous = Speed(v);
            peak = std::max(peak, decel);
            if (tTo7 < 0.0f && decel >= 7.0f) tTo7 = t;
        }
        const float distance = (v.OriginPosition() - start).Length();
        std::printf("braking from %.0f km/h: %.1f m in %.2f s, mean %.2f g, peak %.2f g, 7 m/s^2 after %.2f s\n",
                    Units::MsToKmh(v0), distance, t, v0 * v0 / (2.0f * distance) / kGravity, peak / kGravity, tTo7);
    }

    void MeasureStepSteer(const VehicleDefinition& def, const GroundSurface& ground, float atKmh)
    {
        Vehicle v(def, TransmissionMode::Automatic);
        ReadyInDrive(v, ground);
        DriveUpTo(v, ground, atKmh);
        const float target = Units::KmhToMs(atKmh);
        constexpr float kHold = 3.0f;
        std::vector<float> yaw;
        float worstSlip = 0.0f;
        float tHalfG = -1.0f;
        for (int i = 0; i < static_cast<int>(kHold / kFrame); ++i) {
            DriverControls c = HoldSpeed(v, target);
            c.steering = 1.0f;
            v.Update(c, kFrame, ground);
            yaw.push_back(std::fabs(v.Body().AngularVelocity().Y));
            const Vector3 velocity = v.Body().LinearVelocity();
            const float along = Vector3::Dot(velocity, v.Body().Forward());
            const float across = Vector3::Dot(velocity, v.Body().Right());
            worstSlip = std::max(worstSlip, std::fabs(std::atan2(across, std::fabs(along))));
            if (tHalfG < 0.0f && std::fabs(along) * yaw.back() >= 0.5f * kGravity) {
                tHalfG = static_cast<float>(yaw.size()) * kFrame;
            }
        }
        const float steady = yaw.back();
        float t90 = -1.0f;
        for (std::size_t i = 0; i < yaw.size(); ++i) {
            if (yaw[i] >= 0.9f * steady) {
                t90 = static_cast<float>(i + 1) * kFrame;
                break;
            }
        }
        const float speed = std::fabs(Speed(v));
        const float wheelDeg = v.SteeringWheelAngle() / def.steering.steeringRatio * 57.2958f;
        std::printf("keyboard step steer at %.0f km/h: 0.5 g after %.2f s, 90%% yaw after %.2f s, %.1f deg/s, "
                    "%.2f g lateral, radius %.1f m, road wheels %.1f deg, body slip max %.1f deg, speed now %.0f km/h\n",
                    atKmh, tHalfG, t90, steady * 57.2958f, speed * steady / kGravity,
                    steady > 1e-3f ? speed / steady : 0.0f, wheelDeg, worstSlip * 57.2958f,
                    Units::MsToKmh(speed));
    }

    void MeasureTurningCircle(const VehicleDefinition& def, const GroundSurface& ground)
    {
        Vehicle v(def, TransmissionMode::Automatic);
        ReadyInDrive(v, ground);
        const float target = Units::KmhToMs(8.0f);
        for (int i = 0; i < 60 * 6; ++i) {
            DriverControls c = HoldSpeed(v, target);
            c.steering = 1.0f;
            v.Update(c, kFrame, ground);
        }
        const float yawRate = std::fabs(v.Body().AngularVelocity().Y);
        std::printf("turning circle at full lock: %.1f m kerb-to-kerb diameter (approx.)\n",
                    yawRate > 1e-3f ? 2.0f * std::fabs(Speed(v)) / yawRate + def.FrontTrackM() : 0.0f);
    }

    void RunMetrics(const VehicleDefinition& def)
    {
        FlatGround ground(0.0f);
        MeasureAcceleration(def, ground);
        MeasureBraking(def, ground, 100.0f);
        MeasureBraking(def, ground, 50.0f);
        MeasureStepSteer(def, ground, 50.0f);
        MeasureStepSteer(def, ground, 90.0f);
        MeasureStepSteer(def, ground, 120.0f);
        MeasureTurningCircle(def, ground);
    }
}

int main(int argc, char* argv[])
{
    const std::string scenario = argc > 1 ? argv[1] : "auto-launch";
    VehicleDefinition def = MakeReferenceVehicle();
    std::string contentRoot = "content";
    std::string routeName = "town";
    for (int i = 2; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--content") contentRoot = argv[i + 1];
        if (std::string(argv[i]) == "--route") routeName = argv[i + 1];
        if (std::string(argv[i]) == "--vehicle") {
            const auto loaded = LoadVehicleDefinitionFile(argv[i + 1]);
            if (!loaded.ok()) {
                for (const auto& e : loaded.errors) std::fprintf(stderr, "%s\n", e.c_str());
                return 2;
            }
            def = loaded.definition;
        }
    }
    FlatGround ground(0.0f);
    if (scenario == "metrics") {
        RunMetrics(def);
        return 0;
    }
    if (scenario == "route") {
        // A map route driven by the same RouteDriver the route tests and --route use, printed
        // every half second with the lateral error from the planned path.
        std::vector<std::string> errors;
        auto world = CarSim::Map::MapWorld::Load(CarSim::Map::MapDirectory(contentRoot, "lipova"), errors);
        if (!world) {
            for (const auto& e : errors) std::fprintf(stderr, "%s\n", e.c_str());
            return 2;
        }
        const CarSim::Map::RouteSpec* spec = nullptr;
        for (const auto& r : world->Data().traffic.routes) {
            if (r.name == routeName) spec = &r;
        }
        if (spec == nullptr) {
            std::fprintf(stderr, "unknown route '%s'\n", routeName.c_str());
            return 2;
        }
        Vehicle v(def, TransmissionMode::Automatic);
        const auto spawn = world->PlayerSpawn(spec->spawn);
        v.PlaceAt(world->SpawnPosition(spawn), -spawn.headingDeg * (std::numbers::pi_v<float> / 180.0f));
        CarSim::Traffic::RouteDriver driver(world->Lanes());
        const auto s0 = v.Snapshot();
        const Vector3 forward = s0.worldMatrix.getForwardProperty();
        driver.Plan(s0.originPosition, std::atan2(forward.X, -forward.Z), spec->waypoints);
        std::printf("%6s %6s %5s %5s %5s %5s %6s %7s %8s %8s\n", "t", "kmh", "gear", "rpm", "thr", "brk", "steer",
                    "latErr", "x", "z");
        float sincePrint = 0.5f;
        for (float t = 0.0f; t < 240.0f && !driver.Progress().finished; t += kFrame) {
            const auto state = v.Snapshot();
            const DriverControls c = driver.Update(state, kFrame);
            v.Update(c, kFrame, world->Ground());
            sincePrint += kFrame;
            if (sincePrint >= 0.5f || std::fabs(driver.Progress().lateralErrorM) > 2.0f) {
                std::printf("%6.2f %6.1f %5s %5.0f %5.2f %5.2f %6.2f %7.2f %8.1f %8.1f\n", t, state.speedKmh,
                            state.gearLabel.c_str(), state.engineRpm, c.throttle, c.brake, c.steering,
                            driver.Progress().lateralErrorM, state.originPosition.X, state.originPosition.Z);
                sincePrint = 0.0f;
            }
        }
        std::printf("finished %d, worst lateral %.2f m\n", driver.Progress().finished ? 1 : 0, driver.Progress().offRouteM);
        return 0;
    }
    if (scenario == "stop-reverse") {
        // The quick-start sequence: drive off in D, brake to a stop, select R and reverse.
        Vehicle v(def, TransmissionMode::Automatic);
        ReadyInDrive(v, ground);
        PrintHeader();
        Run(v, ground, 4.0f, 0.5f, [](float) { DriverControls c; c.throttle = 1.0f; return c; });
        Run(v, ground, 8.0f, 0.5f, [](float) { DriverControls c; c.brake = 1.0f; return c; });
        DriverControls r;
        r.selector = AutomaticSelector::Reverse;
        v.Update(r, kFrame, ground);
        Run(v, ground, 1.0f, 0.25f, [](float) { return DriverControls{}; });
        Run(v, ground, 3.0f, 0.25f, [](float) { DriverControls c; c.throttle = 1.0f; return c; });
        return 0;
    }
    PrintHeader();

    if (scenario == "auto-launch") {
        Vehicle v(def, TransmissionMode::Automatic);
        DriverControls c;
        c.brake = 1.0f;
        v.Update(c, kFrame, ground);
        c.toggleEngine = true;
        v.Update(c, kFrame, ground);
        c.toggleEngine = false;
        Run(v, ground, 3.0f, 0.5f, [c](float) { return c; });
        c.selector = AutomaticSelector::Drive;
        v.Update(c, kFrame, ground);
        c.selector.reset();
        Run(v, ground, 1.0f, 0.5f, [c](float) { return c; });
        Run(v, ground, 20.0f, 0.25f, [](float) { DriverControls k; k.throttle = 1.0f; return k; });
    } else if (scenario == "manual-launch") {
        Vehicle v(def, TransmissionMode::Manual);
        DriverControls c;
        c.clutch = 1.0f;
        c.brake = 1.0f;
        v.Update(c, kFrame, ground);
        c.toggleEngine = true;
        v.Update(c, kFrame, ground);
        c.toggleEngine = false;
        Run(v, ground, 3.0f, 0.5f, [c](float) { return c; });
        DriverControls g;
        g.clutch = 1.0f;
        g.selectGear = 1;
        v.Update(g, kFrame, ground);
        Run(v, ground, 0.5f, 0.5f, [](float) { DriverControls k; k.clutch = 1.0f; return k; });
        Run(v, ground, 8.0f, 1.0f / 60.0f, [](float) { DriverControls k; k.throttle = 0.45f; k.clutch = 0.0f; return k; });
    } else if (scenario == "brake") {
        Vehicle v(def, TransmissionMode::Automatic);
        Run(v, ground, 2.0f, 0.5f, [](float) { DriverControls c; c.brake = 1.0f; return c; });
        v.ForceForwardSpeed(Units::KmhToMs(100.0f));
        const Vector3 start = v.OriginPosition();
        Run(v, ground, 6.0f, 0.25f, [](float) { DriverControls c; c.brake = 1.0f; return c; });
        std::printf("distance %.1f m\n", (v.OriginPosition() - start).Length());
    } else if (scenario == "steer") {
        Vehicle v(def, TransmissionMode::Automatic);
        Run(v, ground, 2.0f, 0.5f, [](float) { DriverControls c; c.brake = 1.0f; return c; });
        v.ForceForwardSpeed(Units::KmhToMs(40.0f));
        Run(v, ground, 4.0f, 0.25f, [](float) { DriverControls c; c.steering = 0.4f; return c; });
        const auto p = v.OriginPosition();
        std::printf("position x=%.2f z=%.2f up.y=%.4f\n", p.X, p.Z, v.Body().Up().Y);
    } else if (scenario == "slope") {
        const FunctionGround slope([](float, float z) { return -0.12f * z; });
        Vehicle v(def, TransmissionMode::Manual);
        v.PlaceAt(Vector3(0.0f, 0.0f, 0.0f), 0.0f);
        Run(v, slope, 1.0f, 0.5f, [](float) { DriverControls c; c.brake = 1.0f; return c; });
        std::printf("start z=%.3f\n", v.OriginPosition().Z);
        Run(v, slope, 2.0f, 0.5f, [](float) { DriverControls c; c.handbrake = true; return c; });
        std::printf("after handbrake z=%.3f\n", v.OriginPosition().Z);
        for (int i = 0; i < 8; ++i) {
            Run(v, slope, 0.5f, 0.5f, [](float) { return DriverControls{}; });
            const auto p = v.OriginPosition();
            std::printf("  free roll t=%.1f z=%.3f y=%.3f vz=%.3f pitch.up.z=%.3f\n", 0.5f * (i + 1), p.Z, p.Y,
                        v.Body().LinearVelocity().Z, v.Body().Up().Z);
        }
    } else {
        std::fprintf(stderr, "unknown scenario '%s' (auto-launch, manual-launch, brake, steer, slope, metrics)\n",
                     scenario.c_str());
        return 2;
    }
    return 0;
}
