#pragma once
#include <filesystem>
#include <string>

inline std::string toUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}
