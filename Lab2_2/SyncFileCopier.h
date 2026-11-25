#pragma once
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <vector>
#include "CopyTypes.h"

class SyncFileCopier {
public:
    explicit SyncFileCopier(std::size_t blockSize) : blockSize_(blockSize) {
        if (blockSize == 0) {
            throw std::invalid_argument("Размер блока должен быть положительным");
        }
    }

    FileCopyResult copy(const FileTask& task) const {
        std::uint64_t total = 0;
        try {
            std::ifstream in(task.source, std::ios::binary);
            if (!in) {
                return failedResult(task, total, "не удалось открыть исходный файл");
            }
            std::ofstream out(task.target, std::ios::binary | std::ios::trunc);
            if (!out) {
                return failedResult(task, total, "не удалось создать целевой файл");
            }

            std::vector<char> buffer(blockSize_);
            while (in) {
                in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const std::streamsize got = in.gcount();
                if (got > 0) {
                    out.write(buffer.data(), got);
                    if (!out) {
                        return failedResult(task, total, "ошибка записи");
                    }
                    total += static_cast<std::uint64_t>(got);
                }
            }
            if (in.bad()) {
                return failedResult(task, total, "ошибка чтения");
            }
            out.close();
            if (out.fail()) {
                return failedResult(task, total, "ошибка записи при закрытии файла");
            }
        } catch (const std::exception&) {
            return failedResult(task, total, "внутренняя ошибка при копировании");
        }
        return succeededResult(task, total);
    }

private:
    std::size_t blockSize_;
};
