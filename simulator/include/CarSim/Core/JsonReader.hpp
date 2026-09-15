// Small validating reader over Sharp Runtime's System::Text::Json elements.
//
// Every accessor appends a human-readable message to a shared error list instead of
// throwing, so a whole document can be checked in one pass and reported at once. Paths in
// the messages use dotted notation ("roads[3].lanes.width").
#pragma once

#include "CarSim/Core/PiecewiseLinear.hpp"

#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/Text/Json/JsonDocument.hpp"
#include "System/Text/Json/JsonElement.hpp"

#include <memory>
#include <string>
#include <vector>

namespace CarSim::Core
{
    class JsonReader
    {
    public:
        using JsonElement = System::Text::Json::JsonElement;

        explicit JsonReader(std::vector<std::string>& errors) : errors_(errors) {}

        [[nodiscard]] std::vector<std::string>& Errors() const { return errors_; }
        void Error(const std::string& message) const { errors_.push_back(message); }

        [[nodiscard]] static bool IsObject(const JsonElement& e);
        [[nodiscard]] static bool IsArray(const JsonElement& e);
        [[nodiscard]] static bool IsNumber(const JsonElement& e);
        [[nodiscard]] static bool IsString(const JsonElement& e);

        /// True when `key` exists on `parent` and is an object; `out` receives it.
        [[nodiscard]] bool HasObject(const JsonElement& parent, const char* key, JsonElement& out) const;
        /// True when `key` exists on `parent` and is an array; `out` receives it.
        [[nodiscard]] bool HasArray(const JsonElement& parent, const char* key, JsonElement& out) const;
        [[nodiscard]] bool Has(const JsonElement& parent, const char* key) const;

        JsonElement RequireObject(const JsonElement& parent, const char* key, const std::string& path) const;
        /// Returns the array or an empty (Undefined) element after recording an error.
        JsonElement RequireArray(const JsonElement& parent, const char* key, const std::string& path) const;

        void Float(const JsonElement& obj, const char* key, float& target, const std::string& path, bool required = false) const;
        void Int(const JsonElement& obj, const char* key, int& target, const std::string& path, bool required = false) const;
        void Bool(const JsonElement& obj, const char* key, bool& target, const std::string& path) const;
        void String(const JsonElement& obj, const char* key, std::string& target, const std::string& path, bool required = false) const;
        void Vec2(const JsonElement& obj, const char* key, Microsoft::Xna::Framework::Vector2& target, const std::string& path, bool required = false) const;
        /// Reads a bare `[x, z]` array element (one already pulled out of an array of points).
        bool Vec2Value(const JsonElement& element, Microsoft::Xna::Framework::Vector2& target, const std::string& path) const;
        void Vec3(const JsonElement& obj, const char* key, Microsoft::Xna::Framework::Vector3& target, const std::string& path, bool required = false) const;
        void Curve(const JsonElement& obj, const char* key, PiecewiseLinear& target, const std::string& path) const;
        void FloatArray(const JsonElement& obj, const char* key, std::vector<float>& target, const std::string& path) const;
        void StringArray(const JsonElement& obj, const char* key, std::vector<std::string>& target, const std::string& path) const;
        /// Array of [x, z] pairs (map-plane polygon or polyline).
        void Vec2Array(const JsonElement& obj, const char* key, std::vector<Microsoft::Xna::Framework::Vector2>& target,
                       const std::string& path, bool required = false) const;

        /// Reads a numeric array element directly (used for inline [x, y] arrays).
        [[nodiscard]] bool NumberAt(const JsonElement& array, std::size_t index, float& out) const;

    private:
        std::vector<std::string>& errors_;
    };

    /// Parses a JSON document from text; on failure records the parser message and returns null.
    std::shared_ptr<System::Text::Json::JsonDocument> ParseJsonText(const std::string& text, const std::string& sourceName,
                                                                     std::vector<std::string>& errors);

    /// Reads a whole file; returns false and records an error when it cannot be read.
    bool ReadTextFile(const std::string& path, std::string& out, std::vector<std::string>& errors);
}
