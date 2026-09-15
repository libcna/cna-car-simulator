#include "CarSim/Core/SaveData.hpp"

#include "CarSim/Core/Weather.hpp"

#include "CarSim/Core/JsonReader.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace CarSim::Core
{
    using System::Text::Json::JsonElement;

    namespace
    {
        std::string Escape(const std::string& s)
        {
            std::string out;
            out.reserve(s.size() + 2);
            for (const char c : s) {
                switch (c) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (static_cast<unsigned char>(c) < 0x20) {
                            char buf[8];
                            std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                            out += buf;
                        } else {
                            out.push_back(c);
                        }
                }
            }
            return out;
        }

        const char* EnvOrNull(const char* name)
        {
            const char* v = std::getenv(name);
            return (v && *v) ? v : nullptr;
        }
    }

    std::string DefaultSavePath()
    {
        namespace fs = std::filesystem;
        fs::path dir;
        if (const char* custom = EnvOrNull("CARSIM_SAVE_DIR")) {
            dir = custom;
        } else if (const char* xdg = EnvOrNull("XDG_DATA_HOME")) {
            dir = fs::path(xdg) / "cna-car-simulator";
        } else if (const char* appdata = EnvOrNull("APPDATA")) {
            dir = fs::path(appdata) / "cna-car-simulator";
        } else if (const char* home = EnvOrNull("HOME")) {
            dir = fs::path(home) / ".local" / "share" / "cna-car-simulator";
        } else {
            dir = fs::path(".");
        }
        return (dir / "save.json").string();
    }

    SaveLoadResult ParseSaveData(const std::string& jsonText)
    {
        SaveLoadResult result;
        std::vector<std::string> errors;
        const auto doc = ParseJsonText(jsonText, "save.json", errors);
        if (!doc) {
            result.warnings.insert(result.warnings.end(), errors.begin(), errors.end());
            result.warnings.push_back("save.json: unreadable, using defaults");
            return result;
        }
        const JsonElement root = doc->getRootElementProperty();
        JsonReader r(errors);
        if (!JsonReader::IsObject(root)) {
            result.warnings.push_back("save.json: root is not an object, using defaults");
            return result;
        }
        int version = 0;
        r.Int(root, "schemaVersion", version, "save", true);
        if (version > kSaveSchemaVersion) {
            result.warnings.push_back("save.json: schemaVersion " + std::to_string(version) + " is newer than this build supports; not saving");
            result.readOnly = true;
            return result;
        }
        SaveData& d = result.data;
        float odo = 0.0f;
        float trip = 0.0f;
        r.Float(root, "odometerKm", odo, "save");
        r.Float(root, "tripKm", trip, "save");
        d.odometerKm = odo;
        d.tripKm = trip;
        r.String(root, "transmissionMode", d.transmissionMode, "save");
        r.String(root, "vehicleId", d.vehicleId, "save");
        r.String(root, "mapId", d.mapId, "save");
        JsonElement settings;
        if (r.HasObject(root, "settings", settings)) {
            r.Float(settings, "masterVolume", d.settings.masterVolume, "save.settings");
            r.Float(settings, "engineVolume", d.settings.engineVolume, "save.settings");
            r.Float(settings, "effectsVolume", d.settings.effectsVolume, "save.settings");
            r.Bool(settings, "mirrorEnabled", d.settings.mirrorEnabled, "save.settings");
            r.Int(settings, "mirrorUpdateEvery", d.settings.mirrorUpdateEvery, "save.settings");
            d.settings.mirrorUpdateEvery = std::clamp(d.settings.mirrorUpdateEvery, 1, 8);
            r.Float(settings, "timeOfDayHours", d.settings.timeOfDayHours, "save.settings");
            d.settings.timeOfDayHours = std::clamp(d.settings.timeOfDayHours, 0.0f, 24.0f);
            r.Float(settings, "timeScale", d.settings.timeScale, "save.settings");
            d.settings.timeScale = std::clamp(d.settings.timeScale, 0.0f, 3600.0f);
            r.String(settings, "weather", d.settings.weather, "save.settings");
            {
                WeatherKind kind = WeatherKind::FewClouds;
                if (!WeatherFromName(d.settings.weather, kind)) {
                    d.settings.weather = "few-clouds";
                }
            }
            r.Bool(settings, "hudVisible", d.settings.hudVisible, "save.settings");
            r.Bool(settings, "startInCockpit", d.settings.startInCockpit, "save.settings");
        }
        JsonElement bindings;
        if (r.HasArray(root, "bindings", bindings)) {
            for (const auto& b : bindings.EnumerateArray()) {
                std::string action;
                std::string key;
                r.String(b, "action", action, "save.bindings", true);
                r.String(b, "key", key, "save.bindings", true);
                if (!action.empty() && !key.empty()) {
                    d.bindings.emplace_back(action, key);
                }
            }
        }
        // Sanity clamps.
        const auto clamp01 = [](float& v) { v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
        clamp01(d.settings.masterVolume);
        clamp01(d.settings.engineVolume);
        clamp01(d.settings.effectsVolume);
        if (d.odometerKm < 0.0 || d.odometerKm > 9999999.0) {
            result.warnings.push_back("save.json: odometer out of range, reset to 0");
            d.odometerKm = 0.0;
        }
        if (d.tripKm < 0.0) d.tripKm = 0.0;
        result.warnings.insert(result.warnings.end(), errors.begin(), errors.end());
        result.loaded = true;
        d.schemaVersion = kSaveSchemaVersion;   // in-memory upgrade of older versions
        return result;
    }

    SaveLoadResult LoadSaveData(const std::string& path)
    {
        std::string text;
        std::vector<std::string> errors;
        if (!std::filesystem::exists(path)) {
            SaveLoadResult fresh;
            return fresh;
        }
        if (!ReadTextFile(path, text, errors)) {
            SaveLoadResult r;
            r.warnings = errors;
            return r;
        }
        return ParseSaveData(text);
    }

    std::string SerializeSaveData(const SaveData& data)
    {
        std::ostringstream out;
        out.setf(std::ios::fixed);
        out << "{\n";
        out << "  \"schemaVersion\": " << kSaveSchemaVersion << ",\n";
        out << "  \"odometerKm\": " << std::setprecision(3) << data.odometerKm << ",\n";
        out << "  \"tripKm\": " << std::setprecision(3) << data.tripKm << ",\n";
        out << "  \"transmissionMode\": \"" << Escape(data.transmissionMode) << "\",\n";
        out << "  \"vehicleId\": \"" << Escape(data.vehicleId) << "\",\n";
        out << "  \"mapId\": \"" << Escape(data.mapId) << "\",\n";
        out << "  \"settings\": {\n";
        out << "    \"masterVolume\": " << std::setprecision(3) << data.settings.masterVolume << ",\n";
        out << "    \"engineVolume\": " << data.settings.engineVolume << ",\n";
        out << "    \"effectsVolume\": " << data.settings.effectsVolume << ",\n";
        out << "    \"mirrorEnabled\": " << (data.settings.mirrorEnabled ? "true" : "false") << ",\n";
        out << "    \"mirrorUpdateEvery\": " << data.settings.mirrorUpdateEvery << ",\n";
        out << "    \"timeOfDayHours\": " << data.settings.timeOfDayHours << ",\n";
        out << "    \"timeScale\": " << data.settings.timeScale << ",\n";
        out << "    \"weather\": \"" << Escape(data.settings.weather) << "\",\n";
        out << "    \"hudVisible\": " << (data.settings.hudVisible ? "true" : "false") << ",\n";
        out << "    \"startInCockpit\": " << (data.settings.startInCockpit ? "true" : "false") << "\n";
        out << "  },\n";
        out << "  \"bindings\": [";
        for (std::size_t i = 0; i < data.bindings.size(); ++i) {
            out << (i == 0 ? "\n" : ",\n") << "    {\"action\": \"" << Escape(data.bindings[i].first) << "\", \"key\": \"" << Escape(data.bindings[i].second) << "\"}";
        }
        out << (data.bindings.empty() ? "]\n" : "\n  ]\n");
        out << "}\n";
        return out.str();
    }

    bool WriteSaveData(const std::string& path, const SaveData& data, std::string& error)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path target(path);
        if (!target.parent_path().empty()) {
            fs::create_directories(target.parent_path(), ec);
        }
        const fs::path temp = target.string() + ".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out) {
                error = "cannot write '" + temp.string() + "'";
                return false;
            }
            out << SerializeSaveData(data);
            if (!out) {
                error = "write failed for '" + temp.string() + "'";
                return false;
            }
        }
        fs::rename(temp, target, ec);
        if (ec) {
            error = "cannot replace '" + path + "': " + ec.message();
            fs::remove(temp, ec);
            return false;
        }
        return true;
    }
}
