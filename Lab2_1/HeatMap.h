#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include "CheckedMath.h"

class HeatMap {
public:
    HeatMap(int width, int height) : width_(width), height_(height) {
        if (width <= 0 || height <= 0) {
            throw std::invalid_argument("Карта: ширина и высота должны быть положительными");
        }
        cells_.assign(std::size_t(width) * std::size_t(height), 0);
    }

    int width() const { return width_; }
    int height() const { return height_; }

    std::int64_t at(int x, int y) const { return cells_[index(x, y)]; }

    void add(int x, int y, std::int64_t value) {
        std::int64_t& cell = cells_[index(x, y)];
        cell = checkedAdd(cell, value);
    }

    std::int64_t minIntensity() const { return *std::min_element(cells_.begin(), cells_.end()); }
    std::int64_t maxIntensity() const { return *std::max_element(cells_.begin(), cells_.end()); }

    std::uint64_t checksum() const {
        std::uint64_t h = 1469598103934665603ULL;
        for (std::int64_t c : cells_) {
            h = (h ^ static_cast<std::uint64_t>(c)) * 1099511628211ULL;
        }
        return h;
    }

private:
    std::size_t index(int x, int y) const {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) {
            throw std::out_of_range("Карта: клетка за границами карты");
        }
        return std::size_t(y) * std::size_t(width_) + std::size_t(x);
    }

    int width_;
    int height_;
    std::vector<std::int64_t> cells_;
};
