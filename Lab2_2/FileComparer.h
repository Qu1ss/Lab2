#pragma once
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <vector>
#include "CopyTypes.h"

inline bool filesEqual(const std::filesystem::path& a, const std::filesystem::path& b, std::size_t blockSize = 65536) {
    std::ifstream fa(a, std::ios::binary);
    std::ifstream fb(b, std::ios::binary);
    if (!fa || !fb || blockSize == 0) {
        return false;
    }

    std::vector<char> bufferA(blockSize);
    std::vector<char> bufferB(blockSize);
    while (true) {
        fa.read(bufferA.data(), static_cast<std::streamsize>(blockSize));
        fb.read(bufferB.data(), static_cast<std::streamsize>(blockSize));
        if (fa.bad() || fb.bad()) {
            return false;
        }
        const std::streamsize gotA = fa.gcount();
        const std::streamsize gotB = fb.gcount();
        if (gotA != gotB || !std::equal(bufferA.begin(), bufferA.begin() + gotA, bufferB.begin())) {
            return false;
        }
        if (gotA < static_cast<std::streamsize>(blockSize)) {
            return true;
        }
    }
}

inline void verifyResults(const std::vector<FileTask>& tasks, std::vector<FileCopyResult>& results,
                          std::size_t blockSize) {
    for (std::size_t i = 0; i < results.size(); ++i) {
        if (!results[i].success) {
            continue;
        }
        if (filesEqual(tasks[i].source, tasks[i].target, blockSize)) {
            results[i].verified = true;
        } else {
            results[i].success = false;
            results[i].error = "байты исходного и скопированного файла не совпадают";
        }
    }
}
