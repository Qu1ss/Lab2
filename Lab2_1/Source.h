#pragma once
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include "CheckedMath.h"

struct Source {
    int x = 0;
    int y = 0;
    int radius = 0;
    std::int64_t intensity = 0;

    std::int64_t contributionAt(int cx, int cy) const {
        const std::int64_t d = std::abs(std::int64_t(cx) - x) + std::abs(std::int64_t(cy) - y);
        const std::int64_t k = std::int64_t(radius) + 1 - d;
        if (k <= 0) {
            return 0;
        }
        return checkedMul(intensity, k);
    }
};

inline void validate(const Source& s) {
    if (s.radius < 0) throw std::invalid_argument("Источник: радиус должен быть >= 0");
    if (s.intensity < 0) throw std::invalid_argument("Источник: интенсивность должна быть >= 0");
}
