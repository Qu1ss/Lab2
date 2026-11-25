#pragma once
#include <cstdint>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "HeatMap.h"
#include "Source.h"

inline std::vector<Source> loadSources(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Не удалось открыть файл источников");

    std::vector<Source> result;
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        std::istringstream row(line);
        std::string firstToken;
        if (!(row >> firstToken) || firstToken[0] == '#') continue;

        row.clear();
        row.str(line);
        Source s;
        if (!(row >> s.x >> s.y >> s.radius >> s.intensity)) {
            throw std::runtime_error("Некорректный источник в строке " + std::to_string(lineNo));
        }
        validate(s);
        result.push_back(s);
    }
    return result;
}

inline std::vector<Source> generateRandomSources(int count, int width, int height,
                                                 int maxRadius, std::int64_t maxIntensity,
                                                 unsigned seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> distX(0, width - 1);
    std::uniform_int_distribution<int> distY(0, height - 1);
    std::uniform_int_distribution<int> distR(0, maxRadius);
    std::uniform_int_distribution<std::int64_t> distA(0, maxIntensity);

    std::vector<Source> result;
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        Source s;
        s.x = distX(rng);
        s.y = distY(rng);
        s.radius = distR(rng);
        s.intensity = distA(rng);
        result.push_back(s);
    }
    return result;
}

inline void saveMatrix(const HeatMap& map, const std::string& path) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Не удалось открыть файл матрицы для записи");
    out << map.width() << ' ' << map.height() << '\n';
    for (int y = 0; y < map.height(); ++y) {
        for (int x = 0; x < map.width(); ++x) {
            out << map.at(x, y) << (x + 1 < map.width() ? ' ' : '\n');
        }
    }
}

inline std::uint8_t intensityToRed(std::int64_t value, std::int64_t maxValue) {
    if (maxValue <= 0) return 0;
    const std::int64_t limit = std::numeric_limits<std::int64_t>::max() / 255;
    while (maxValue > limit) {
        maxValue >>= 1;
        value >>= 1;
    }
    return static_cast<std::uint8_t>(value * 255 / maxValue);
}

inline void savePpm(const HeatMap& map, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Не удалось открыть файл изображения для записи");

    const std::int64_t maxValue = map.maxIntensity();
    out << "P6\n" << map.width() << ' ' << map.height() << "\n255\n";
    std::vector<char> row(std::size_t(map.width()) * 3);
    for (int y = 0; y < map.height(); ++y) {
        for (int x = 0; x < map.width(); ++x) {
            row[std::size_t(x) * 3] = static_cast<char>(intensityToRed(map.at(x, y), maxValue));
            row[std::size_t(x) * 3 + 1] = 0;
            row[std::size_t(x) * 3 + 2] = 0;
        }
        out.write(row.data(), std::streamsize(row.size()));
    }
}
