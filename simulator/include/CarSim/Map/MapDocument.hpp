// Loader for the JSON map source files (schema v1) with version checks and validation.
#pragma once

#include "CarSim/Map/MapData.hpp"

#include <string>
#include <vector>

namespace CarSim::Map
{
    struct MapLoadResult
    {
        MapData data;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;

        [[nodiscard]] bool ok() const { return errors.empty(); }
    };

    /// Text of the individual documents; empty strings mean "file absent" for the optional ones.
    struct MapSourceTexts
    {
        std::string map;
        std::string terrain;
        std::string roads;
        std::string objects;
        std::string traffic;
    };

    /// Loads `<directory>/map.json` and the files it references.
    [[nodiscard]] MapLoadResult LoadMapDirectory(const std::string& directory);

    /// Parses documents already in memory (unit tests, tools).
    [[nodiscard]] MapLoadResult ParseMapSources(const MapSourceTexts& sources);

    /// Structural validation shared by the loader and the validate tool: referential integrity,
    /// geometric sanity. Appends to `errors`/`warnings`.
    void ValidateMapData(const MapData& data, std::vector<std::string>& errors, std::vector<std::string>& warnings);

    /// Directory of a named map below a content root: `<contentRoot>/maps/<name>`.
    [[nodiscard]] std::string MapDirectory(const std::string& contentRoot, const std::string& mapName);
}
