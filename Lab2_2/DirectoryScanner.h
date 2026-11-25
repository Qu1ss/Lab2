#pragma once
#include <algorithm>
#include <filesystem>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include "CopyTypes.h"
#include "PathText.h"

inline std::filesystem::path normalizedPath(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path result = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        throw std::runtime_error("Не удалось определить полный путь" + errorCodeText(ec.value()));
    }
    if (!result.has_filename()) {
        result = result.parent_path();
    }
    return result;
}

inline bool isSameOrInside(const std::filesystem::path& candidate, const std::filesystem::path& base) {
    const std::filesystem::path c = normalizedPath(candidate);
    const std::filesystem::path b = normalizedPath(base);
    const auto mismatch = std::mismatch(b.begin(), b.end(), c.begin(), c.end());
    return mismatch.first == b.end();
}

class DirectoryScanner {
public:
    DirectoryScanner(std::filesystem::path source, std::filesystem::path target)
        : source_(std::move(source)), target_(std::move(target)) {
        std::error_code ec;
        if (!std::filesystem::is_directory(source_, ec)) {
            throw std::invalid_argument("Исходный путь не существует или не является каталогом");
        }
        if (isSameOrInside(target_, source_)) {
            throw std::invalid_argument("Нельзя копировать каталог внутрь самого себя");
        }
    }

    ScanResult scan() const {
        namespace fs = std::filesystem;
        ScanResult result;
        std::error_code ec;
        fs::recursive_directory_iterator it(source_, fs::directory_options::none, ec);
        const fs::recursive_directory_iterator end;

        for (; !ec && it != end; it.increment(ec)) {
            const fs::path relative = it->path().lexically_relative(source_);
            std::error_code entryEc;
            if (it->is_symlink(entryEc)) {
                result.skipped.push_back(relative);
            } else if (it->is_directory(entryEc)) {
                result.directories.push_back(relative);
            } else if (it->is_regular_file(entryEc)) {
                result.files.push_back(FileTask{it->path(), target_ / relative, relative});
            } else {
                result.skipped.push_back(relative);
            }
        }
        if (ec) {
            throw std::runtime_error("Не удалось обойти исходный каталог" + errorCodeText(ec.value()));
        }

        std::sort(result.directories.begin(), result.directories.end());
        std::sort(result.skipped.begin(), result.skipped.end());
        std::sort(result.files.begin(), result.files.end(),
                  [](const FileTask& a, const FileTask& b) { return a.relative < b.relative; });
        return result;
    }

private:
    std::filesystem::path source_;
    std::filesystem::path target_;
};

inline std::vector<std::string> prepareTargetDirectories(const std::filesystem::path& target,
                                                         const std::vector<std::filesystem::path>& directories) {
    std::vector<std::string> errors;
    auto create = [&errors](const std::filesystem::path& dir) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            errors.push_back("Не удалось создать каталог " + toUtf8(dir) + errorCodeText(ec.value()));
        }
    };
    create(target);
    for (const std::filesystem::path& relative : directories) {
        create(target / relative);
    }
    return errors;
}
