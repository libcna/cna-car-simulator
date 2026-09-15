// Graphics quality tiers.
//
// Three of them, not twenty sliders. Each one is a set of draw distances and update rates that
// were chosen from measurements, and the default ("high") is what every picture and every
// performance number in the documentation was taken at. A tier changes how far the world is
// drawn and how often the mirror is redrawn; it never changes the look of anything close to the
// car, because close-range quality is the point of the whole project.
#pragma once

#include <string>

namespace CarSim::Render
{
    enum class QualityTier
    {
        Low,       // for a weak integrated GPU or a high resolution: half the draw distance
        Medium,    // a mid-range machine at 1080p
        High       // the default; what the screenshots and the benchmark tables were taken at
    };

    struct QualitySettings
    {
        /// Multiplies every world draw distance (terrain, objects, trees) in the main view.
        float drawDistanceScale = 1.0f;
        /// How far the world is drawn into the rear-view mirror, in metres.
        float mirrorDistanceM = 300.0f;
        /// Redraw the mirror every n frames (1 = every frame).
        int mirrorUpdateEvery = 1;
        /// Multiplies the tree draw distance on top of `drawDistanceScale`: vegetation is the
        /// cheapest thing to pull in and the least missed at a distance.
        float vegetationScale = 1.0f;
    };

    [[nodiscard]] QualitySettings SettingsFor(QualityTier tier);
    [[nodiscard]] const char* ToString(QualityTier tier);
    /// Parses "low" / "medium" / "high". Returns false and leaves `out` alone on anything else.
    [[nodiscard]] bool QualityFromName(const std::string& name, QualityTier& out);
}
