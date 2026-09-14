// Persistent player data and settings: a small versioned JSON file (odometer, trip, transmission
// mode, selected vehicle, audio levels, mirror, key bindings). Project-owned reader/writer over
// Sharp Runtime's JSON parser; unknown newer versions are loaded read-only so they are never
// overwritten by an older build.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace CarSim::Core
{
    inline constexpr int kSaveSchemaVersion = 1;

    struct SaveSettings
    {
        float masterVolume = 0.8f;
        float engineVolume = 1.0f;
        float effectsVolume = 1.0f;
        bool mirrorEnabled = true;
        int mirrorUpdateEvery = 1;   // redraw the rear-view mirror every n frames (2 = half rate)
        bool hudVisible = true;
        bool startInCockpit = false;
    };

    struct SaveData
    {
        int schemaVersion = kSaveSchemaVersion;
        double odometerKm = 0.0;
        double tripKm = 0.0;
        std::string transmissionMode;   // "manual" / "automatic"; empty = vehicle default
        std::string vehicleId;          // empty = default vehicle
        std::string mapId;              // last map
        SaveSettings settings;
        /// Key binding overrides as (action name, key name) pairs; empty = defaults.
        std::vector<std::pair<std::string, std::string>> bindings;
    };

    struct SaveLoadResult
    {
        SaveData data;
        bool loaded = false;        // a file was read and parsed
        bool readOnly = false;      // newer schema: keep the file untouched
        std::vector<std::string> warnings;
    };

    /// Default location: $CARSIM_SAVE_DIR, else $XDG_DATA_HOME/cna-car-simulator, else
    /// $HOME/.local/share/cna-car-simulator (Linux) or %APPDATA%/cna-car-simulator (Windows).
    [[nodiscard]] std::string DefaultSavePath();

    [[nodiscard]] SaveLoadResult ParseSaveData(const std::string& jsonText);
    [[nodiscard]] SaveLoadResult LoadSaveData(const std::string& path);
    [[nodiscard]] std::string SerializeSaveData(const SaveData& data);
    /// Writes atomically (temporary file + rename). Returns false and sets `error` on failure.
    bool WriteSaveData(const std::string& path, const SaveData& data, std::string& error);
}
