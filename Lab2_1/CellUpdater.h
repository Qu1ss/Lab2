#pragma once
#include <cstdint>
#include <mutex>
#include <vector>
#include "HeatMap.h"

class ICellUpdater {
public:
    virtual ~ICellUpdater() = default;
    virtual void add(HeatMap& map, int x, int y, std::int64_t value) = 0;
};

class UnsafeCellUpdater : public ICellUpdater {
public:
    void add(HeatMap& map, int x, int y, std::int64_t value) override {
        map.add(x, y, value);
    }
};

class RowMutexCellUpdater : public ICellUpdater {
public:
    explicit RowMutexCellUpdater(int height) : rowLocks_(height) {}

    void add(HeatMap& map, int x, int y, std::int64_t value) override {
        std::lock_guard<std::mutex> lock(rowLocks_.at(y));
        map.add(x, y, value);
    }

private:
    std::vector<std::mutex> rowLocks_;
};
