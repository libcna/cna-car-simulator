#include "CarSim/Core/SaveData.hpp"
#include "CarSim/Input/InputMapper.hpp"

#include <gtest/gtest.h>

#include <filesystem>

using namespace CarSim;

TEST(SaveData, RoundTripsThroughJson)
{
    Core::SaveData d;
    d.odometerKm = 12345.678;
    d.tripKm = 12.3;
    d.transmissionMode = "automatic";
    d.vehicleId = "lipan_12";
    d.mapId = "lipova";
    d.settings.masterVolume = 0.5f;
    d.settings.mirrorEnabled = false;
    d.settings.startInCockpit = true;
    d.settings.timeOfDayHours = 21.25f;
    d.settings.timeScale = 0.0f;
    d.settings.weather = "overcast";
    d.bindings = {{"Throttle", "Up"}, {"Horn", "H"}};
    const std::string text = Core::SerializeSaveData(d);
    const auto parsed = Core::ParseSaveData(text);
    ASSERT_TRUE(parsed.loaded);
    EXPECT_FALSE(parsed.readOnly);
    EXPECT_NEAR(parsed.data.odometerKm, 12345.678, 0.01);
    EXPECT_NEAR(parsed.data.tripKm, 12.3, 0.01);
    EXPECT_EQ(parsed.data.transmissionMode, "automatic");
    EXPECT_EQ(parsed.data.vehicleId, "lipan_12");
    EXPECT_EQ(parsed.data.mapId, "lipova");
    EXPECT_FLOAT_EQ(parsed.data.settings.masterVolume, 0.5f);
    EXPECT_FALSE(parsed.data.settings.mirrorEnabled);
    EXPECT_TRUE(parsed.data.settings.startInCockpit);
    EXPECT_NEAR(parsed.data.settings.timeOfDayHours, 21.25f, 1e-3f);
    EXPECT_FLOAT_EQ(parsed.data.settings.timeScale, 0.0f);
    EXPECT_EQ(parsed.data.settings.weather, "overcast");
    ASSERT_EQ(parsed.data.bindings.size(), 2u);
    EXPECT_EQ(parsed.data.bindings[1].second, "H");
}

TEST(SaveData, CorruptOrNewerFilesAreHandled)
{
    const auto corrupt = Core::ParseSaveData("{ this is not json");
    EXPECT_FALSE(corrupt.loaded);
    EXPECT_FALSE(corrupt.readOnly);
    EXPECT_FALSE(corrupt.warnings.empty());
    EXPECT_DOUBLE_EQ(corrupt.data.odometerKm, 0.0);

    const auto newer = Core::ParseSaveData(R"({"schemaVersion": 99, "odometerKm": 5})");
    EXPECT_FALSE(newer.loaded);
    EXPECT_TRUE(newer.readOnly);

    const auto outOfRange = Core::ParseSaveData(R"({"schemaVersion": 1, "odometerKm": -5, "settings": {"masterVolume": 7}})");
    EXPECT_TRUE(outOfRange.loaded);
    EXPECT_DOUBLE_EQ(outOfRange.data.odometerKm, 0.0);
    EXPECT_FLOAT_EQ(outOfRange.data.settings.masterVolume, 1.0f);
}

TEST(SaveData, WritesAndReadsFiles)
{
    const auto dir = std::filesystem::temp_directory_path() / "carsim-save-test";
    std::filesystem::create_directories(dir);
    const std::string path = (dir / "save.json").string();
    Core::SaveData d;
    d.odometerKm = 42.5;
    std::string error;
    ASSERT_TRUE(Core::WriteSaveData(path, d, error)) << error;
    const auto loaded = Core::LoadSaveData(path);
    ASSERT_TRUE(loaded.loaded);
    EXPECT_NEAR(loaded.data.odometerKm, 42.5, 0.001);
    EXPECT_FALSE(std::filesystem::exists(path + ".tmp"));
    const auto missing = Core::LoadSaveData((dir / "nothing.json").string());
    EXPECT_FALSE(missing.loaded);
    EXPECT_TRUE(missing.warnings.empty());
    std::filesystem::remove_all(dir);
}

TEST(InputBindings, KeyNamesRoundTripAndOverridesApply)
{
    using Microsoft::Xna::Framework::Input::Keys;
    Keys key;
    EXPECT_TRUE(Input::InputMapper::KeyFromName("Space", key));
    EXPECT_EQ(key, Keys::Space);
    EXPECT_TRUE(Input::InputMapper::KeyFromName("left shift", key));
    EXPECT_EQ(key, Keys::LeftShift);
    EXPECT_TRUE(Input::InputMapper::KeyFromName("F3", key));
    EXPECT_EQ(key, Keys::F3);
    EXPECT_TRUE(Input::InputMapper::KeyFromName("7", key));
    EXPECT_EQ(key, Keys::D7);
    EXPECT_FALSE(Input::InputMapper::KeyFromName("no such key", key));
    for (const auto& b : Input::InputMapper::DefaultBindings()) {
        Keys parsed;
        EXPECT_TRUE(Input::InputMapper::KeyFromName(Input::InputMapper::KeyName(b.key), parsed)) << Input::InputMapper::KeyName(b.key);
        EXPECT_EQ(parsed, b.key);
    }
    Input::GameAction action;
    EXPECT_TRUE(Input::InputMapper::ActionFromName("toggleengine", action));
    EXPECT_EQ(action, Input::GameAction::ToggleEngine);

    Input::InputMapper mapper;
    std::vector<std::string> warnings;
    mapper.ApplyOverrides({{"Horn", "J"}, {"Nonsense", "K"}, {"Throttle", "no key"}}, warnings);
    EXPECT_EQ(warnings.size(), 2u);
    EXPECT_NE(mapper.KeysFor(Input::GameAction::Horn).find("J"), std::string::npos);
    const auto named = mapper.NamedBindings();
    EXPECT_FALSE(named.empty());
}
