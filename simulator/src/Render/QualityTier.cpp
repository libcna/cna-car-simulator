#include "CarSim/Render/QualityTier.hpp"

#include <algorithm>
#include <cctype>

namespace CarSim::Render
{
    QualitySettings SettingsFor(const QualityTier tier)
    {
        switch (tier) {
            case QualityTier::Low:
                // Half the world distance, a quarter of the vegetation, and a mirror that is
                // redrawn every third frame at a third of the distance. On a software rasteriser
                // this roughly halves the frame; on a GPU it is the setting for 1440p or higher.
                return QualitySettings{0.5f, 110.0f, 3, 0.5f};
            case QualityTier::Medium:
                return QualitySettings{0.75f, 200.0f, 2, 0.75f};
            case QualityTier::High:
            default:
                return QualitySettings{1.0f, 300.0f, 1, 1.0f};
        }
    }

    const char* ToString(const QualityTier tier)
    {
        switch (tier) {
            case QualityTier::Low: return "low";
            case QualityTier::Medium: return "medium";
            case QualityTier::High: return "high";
        }
        return "high";
    }

    bool QualityFromName(const std::string& name, QualityTier& out)
    {
        std::string key;
        key.reserve(name.size());
        for (const char c : name) {
            key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        if (key == "low") { out = QualityTier::Low; return true; }
        if (key == "medium" || key == "mid") { out = QualityTier::Medium; return true; }
        if (key == "high" || key == "full") { out = QualityTier::High; return true; }
        return false;
    }
}
