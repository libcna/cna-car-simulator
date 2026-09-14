// carsim-simtrace: run a named scenario headlessly and print a trace of the vehicle state.
#include "CarSim/Sim/Ground.hpp"
#include "CarSim/Sim/Units.hpp"
#include "CarSim/Sim/Vehicle.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>

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
}

int main(int argc, char* argv[])
{
    const std::string scenario = argc > 1 ? argv[1] : "auto-launch";
    const VehicleDefinition def = MakeReferenceVehicle();
    FlatGround ground(0.0f);
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
        std::fprintf(stderr, "unknown scenario '%s' (auto-launch, manual-launch, brake, steer, slope)\n", scenario.c_str());
        return 2;
    }
    return 0;
}
