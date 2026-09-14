// Czech registration plates (standard series since 2001, see docs/research/czech-plates.md):
// `1A2 3456` = digit, regional letter, digit or letter, four digits. Letters G, O, Q, W are
// never used. Generated plates are unique per generator instance and reproducible by seed.
#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

namespace CarSim::Traffic
{
    struct PlateRegion
    {
        char letter;
        const char* name;
        float weight;             // share of vehicles (population based estimate)
        float twoLetterShare;     // share of plates whose third character is a letter
    };

    class PlateGenerator
    {
    public:
        explicit PlateGenerator(std::uint32_t seed = 1);

        /// Next unique standard plate, formatted with the space: "1A2 3456".
        [[nodiscard]] std::string Next();

        /// Plate without the space (7 characters), for storage.
        [[nodiscard]] static std::string Compact(const std::string& formatted);
        [[nodiscard]] static std::string Format(const std::string& compact);

        /// Structural validation of a standard car plate (either form). Regional letter must be
        /// one of the fourteen regions; no G/O/Q/W anywhere; four trailing digits.
        [[nodiscard]] static bool IsValidStandard(const std::string& plate);
        /// True for the electric-vehicle series "EL0 00AA" (also accepted by the renderer).
        [[nodiscard]] static bool IsValidElectric(const std::string& plate);

        [[nodiscard]] static const std::vector<PlateRegion>& Regions();
        [[nodiscard]] static const std::string& AllowedLetters();   // letters that may appear in a plate

        /// Share of electric plates (0 disables them).
        void SetElectricShare(float share) { electricShare_ = share; }
        [[nodiscard]] std::size_t IssuedCount() const { return issued_.size(); }

    private:
        [[nodiscard]] std::string Generate();

        std::mt19937 rng_;
        std::unordered_set<std::string> issued_;
        float electricShare_ = 0.02f;
    };
}
