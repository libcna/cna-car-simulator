#include "CarSim/Core/JsonReader.hpp"

#include "System/Text/Json/JsonValueKind.hpp"

#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace CarSim::Core
{
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;
    using System::Text::Json::JsonDocument;
    using System::Text::Json::JsonElement;
    using System::Text::Json::JsonValueKind;

    bool JsonReader::IsObject(const JsonElement& e) { return e.getValueKindProperty() == JsonValueKind::Object; }
    bool JsonReader::IsArray(const JsonElement& e) { return e.getValueKindProperty() == JsonValueKind::Array; }
    bool JsonReader::IsNumber(const JsonElement& e) { return e.getValueKindProperty() == JsonValueKind::Number; }
    bool JsonReader::IsString(const JsonElement& e) { return e.getValueKindProperty() == JsonValueKind::String; }

    bool JsonReader::HasObject(const JsonElement& parent, const char* key, JsonElement& out) const
    {
        JsonElement e;
        if (!IsObject(parent) || !parent.TryGetProperty(key, e) || !IsObject(e)) {
            return false;
        }
        out = e;
        return true;
    }

    bool JsonReader::HasArray(const JsonElement& parent, const char* key, JsonElement& out) const
    {
        JsonElement e;
        if (!IsObject(parent) || !parent.TryGetProperty(key, e) || !IsArray(e)) {
            return false;
        }
        out = e;
        return true;
    }

    bool JsonReader::Has(const JsonElement& parent, const char* key) const
    {
        JsonElement e;
        return IsObject(parent) && parent.TryGetProperty(key, e) && e.getValueKindProperty() != JsonValueKind::Null;
    }

    JsonElement JsonReader::RequireObject(const JsonElement& parent, const char* key, const std::string& path) const
    {
        JsonElement e;
        if (!HasObject(parent, key, e)) {
            errors_.push_back(path + "." + key + ": object is required");
        }
        return e;
    }

    JsonElement JsonReader::RequireArray(const JsonElement& parent, const char* key, const std::string& path) const
    {
        JsonElement e;
        if (!HasArray(parent, key, e)) {
            errors_.push_back(path + "." + key + ": array is required");
            return JsonElement();
        }
        return e;
    }

    void JsonReader::Float(const JsonElement& obj, const char* key, float& target, const std::string& path, const bool required) const
    {
        JsonElement e;
        if (!IsObject(obj) || !obj.TryGetProperty(key, e)) {
            if (required) {
                errors_.push_back(path + "." + key + ": number is required");
            }
            return;
        }
        if (!IsNumber(e)) {
            errors_.push_back(path + "." + key + ": must be a number");
            return;
        }
        target = static_cast<float>(e.GetDouble());
    }

    void JsonReader::Int(const JsonElement& obj, const char* key, int& target, const std::string& path, const bool required) const
    {
        JsonElement e;
        if (!IsObject(obj) || !obj.TryGetProperty(key, e)) {
            if (required) {
                errors_.push_back(path + "." + key + ": integer is required");
            }
            return;
        }
        if (!IsNumber(e)) {
            errors_.push_back(path + "." + key + ": must be a number");
            return;
        }
        target = static_cast<int>(std::lround(e.GetDouble()));
    }

    void JsonReader::Bool(const JsonElement& obj, const char* key, bool& target, const std::string& path) const
    {
        JsonElement e;
        if (!IsObject(obj) || !obj.TryGetProperty(key, e)) {
            return;
        }
        const auto kind = e.getValueKindProperty();
        if (kind == JsonValueKind::True) {
            target = true;
        } else if (kind == JsonValueKind::False) {
            target = false;
        } else {
            errors_.push_back(path + "." + key + ": must be a boolean");
        }
    }

    void JsonReader::String(const JsonElement& obj, const char* key, std::string& target, const std::string& path, const bool required) const
    {
        JsonElement e;
        if (!IsObject(obj) || !obj.TryGetProperty(key, e)) {
            if (required) {
                errors_.push_back(path + "." + key + ": string is required");
            }
            return;
        }
        if (!IsString(e)) {
            errors_.push_back(path + "." + key + ": must be a string");
            return;
        }
        target = e.GetString();
    }

    bool JsonReader::NumberAt(const JsonElement& array, const std::size_t index, float& out) const
    {
        if (!IsArray(array) || index >= static_cast<std::size_t>(array.GetArrayLength())) {
            return false;
        }
        const auto items = array.EnumerateArray();
        if (!IsNumber(items[index])) {
            return false;
        }
        out = static_cast<float>(items[index].GetDouble());
        return true;
    }

    void JsonReader::Vec2(const JsonElement& obj, const char* key, Vector2& target, const std::string& path, const bool required) const
    {
        JsonElement e;
        if (!IsObject(obj) || !obj.TryGetProperty(key, e)) {
            if (required) {
                errors_.push_back(path + "." + key + ": [x, z] is required");
            }
            return;
        }
        float v[2];
        if (!IsArray(e) || e.GetArrayLength() != 2 || !NumberAt(e, 0, v[0]) || !NumberAt(e, 1, v[1])) {
            errors_.push_back(path + "." + key + ": must be an array of two numbers");
            return;
        }
        target = Vector2(v[0], v[1]);
    }

    void JsonReader::Vec3(const JsonElement& obj, const char* key, Vector3& target, const std::string& path, const bool required) const
    {
        JsonElement e;
        if (!IsObject(obj) || !obj.TryGetProperty(key, e)) {
            if (required) {
                errors_.push_back(path + "." + key + ": [x, y, z] is required");
            }
            return;
        }
        float v[3];
        if (!IsArray(e) || e.GetArrayLength() != 3 || !NumberAt(e, 0, v[0]) || !NumberAt(e, 1, v[1]) || !NumberAt(e, 2, v[2])) {
            errors_.push_back(path + "." + key + ": must be an array of three numbers");
            return;
        }
        target = Vector3(v[0], v[1], v[2]);
    }

    void JsonReader::Curve(const JsonElement& obj, const char* key, PiecewiseLinear& target, const std::string& path) const
    {
        JsonElement e;
        if (!IsObject(obj) || !obj.TryGetProperty(key, e)) {
            return;
        }
        if (!IsArray(e)) {
            errors_.push_back(path + "." + key + ": must be an array of [x, y] pairs");
            return;
        }
        std::vector<std::pair<float, float>> points;
        for (const auto& item : e.EnumerateArray()) {
            float x = 0.0f;
            float y = 0.0f;
            if (!IsArray(item) || item.GetArrayLength() != 2 || !NumberAt(item, 0, x) || !NumberAt(item, 1, y)) {
                errors_.push_back(path + "." + key + ": each point must be [x, y]");
                return;
            }
            points.emplace_back(x, y);
        }
        target = PiecewiseLinear(std::move(points));
    }

    void JsonReader::FloatArray(const JsonElement& obj, const char* key, std::vector<float>& target, const std::string& path) const
    {
        JsonElement e;
        if (!IsObject(obj) || !obj.TryGetProperty(key, e)) {
            return;
        }
        if (!IsArray(e)) {
            errors_.push_back(path + "." + key + ": must be an array of numbers");
            return;
        }
        std::vector<float> values;
        for (const auto& item : e.EnumerateArray()) {
            if (!IsNumber(item)) {
                errors_.push_back(path + "." + key + ": must be an array of numbers");
                return;
            }
            values.push_back(static_cast<float>(item.GetDouble()));
        }
        target = std::move(values);
    }

    void JsonReader::StringArray(const JsonElement& obj, const char* key, std::vector<std::string>& target, const std::string& path) const
    {
        JsonElement e;
        if (!IsObject(obj) || !obj.TryGetProperty(key, e)) {
            return;
        }
        if (!IsArray(e)) {
            errors_.push_back(path + "." + key + ": must be an array of strings");
            return;
        }
        std::vector<std::string> values;
        for (const auto& item : e.EnumerateArray()) {
            if (!IsString(item)) {
                errors_.push_back(path + "." + key + ": must be an array of strings");
                return;
            }
            values.push_back(item.GetString());
        }
        target = std::move(values);
    }

    void JsonReader::Vec2Array(const JsonElement& obj, const char* key, std::vector<Vector2>& target, const std::string& path,
                               const bool required) const
    {
        JsonElement e;
        if (!IsObject(obj) || !obj.TryGetProperty(key, e)) {
            if (required) {
                errors_.push_back(path + "." + key + ": array of [x, z] points is required");
            }
            return;
        }
        if (!IsArray(e)) {
            errors_.push_back(path + "." + key + ": must be an array of [x, z] points");
            return;
        }
        std::vector<Vector2> values;
        for (const auto& item : e.EnumerateArray()) {
            float x = 0.0f;
            float z = 0.0f;
            if (!IsArray(item) || item.GetArrayLength() != 2 || !NumberAt(item, 0, x) || !NumberAt(item, 1, z)) {
                errors_.push_back(path + "." + key + ": each point must be [x, z]");
                return;
            }
            values.emplace_back(x, z);
        }
        target = std::move(values);
    }

    std::shared_ptr<JsonDocument> ParseJsonText(const std::string& text, const std::string& sourceName, std::vector<std::string>& errors)
    {
        try {
            return JsonDocument::Parse(text);
        } catch (const std::exception& ex) {
            errors.push_back(sourceName + ": JSON parse error: " + ex.what());
            return nullptr;
        }
    }

    bool ReadTextFile(const std::string& path, std::string& out, std::vector<std::string>& errors)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            errors.push_back("cannot open '" + path + "'");
            return false;
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        out = buffer.str();
        return true;
    }
}
