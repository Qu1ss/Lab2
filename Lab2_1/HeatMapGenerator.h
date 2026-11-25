#pragma once
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <thread>
#include <vector>
#include "CellUpdater.h"
#include "HeatMap.h"
#include "Source.h"

struct WorkerStats {
    int worker = 0;
    std::size_t sources = 0;
    std::int64_t cells = 0;
    double milliseconds = 0.0;
};

inline std::int64_t applySource(HeatMap& map, ICellUpdater& updater, const Source& s) {
    std::int64_t touched = 0;
    const std::int64_t yFrom = std::max<std::int64_t>(0, std::int64_t(s.y) - s.radius);
    const std::int64_t yTo = std::min<std::int64_t>(map.height() - 1, std::int64_t(s.y) + s.radius);

    for (std::int64_t y = yFrom; y <= yTo; ++y) {
        const std::int64_t span = s.radius - std::abs(y - s.y);
        const std::int64_t xFrom = std::max<std::int64_t>(0, std::int64_t(s.x) - span);
        const std::int64_t xTo = std::min<std::int64_t>(map.width() - 1, std::int64_t(s.x) + span);
        for (std::int64_t x = xFrom; x <= xTo; ++x) {
            const std::int64_t value = s.contributionAt(int(x), int(y));
            if (value > 0) {
                updater.add(map, int(x), int(y), value);
                ++touched;
            }
        }
    }
    return touched;
}

class HeatMapGenerator {
public:
    HeatMapGenerator(ICellUpdater& updater, int threadCount)
        : updater_(updater), threadCount_(threadCount) {
        if (threadCount < 1) {
            throw std::invalid_argument("Число потоков должно быть >= 1");
        }
    }

    std::vector<WorkerStats> generate(HeatMap& map, const std::vector<Source>& sources) {
        for (const Source& s : sources) {
            validate(s);
        }

        std::vector<WorkerStats> stats(threadCount_);
        if (threadCount_ == 1) {
            runWorker(0, map, sources, stats[0]);
            return stats;
        }

        std::vector<std::exception_ptr> errors(threadCount_);
        std::vector<std::thread> threads;
        for (int i = 0; i < threadCount_; ++i) {
            threads.emplace_back([&, i] {
                try {
                    runWorker(i, map, sources, stats[i]);
                } catch (...) {
                    errors[i] = std::current_exception();
                }
            });
        }
        for (std::thread& t : threads) {
            t.join();
        }
        for (const std::exception_ptr& e : errors) {
            if (e) std::rethrow_exception(e);
        }
        return stats;
    }

private:
    void runWorker(int index, HeatMap& map, const std::vector<Source>& sources, WorkerStats& out) {
        const auto start = std::chrono::steady_clock::now();
        out.worker = index;
        for (std::size_t i = index; i < sources.size(); i += threadCount_) {
            out.cells += applySource(map, updater_, sources[i]);
            ++out.sources;
        }
        const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
        out.milliseconds = elapsed.count();
    }

    ICellUpdater& updater_;
    int threadCount_;
};
