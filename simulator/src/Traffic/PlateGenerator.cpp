#include "CarSim/Traffic/PlateGenerator.hpp"

#include <algorithm>
#include <cctype>

namespace CarSim::Traffic
{
    namespace
    {
        const std::string kLetters = "ABCDEFHIJKLMNPRSTUVXYZ";   // no G, O, Q, W

        bool IsRegionLetter(const char c)
        {
            for (const auto& r : PlateGenerator::Regions()) {
                if (r.letter == c) return true;
            }
            return false;
        }

        bool IsAllowedLetter(const char c) { return kLetters.find(c) != std::string::npos; }
        bool IsDigit(const char c) { return c >= '0' && c <= '9'; }
    }

    const std::vector<PlateRegion>& PlateGenerator::Regions()
    {
        // Weights approximate the population share of the regions (2023 census figures rounded);
        // two-letter shares reflect how far each region is into the letter series.
        static const std::vector<PlateRegion> regions = {
            {'A', "Praha", 12.6f, 0.75f},
            {'S', "Středočeský", 13.1f, 0.0f},
            {'B', "Jihomoravský", 11.3f, 0.35f},
            {'T', "Moravskoslezský", 11.0f, 0.30f},
            {'U', "Ústecký", 7.6f, 0.15f},
            {'M', "Olomoucký", 5.8f, 0.0f},
            {'C', "Jihočeský", 6.0f, 0.0f},
            {'E', "Pardubický", 4.9f, 0.0f},
            {'H', "Královéhradecký", 5.1f, 0.0f},
            {'J', "Vysočina", 4.7f, 0.0f},
            {'L', "Liberecký", 4.1f, 0.0f},
            {'P', "Plzeňský", 5.5f, 0.0f},
            {'Z', "Zlínský", 5.4f, 0.0f},
            {'K', "Karlovarský", 2.7f, 0.0f},
        };
        return regions;
    }

    const std::string& PlateGenerator::AllowedLetters() { return kLetters; }

    PlateGenerator::PlateGenerator(const std::uint32_t seed) : rng_(seed) {}

    std::string PlateGenerator::Compact(const std::string& formatted)
    {
        std::string out;
        for (const char c : formatted) {
            if (c != ' ') out.push_back(c);
        }
        return out;
    }

    std::string PlateGenerator::Format(const std::string& compact)
    {
        if (compact.size() != 7) {
            return compact;
        }
        return compact.substr(0, 3) + " " + compact.substr(3);
    }

    bool PlateGenerator::IsValidStandard(const std::string& plate)
    {
        const std::string p = Compact(plate);
        if (p.size() != 7) return false;
        if (!IsDigit(p[0]) || p[0] == '0') return false;
        if (!IsRegionLetter(p[1])) return false;
        if (!(IsDigit(p[2]) || IsAllowedLetter(p[2]))) return false;
        for (std::size_t i = 3; i < 7; ++i) {
            if (!IsDigit(p[i])) return false;
        }
        for (const char c : p) {
            if (c == 'G' || c == 'O' || c == 'Q' || c == 'W') return false;
        }
        return true;
    }

    bool PlateGenerator::IsValidElectric(const std::string& plate)
    {
        const std::string p = Compact(plate);
        if (p.size() != 7 || p[0] != 'E' || p[1] != 'L') return false;
        for (std::size_t i = 2; i < 7; ++i) {
            if (!(IsDigit(p[i]) || IsAllowedLetter(p[i]))) return false;
        }
        return true;
    }

    std::string PlateGenerator::Generate()
    {
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        std::uniform_int_distribution<int> digit(0, 9);
        std::uniform_int_distribution<int> firstDigit(1, 9);
        std::uniform_int_distribution<int> letter(0, static_cast<int>(kLetters.size()) - 1);
        std::string plate;
        if (unit(rng_) < electricShare_) {
            // EL + three digits + two letters ("EL0 00AA" configuration).
            plate = "EL";
            for (int i = 0; i < 3; ++i) plate.push_back(static_cast<char>('0' + digit(rng_)));
            for (int i = 0; i < 2; ++i) plate.push_back(kLetters[static_cast<std::size_t>(letter(rng_))]);
            return Format(plate);
        }
        float total = 0.0f;
        for (const auto& r : Regions()) total += r.weight;
        float pick = unit(rng_) * total;
        const PlateRegion* region = &Regions().back();
        for (const auto& r : Regions()) {
            pick -= r.weight;
            if (pick <= 0.0f) { region = &r; break; }
        }
        plate.push_back(static_cast<char>('0' + firstDigit(rng_)));
        plate.push_back(region->letter);
        if (unit(rng_) < region->twoLetterShare) {
            plate.push_back(kLetters[static_cast<std::size_t>(letter(rng_))]);
        } else {
            plate.push_back(static_cast<char>('0' + digit(rng_)));
        }
        for (int i = 0; i < 4; ++i) plate.push_back(static_cast<char>('0' + digit(rng_)));
        return Format(plate);
    }

    std::string PlateGenerator::Next()
    {
        for (int attempt = 0; attempt < 1000; ++attempt) {
            const std::string plate = Generate();
            if (issued_.insert(plate).second) {
                return plate;
            }
        }
        // Practically unreachable: fall back to a sequential plate.
        const std::string plate = Format("1S" + std::to_string(issued_.size() % 10) + "0000");
        issued_.insert(plate);
        return plate;
    }
}
