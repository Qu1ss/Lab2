#include "CopyRunners.h"
#include "Utf8Console.h"
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include "CopyReport.h"
#include "DirectoryScanner.h"
#include "FileComparer.h"

namespace {

using Args = std::map<std::string, std::string>;

Args parseArgs(int argc, char** argv) {
    Args args;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string key = argv[i];
        if (key.rfind("--", 0) != 0) {
            throw std::invalid_argument("Аргументы должны иметь вид --имя значение");
        }
        args[key.substr(2)] = argv[i + 1];
    }
    return args;
}

std::string getString(const Args& a, const std::string& key, const std::string& def) {
    auto it = a.find(key);
    return it == a.end() ? def : it->second;
}

long long getInt(const Args& a, const std::string& key, long long def) {
    auto it = a.find(key);
    if (it == a.end()) {
        return def;
    }
    try {
        return std::stoll(it->second);
    } catch (const std::exception&) {
        throw std::invalid_argument("Некорректное число в аргументе --" + key);
    }
}

std::string nowString() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
    std::ostringstream os;
    os << std::put_time(&tm, "%F %T") << '.' << std::setfill('0') << std::setw(3) << ms;
    return os.str();
}

double msSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

std::unique_ptr<ICopyRunner> makeRunner(const std::string& mode, std::size_t blockSize, std::size_t concurrency) {
    if (mode == "sync") {
        return std::make_unique<SyncCopyRunner>(SyncFileCopier(blockSize));
    }
    if (mode == "async") {
        return std::make_unique<AsyncCopyRunner>(AsyncFileCopier(blockSize), concurrency);
    }
    throw std::invalid_argument("Режим должен быть sync или async");
}

void printUsage() {
    std::cout <<
        "Использование: copier --source каталог --target каталог --mode sync|async\n"
        "                      [--block-size байт] [--concurrency N] [--report файл]\n"
        "По умолчанию: block-size=65536, concurrency=4, report=copy_report.txt\n";
}

}

int main(int argc, char** argv) {
    Utf8Console console;
    try {
        const Args args = parseArgs(argc, argv);
        if (!args.count("source") || !args.count("target") || !args.count("mode")) {
            printUsage();
            return 1;
        }

        const std::string mode = args.at("mode");
        const long long blockSize = getInt(args, "block-size", 65536);
        const long long concurrency = getInt(args, "concurrency", 4);
        if (blockSize <= 0) throw std::invalid_argument("Размер блока должен быть положительным");
        if (concurrency <= 0) throw std::invalid_argument("Число одновременно копируемых файлов должно быть >= 1");
        const std::filesystem::path source = args.at("source");
        const std::filesystem::path target = args.at("target");
        const std::filesystem::path reportPath = getString(args, "report", "copy_report.txt");

        const auto programStart = std::chrono::steady_clock::now();
        std::cout << "Время начала:    " << nowString() << '\n';

        DirectoryScanner scanner(source, target);
        CopySummary summary;
        summary.modeName = mode;
        summary.source = source;
        summary.target = target;
        summary.blockSize = static_cast<std::size_t>(blockSize);
        summary.maxConcurrent = mode == "sync" ? 1 : static_cast<std::size_t>(concurrency);
        summary.scan = scanner.scan();
        summary.directoryErrors = prepareTargetDirectories(target, summary.scan.directories);

        auto runner = makeRunner(mode, summary.blockSize, summary.maxConcurrent);
        const auto copyStart = std::chrono::steady_clock::now();
        summary.outcome = runner->run(summary.scan.files);
        summary.copyMilliseconds = msSince(copyStart);

        verifyResults(summary.scan.files, summary.outcome.results, summary.blockSize);
        saveReport(reportPath, formatReport(summary));

        const double totalMs = msSince(programStart);
        const std::vector<FileCopyResult>& results = summary.outcome.results;
        std::cout << "Режим: " << mode << ", блок: " << summary.blockSize << " байт, одновременно файлов: "
                  << summary.maxConcurrent << '\n'
                  << "Найдено файлов: " << results.size() << ", каталогов: " << summary.scan.directories.size()
                  << ", пропущено: " << summary.scan.skipped.size() << '\n'
                  << "Порядок завершения копирования:\n";
        for (std::size_t i = 0; i < summary.outcome.completionOrder.size(); ++i) {
            const FileCopyResult& r = results[summary.outcome.completionOrder[i]];
            std::cout << "  " << (i + 1) << ". " << describeResult(r) << '\n';
        }
        for (const std::string& e : summary.directoryErrors) {
            std::cout << "  " << e << '\n';
        }
        std::cout << "Результат: успешно " << countSucceeded(results) << ", с ошибками " << countFailed(results)
                  << ", скопировано байт " << totalCopiedBytes(results) << '\n'
                  << "Пик одновременных копирований: " << summary.outcome.peakConcurrency << '\n'
                  << "Время копирования: " << summary.copyMilliseconds << " мс\n"
                  << "Время окончания: " << nowString() << '\n'
                  << "Общее время:     " << totalMs << " мс\n"
                  << "Отчёт сохранён\n";
    } catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << '\n';
        return 2;
    }
    return 0;
}
