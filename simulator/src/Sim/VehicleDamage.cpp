#include "CarSim/Sim/VehicleDamage.hpp"

#include <algorithm>
#include <cmath>

namespace CarSim::Sim
{
    using Microsoft::Xna::Framework::Vector3;

    bool VehicleDamage::AddImpact(const Vector3& point, const Vector3& inward, const float closingSpeed, const float frontZ, const float rearZ)
    {
        if (closingSpeed < kMinSpeedMs) return false;
        Vector3 direction = inward;
        if (direction.LengthSquared() < 1e-6f) return false;
        direction.Normalize();
        const float excess = closingSpeed - kMinSpeedMs;
        const float depth = std::min(kMaxDepthM, 0.012f * excess);
        const float radius = std::min(0.9f, 0.28f + 0.045f * closingSpeed);
        // A second knock in the same place deepens the dent rather than adding another.
        bool merged = false;
        for (auto& d : dents_) {
            if (Vector3::Distance(d.centre, point) < 0.35f) {
                d.depth = std::min(kMaxDepthM, d.depth + depth * 0.7f);
                d.radius = std::max(d.radius, radius);
                merged = true;
                break;
            }
        }
        if (!merged) {
            if (static_cast<int>(dents_.size()) >= kMaxDents) {
                // Keep the deepest: the shallowest dent gives way to the new one.
                auto shallowest = std::min_element(dents_.begin(), dents_.end(), [](const Dent& a, const Dent& b) { return a.depth < b.depth; });
                if (shallowest->depth > depth) return false;
                dents_.erase(shallowest);
            }
            dents_.push_back({point, direction, radius, depth});
        }
        if (closingSpeed >= kLampBreakSpeedMs) {
            if (point.Z < frontZ + 0.6f) headlampsBroken_ = true;
            if (point.Z > rearZ - 0.6f) tailLampsBroken_ = true;
        }
        ++version_;
        return true;
    }

    void VehicleDamage::Repair()
    {
        if (dents_.empty() && !headlampsBroken_ && !tailLampsBroken_) return;
        dents_.clear();
        headlampsBroken_ = tailLampsBroken_ = false;
        ++version_;
    }

    float VehicleDamage::Severity() const
    {
        float sum = 0.0f;
        for (const auto& d : dents_) sum += d.depth;
        return sum;
    }

    Vector3 VehicleDamage::Displacement(const Vector3& point) const
    {
        Vector3 total(0.0f, 0.0f, 0.0f);
        for (const auto& d : dents_) {
            const float r = Vector3::Distance(point, d.centre) / d.radius;
            if (r >= 1.0f) continue;
            // Smooth bowl: full depth at the centre, easing to nothing at the rim.
            const float falloff = 0.5f + 0.5f * std::cos(r * 3.14159265f);
            total += d.direction * (d.depth * falloff);
        }
        return total;
    }
}
