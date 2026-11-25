#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include "CellUpdater.h"
#include "HeatMap.h"
#include "HeatMapGenerator.h"
#include "HeatMapIO.h"

static int failed = 0;

#define CHECK(cond) do { if (!(cond)) { ++failed; std::cout << "FAILED: " #cond " (line " << __LINE__ << ")\n"; } } while (0)

template <class Ex, class F>
bool throws(F f) {
    try { f(); } catch (const Ex&) { return true; } catch (...) { return false; }
    return false;
}

void testContribution() {
    Source s{2, 2, 2, 3};
    CHECK(s.contributionAt(2, 2) == 9);
    CHECK(s.contributionAt(3, 2) == 6);
    CHECK(s.contributionAt(3, 3) == 3);
    CHECK(s.contributionAt(4, 3) == 0);
}

void testSingleSourceOnMap() {
    HeatMap map(5, 5);
    UnsafeCellUpdater updater;
    HeatMapGenerator gen(updater, 1);
    gen.generate(map, {Source{2, 2, 2, 3}});
    CHECK(map.at(2, 2) == 9);
    CHECK(map.at(2, 1) == 6);
    CHECK(map.at(0, 0) == 0);
    CHECK(map.minIntensity() == 0);
    CHECK(map.maxIntensity() == 9);
}

void testSourcesOverlapAndSum() {
    HeatMap map(3, 1);
    UnsafeCellUpdater updater;
    HeatMapGenerator gen(updater, 1);
    gen.generate(map, {Source{0, 0, 1, 10}, Source{2, 0, 1, 10}});
    CHECK(map.at(0, 0) == 20);
    CHECK(map.at(1, 0) == 10 + 10);
    CHECK(map.at(2, 0) == 20);
}

void testSourceOutsideMap() {
    HeatMap map(3, 3);
    UnsafeCellUpdater updater;
    HeatMapGenerator gen(updater, 1);
    gen.generate(map, {Source{-1, -1, 2, 1}, Source{100, 100, 5, 1}});
    CHECK(map.at(0, 0) == 1);
    CHECK(map.at(1, 0) == 0);
    CHECK(map.maxIntensity() == 1);
}

void testZeroIntensityAndRadius() {
    HeatMap map(3, 3);
    UnsafeCellUpdater updater;
    HeatMapGenerator gen(updater, 1);
    gen.generate(map, {Source{1, 1, 0, 5}, Source{0, 0, 3, 0}});
    CHECK(map.at(1, 1) == 5);
    CHECK(map.at(0, 0) == 0);
    CHECK(map.maxIntensity() == 5);
}

void testOverflow() {
    const std::int64_t big = std::numeric_limits<std::int64_t>::max() / 2 + 1;
    {
        HeatMap map(1, 1);
        UnsafeCellUpdater updater;
        HeatMapGenerator gen(updater, 1);
        CHECK(throws<std::overflow_error>([&] { gen.generate(map, {Source{0, 0, 0, big}, Source{0, 0, 0, big}}); }));
    }
    {
        HeatMap map(2, 1);
        UnsafeCellUpdater updater;
        HeatMapGenerator gen(updater, 1);
        CHECK(throws<std::overflow_error>([&] { gen.generate(map, {Source{0, 0, 5, big}}); }));
    }
    {
        HeatMap map(1, 1);
        RowMutexCellUpdater updater(1);
        HeatMapGenerator gen(updater, 2);
        CHECK(throws<std::overflow_error>([&] { gen.generate(map, {Source{0, 0, 0, big}, Source{0, 0, 0, big}}); }));
    }
}

void testInvalidInput() {
    CHECK(throws<std::invalid_argument>([] { HeatMap(0, 5); }));
    CHECK(throws<std::invalid_argument>([] { HeatMap(5, -1); }));
    HeatMap map(2, 2);
    CHECK(throws<std::out_of_range>([&] { map.at(2, 0); }));
    UnsafeCellUpdater updater;
    CHECK(throws<std::invalid_argument>([&] { HeatMapGenerator(updater, 0); }));
    HeatMapGenerator gen(updater, 1);
    CHECK(throws<std::invalid_argument>([&] { gen.generate(map, {Source{0, 0, -1, 1}}); }));
    CHECK(throws<std::invalid_argument>([&] { gen.generate(map, {Source{0, 0, 1, -1}}); }));
}

void testThreadedModesMatchSingleThread() {
    const auto sources = generateRandomSources(300, 60, 40, 15, 100, 7);

    HeatMap reference(60, 40);
    UnsafeCellUpdater single;
    HeatMapGenerator(single, 1).generate(reference, sources);

    for (int threads : {2, 4, 8}) {
        HeatMap map(60, 40);
        RowMutexCellUpdater locked(40);
        HeatMapGenerator(locked, threads).generate(map, sources);
        CHECK(map.checksum() == reference.checksum());
    }

    const auto again = generateRandomSources(300, 60, 40, 15, 100, 7);
    CHECK(again.size() == sources.size() && again[10].x == sources[10].x && again[10].intensity == sources[10].intensity);
}

void testColor() {
    CHECK(intensityToRed(0, 0) == 0);
    CHECK(intensityToRed(10, 10) == 255);
    CHECK(intensityToRed(5, 10) == 127);
    CHECK(intensityToRed(0, 10) == 0);
    const std::int64_t huge = std::numeric_limits<std::int64_t>::max();
    CHECK(intensityToRed(huge, huge) == 255);
}

int main() {
    testContribution();
    testSingleSourceOnMap();
    testSourcesOverlapAndSum();
    testSourceOutsideMap();
    testZeroIntensityAndRadius();
    testOverflow();
    testInvalidInput();
    testThreadedModesMatchSingleThread();
    testColor();
    std::cout << (failed == 0 ? "All tests passed\n" : "Some tests FAILED\n");
    return failed == 0 ? 0 : 1;
}
