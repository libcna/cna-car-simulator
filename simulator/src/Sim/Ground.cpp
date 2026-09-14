#include "CarSim/Sim/Ground.hpp"

#include <cmath>

namespace CarSim::Sim
{
    using Microsoft::Xna::Framework::Vector3;

    float SurfaceFrictionFactor(const SurfaceType surface)
    {
        switch (surface) {
            case SurfaceType::Asphalt: return 1.0f;
            case SurfaceType::Concrete: return 0.98f;
            case SurfaceType::Cobbles: return 0.85f;
            case SurfaceType::Gravel: return 0.65f;
            case SurfaceType::Grass: return 0.55f;
            case SurfaceType::Dirt: return 0.6f;
        }
        return 1.0f;
    }

    float SurfaceRollingFactor(const SurfaceType surface)
    {
        switch (surface) {
            case SurfaceType::Asphalt: return 1.0f;
            case SurfaceType::Concrete: return 1.0f;
            case SurfaceType::Cobbles: return 1.4f;
            case SurfaceType::Gravel: return 2.0f;
            case SurfaceType::Grass: return 3.0f;
            case SurfaceType::Dirt: return 2.5f;
        }
        return 1.0f;
    }

    bool FlatGround::Raycast(const Vector3& origin, const Vector3& direction, const float maxDistance, GroundHit& hit) const
    {
        if (std::fabs(direction.Y) < 1e-6f) {
            return false;
        }
        const float t = (height_ - origin.Y) / direction.Y;
        if (t < 0.0f || t > maxDistance) {
            return false;
        }
        hit.distance = t;
        hit.point = origin + direction * t;
        hit.normal = Vector3(0.0f, 1.0f, 0.0f);
        hit.surface = surface_;
        return true;
    }

    bool FunctionGround::Raycast(const Vector3& origin, const Vector3& direction, const float maxDistance, GroundHit& hit) const
    {
        // March along the ray and bisect the crossing; adequate for gentle terrain.
        const int steps = 32;
        const float stepLength = maxDistance / static_cast<float>(steps);
        float previousT = 0.0f;
        Vector3 previousP = origin;
        float previousD = origin.Y - height_(origin.X, origin.Z);
        if (previousD < 0.0f) {
            // Started below the surface: report a hit at the origin so the vehicle is pushed out.
            hit.distance = 0.0f;
            hit.point = origin;
        } else {
            bool found = false;
            for (int i = 1; i <= steps; ++i) {
                const float t = stepLength * static_cast<float>(i);
                const Vector3 p = origin + direction * t;
                const float d = p.Y - height_(p.X, p.Z);
                if (d <= 0.0f) {
                    float lo = previousT;
                    float hi = t;
                    for (int k = 0; k < 12; ++k) {
                        const float mid = 0.5f * (lo + hi);
                        const Vector3 pm = origin + direction * mid;
                        if (pm.Y - height_(pm.X, pm.Z) <= 0.0f) {
                            hi = mid;
                        } else {
                            lo = mid;
                        }
                    }
                    hit.distance = hi;
                    hit.point = origin + direction * hi;
                    found = true;
                    break;
                }
                previousT = t;
                previousP = p;
                previousD = d;
            }
            (void)previousP;
            (void)previousD;
            if (!found) {
                return false;
            }
        }
        const float eps = 0.25f;
        const float hL = height_(hit.point.X - eps, hit.point.Z);
        const float hR = height_(hit.point.X + eps, hit.point.Z);
        const float hD = height_(hit.point.X, hit.point.Z - eps);
        const float hU = height_(hit.point.X, hit.point.Z + eps);
        Vector3 normal(hL - hR, 2.0f * eps, hD - hU);
        normal.Normalize();
        hit.normal = normal;
        hit.surface = surface_;
        return true;
    }
}
