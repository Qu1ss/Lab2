#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

struct FileTask {
    std::filesystem::path source;
    std::filesystem::path target;
    std::filesystem::path relative;
};

struct FileCopyResult {
    std::filesystem::path relative;
    std::uint64_t bytesCopied = 0;
    bool success = false;
    bool verified = false;
    std::string error;
};

struct ScanResult {
    std::vector<std::filesystem::path> directories;
    std::vector<FileTask> files;
    std::vector<std::filesystem::path> skipped;
};

struct CopyOutcome {
    std::vector<FileCopyResult> results;
    std::vector<std::size_t> startOrder;
    std::vector<std::size_t> completionOrder;
    std::size_t peakConcurrency = 0;
};

inline FileCopyResult failedResult(const FileTask& task, std::uint64_t bytes, std::string error) {
    FileCopyResult result;
    result.relative = task.relative;
    result.bytesCopied = bytes;
    result.success = false;
    result.error = std::move(error);
    return result;
}

inline FileCopyResult succeededResult(const FileTask& task, std::uint64_t bytes) {
    FileCopyResult result;
    result.relative = task.relative;
    result.bytesCopied = bytes;
    result.success = true;
    return result;
}

inline std::string errorCodeText(int code) {
    return " (код " + std::to_string(code) + ")";
}
