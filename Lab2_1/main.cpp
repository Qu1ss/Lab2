#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include "CellUpdater.h"
#include "HeatMap.h"
#include "HeatMapGenerator.h"
#include "HeatMapIO.h"

namespace {

using Args = std::map<std::string, std::string>;

class Utf8Console {
public:
    Utf8Console() {
#ifdef _WIN32
        previousCodePage_ = GetConsoleOutputCP();
        SetConsoleOutputCP(CP_UTF8);
#endif
    }

    ~Utf8Console() {
#ifdef _WIN32
        if (previousCodePage_ != 0) {
            SetConsoleOutputCP(previousCodePage_);
        }
#endif
    }

    Utf8Console(const Utf8Console&) = delete;
    Utf8Console& operator=(const Utf8Console&) = delete;

private:
#ifdef _WIN32
    UINT previousCodePage_ = 0;
#endif
};

Args parseArgs(int argc, char** argv) {
    Args args;
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string key = argv[i];
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

std::unique_ptr<ICellUpdater> makeUpdater(int mode, int height) {
    if (mode == 3) return std::make_unique<RowMutexCellUpdater>(height);
    return std::make_unique<UnsafeCellUpdater>();
}

void printUsage() {
    std::cout <<
        "Использование: heatmap --width W --height H --mode 1|2|3 [--threads N]\n"
        "                       (--sources файл | --random N [--seed S] [--max-radius R] [--max-intensity A])\n"
        "                       [--matrix matrix.txt] [--image heatmap.ppm]\n"
        "Режимы: 1 - один поток, 2 - потоки без синхронизации, 3 - потоки с мьютексами по строкам\n";
}

}

int main(int argc, char** argv) {
    Utf8Console console;
    try {
        const Args args = parseArgs(argc, argv);
        if (!args.count("width") || !args.count("height") || !args.count("mode") ||
            (!args.count("sources") && !args.count("random"))) {
            printUsage();
            return 1;
        }

        const int width = int(getInt(args, "width", 0));
        const int height = int(getInt(args, "height", 0));
        const int mode = int(getInt(args, "mode", 1));
        if (mode < 1 || mode > 3) throw std::invalid_argument("Режим должен быть 1, 2 или 3");
        const unsigned hw = std::thread::hardware_concurrency();
        const int threads = mode == 1 ? 1 : int(getInt(args, "threads", hw ? hw : 4));

        const auto programStart = std::chrono::steady_clock::now();
        std::cout << "Время начала:    " << nowString() << '\n';

        std::vector<Source> sources;
        if (args.count("sources")) {
            sources = loadSources(args.at("sources"));
        } else {
            sources = generateRandomSources(int(getInt(args, "random", 0)), width, height,
                                            int(getInt(args, "max-radius", 50)),
                                            getInt(args, "max-intensity", 100),
                                            unsigned(getInt(args, "seed", 42)));
        }

        HeatMap map(width, height);
        auto updater = makeUpdater(mode, height);
        HeatMapGenerator generator(*updater, threads);

        const auto computeStart = std::chrono::steady_clock::now();
        const std::vector<WorkerStats> stats = generator.generate(map, sources);
        const double computeMs = msSince(computeStart);

        saveMatrix(map, getString(args, "matrix", "heatmap.txt"));
        savePpm(map, getString(args, "image", "heatmap.ppm"));

        const double totalMs = msSince(programStart);
        std::cout << "Время окончания: " << nowString() << '\n'
                  << "Общее время:     " << totalMs << " мс (вычисления: " << computeMs << " мс)\n"
                  << "Режим: " << mode << ", потоков: " << threads << ", источников: " << sources.size() << '\n'
                  << "Результат: min=" << map.minIntensity() << " max=" << map.maxIntensity()
                  << " контрольная сумма=" << map.checksum() << '\n';
        for (const WorkerStats& w : stats) {
            std::cout << "  поток " << w.worker << ": источников=" << w.sources << " клеток=" << w.cells
                      << " время=" << w.milliseconds << " мс\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << '\n';
        return 2;
    }
    return 0;
}
