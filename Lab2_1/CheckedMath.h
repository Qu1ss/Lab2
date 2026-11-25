#pragma once
#include <cstdint>
#include <limits>
#include <stdexcept>

inline std::int64_t checkedAdd(std::int64_t a, std::int64_t b) {
    if (a > std::numeric_limits<std::int64_t>::max() - b) {
        throw std::overflow_error("Переполнение при сложении");
    }
    return a + b;
}

inline std::int64_t checkedMul(std::int64_t a, std::int64_t b) {
    if (a != 0 && b > std::numeric_limits<std::int64_t>::max() / a) {
        throw std::overflow_error("Переполнение при умножении");
    }
    return a * b;
}
