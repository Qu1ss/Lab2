#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "CopyTypes.h"
#include "PathText.h"

struct CopySummary {
    std::string modeName;
    std::filesystem::path source;
    std::filesystem::path target;
    std::size_t blockSize = 0;
    std::size_t maxConcurrent = 0;
    ScanResult scan;
    std::vector<std::string> directoryErrors;
    CopyOutcome outcome;
    double copyMilliseconds = 0.0;
};

inline std::size_t countSucceeded(const std::vector<FileCopyResult>& results) {
    return static_cast<std::size_t>(std::count_if(results.begin(), results.end(),
                                                  [](const FileCopyResult& r) { return r.success; }));
}

inline std::size_t countFailed(const std::vector<FileCopyResult>& results) {
    return results.size() - countSucceeded(results);
}

inline std::uint64_t totalCopiedBytes(const std::vector<FileCopyResult>& results) {
    std::uint64_t total = 0;
    for (const FileCopyResult& r : results) {
        if (r.success) total += r.bytesCopied;
    }
    return total;
}

inline std::string describeResult(const FileCopyResult& r) {
    std::ostringstream os;
    if (r.success) {
        os << toUtf8(r.relative) << " - " << r.bytesCopied << " байт, "
           << (r.verified ? "байты совпадают" : "не проверено");
    } else {
        os << toUtf8(r.relative) << " - ОШИБКА: " << r.error << " (скопировано байт: " << r.bytesCopied << ")";
    }
    return os.str();
}

inline std::string formatReport(const CopySummary& s) {
    const std::vector<FileCopyResult>& results = s.outcome.results;
    std::ostringstream out;
    out << "Отчёт о копировании\n"
        << "Режим: " << s.modeName << '\n'
        << "Исходный каталог: " << toUtf8(s.source) << '\n'
        << "Целевой каталог: " << toUtf8(s.target) << '\n'
        << "Размер блока: " << s.blockSize << " байт\n"
        << "Максимум одновременно копируемых файлов: " << s.maxConcurrent << '\n'
        << "Пиковое число одновременных копирований: " << s.outcome.peakConcurrency << '\n'
        << "Время копирования: " << s.copyMilliseconds << " мс\n\n"
        << "Итог: файлов " << results.size() << ", успешно " << countSucceeded(results)
        << ", с ошибками " << countFailed(results) << ", скопировано байт " << totalCopiedBytes(results) << "\n\n";

    out << "Файлы (по порядку списка):\n";
    for (const FileCopyResult& r : results) {
        out << "  " << (r.success ? "[OK]     " : "[ОШИБКА] ") << describeResult(r) << '\n';
    }

    if (!s.directoryErrors.empty()) {
        out << "\nОшибки создания каталогов:\n";
        for (const std::string& e : s.directoryErrors) out << "  " << e << '\n';
    }
    if (!s.scan.skipped.empty()) {
        out << "\nПропущено (символические ссылки и особые файлы):\n";
        for (const std::filesystem::path& p : s.scan.skipped) out << "  " << toUtf8(p) << '\n';
    }

    out << "\nПорядок запуска:\n";
    for (std::size_t i = 0; i < s.outcome.startOrder.size(); ++i) {
        out << "  " << (i + 1) << ". " << toUtf8(results[s.outcome.startOrder[i]].relative) << '\n';
    }
    out << "\nПорядок завершения:\n";
    for (std::size_t i = 0; i < s.outcome.completionOrder.size(); ++i) {
        out << "  " << (i + 1) << ". " << toUtf8(results[s.outcome.completionOrder[i]].relative) << '\n';
    }
    return out.str();
}

inline void saveReport(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Не удалось открыть файл отчёта для записи");
    }
    out << text;
    if (!out) {
        throw std::runtime_error("Не удалось записать файл отчёта");
    }
}
