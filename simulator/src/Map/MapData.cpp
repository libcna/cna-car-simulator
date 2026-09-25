#include "CarSim/Map/MapData.hpp"

#include <algorithm>
#include <cctype>

namespace CarSim::Map
{
    namespace
    {
        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }
    }

    CentreLineMarking RoadSpec::CentreLineAt(const float roadS) const
    {
        for (const auto& section : centreLineSections) {
            if (roadS >= section.fromM && roadS < section.toM) return section.marking;
        }
        return centreLine;
    }

    bool RoadSpec::MayOvertakeBetween(float fromRoadS, float toRoadS, const bool forward) const
    {
        if (noOvertaking) return false;
        if (fromRoadS > toRoadS) std::swap(fromRoadS, toRoadS);
        float cursor = fromRoadS;
        for (const auto& section : centreLineSections) {
            if (section.toM <= cursor) continue;
            if (section.fromM >= toRoadS) break;
            if (section.fromM > cursor && !MayCrossCentreLine(centreLine, forward)) return false;
            if (section.noOvertaking || (forward ? section.noOvertakingForward : section.noOvertakingReverse) ||
                !MayCrossCentreLine(section.marking, forward)) return false;
            cursor = std::min(toRoadS, section.toM);
        }
        return cursor >= toRoadS || MayCrossCentreLine(centreLine, forward);
    }

    const RoadNodeSpec* MapData::FindNode(const std::string& id) const
    {
        for (const auto& n : nodes) {
            if (n.id == id) {
                return &n;
            }
        }
        return nullptr;
    }

    const RoadSpec* MapData::FindRoad(const std::string& id) const
    {
        for (const auto& r : roads) {
            if (r.id == id) {
                return &r;
            }
        }
        return nullptr;
    }

    const char* ToString(const RoadClass c)
    {
        switch (c) {
            case RoadClass::ClassI: return "I";
            case RoadClass::ClassII: return "II";
            case RoadClass::ClassIII: return "III";
            case RoadClass::Local: return "local";
            case RoadClass::Residential: return "residential";
            case RoadClass::Forest: return "forest";
            case RoadClass::Track: return "track";
        }
        return "?";
    }

    bool ParseRoadClass(const std::string& text, RoadClass& out)
    {
        const std::string t = Lower(text);
        if (t == "i" || t == "1") out = RoadClass::ClassI;
        else if (t == "ii" || t == "2") out = RoadClass::ClassII;
        else if (t == "iii" || t == "3") out = RoadClass::ClassIII;
        else if (t == "local") out = RoadClass::Local;
        else if (t == "residential") out = RoadClass::Residential;
        else if (t == "forest") out = RoadClass::Forest;
        else if (t == "track") out = RoadClass::Track;
        else return false;
        return true;
    }

    bool ParseSurface(const std::string& text, Sim::SurfaceType& out)
    {
        const std::string t = Lower(text);
        if (t == "asphalt") out = Sim::SurfaceType::Asphalt;
        else if (t == "concrete") out = Sim::SurfaceType::Concrete;
        else if (t == "cobbles") out = Sim::SurfaceType::Cobbles;
        else if (t == "gravel") out = Sim::SurfaceType::Gravel;
        else if (t == "grass") out = Sim::SurfaceType::Grass;
        else if (t == "dirt") out = Sim::SurfaceType::Dirt;
        else return false;
        return true;
    }

    const char* ToString(const Sim::SurfaceType s)
    {
        switch (s) {
            case Sim::SurfaceType::Asphalt: return "asphalt";
            case Sim::SurfaceType::Concrete: return "concrete";
            case Sim::SurfaceType::Cobbles: return "cobbles";
            case Sim::SurfaceType::Gravel: return "gravel";
            case Sim::SurfaceType::Grass: return "grass";
            case Sim::SurfaceType::Dirt: return "dirt";
        }
        return "?";
    }

    bool ParseRegionType(const std::string& text, RegionType& out)
    {
        const std::string t = Lower(text);
        if (t == "meadow") out = RegionType::Meadow;
        else if (t == "field") out = RegionType::Field;
        else if (t == "forest") out = RegionType::Forest;
        else if (t == "town") out = RegionType::Town;
        else if (t == "square") out = RegionType::Square;
        else if (t == "yard") out = RegionType::Yard;
        else if (t == "orchard") out = RegionType::Orchard;
        else return false;
        return true;
    }

    bool ParseApproachControl(const std::string& text, ApproachControl& out)
    {
        const std::string t = Lower(text);
        if (t == "priority" || t == "main") out = ApproachControl::Priority;
        else if (t == "right_hand" || t == "righthand" || t == "uncontrolled") out = ApproachControl::RightHandRule;
        else if (t == "yield" || t == "give_way") out = ApproachControl::Yield;
        else if (t == "stop") out = ApproachControl::Stop;
        else if (t == "signal" || t == "signals" || t == "lights") out = ApproachControl::Signal;
        else return false;
        return true;
    }

    const char* ToString(const ApproachControl c)
    {
        switch (c) {
            case ApproachControl::Priority: return "priority";
            case ApproachControl::RightHandRule: return "right_hand";
            case ApproachControl::Yield: return "yield";
            case ApproachControl::Stop: return "stop";
            case ApproachControl::Signal: return "signal";
        }
        return "?";
    }
}
