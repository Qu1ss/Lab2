#pragma once
#include <algorithm>
#include <cstddef>
#include <exception>
#include <stdexcept>
#include <vector>
#include "AsyncFileCopier.h"
#include "CopyTypes.h"
#include "SyncFileCopier.h"

class ICopyRunner {
public:
    virtual ~ICopyRunner() = default;
    virtual CopyOutcome run(const std::vector<FileTask>& tasks) = 0;
};

class SyncCopyRunner : public ICopyRunner {
public:
    explicit SyncCopyRunner(SyncFileCopier copier) : copier_(copier) {}

    CopyOutcome run(const std::vector<FileTask>& tasks) override {
        CopyOutcome outcome;
        outcome.results.reserve(tasks.size());
        for (std::size_t i = 0; i < tasks.size(); ++i) {
            outcome.startOrder.push_back(i);
            outcome.results.push_back(copier_.copy(tasks[i]));
            outcome.completionOrder.push_back(i);
        }
        outcome.peakConcurrency = tasks.empty() ? 0 : 1;
        return outcome;
    }

private:
    SyncFileCopier copier_;
};

class AsyncCopyRunner : public ICopyRunner {
public:
    AsyncCopyRunner(AsyncFileCopier copier, std::size_t maxConcurrent)
        : copier_(copier), maxConcurrent_(maxConcurrent) {
        if (maxConcurrent == 0) {
            throw std::invalid_argument("Число одновременно копируемых файлов должно быть >= 1");
        }
    }

    CopyOutcome run(const std::vector<FileTask>& tasks) override {
        CopyOutcome outcome;
        outcome.results.resize(tasks.size());
        State state;

        boost::asio::io_context io;
        const std::size_t workers = std::min(maxConcurrent_, tasks.size());
        for (std::size_t i = 0; i < workers; ++i) {
            boost::asio::co_spawn(io, worker(tasks, outcome, state), [](std::exception_ptr error) {
                if (error) {
                    std::rethrow_exception(error);
                }
            });
        }
        io.run();

        outcome.peakConcurrency = state.peak;
        return outcome;
    }

private:
    struct State {
        std::size_t next = 0;
        std::size_t active = 0;
        std::size_t peak = 0;
    };

    boost::asio::awaitable<void> worker(const std::vector<FileTask>& tasks, CopyOutcome& outcome, State& state) {
        while (state.next < tasks.size()) {
            const std::size_t index = state.next++;
            outcome.startOrder.push_back(index);
            ++state.active;
            state.peak = std::max(state.peak, state.active);

            outcome.results[index] = co_await copier_.copy(tasks[index]);

            --state.active;
            outcome.completionOrder.push_back(index);
        }
    }

    AsyncFileCopier copier_;
    std::size_t maxConcurrent_;
};
