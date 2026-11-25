#include "CopyRunners.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>
#include "CopyReport.h"
#include "DirectoryScanner.h"
#include "FileComparer.h"

namespace fs = std::filesystem;

static int failed = 0;

#define CHECK(cond) do { if (!(cond)) { ++failed; std::cout << "FAILED: " #cond " (line " << __LINE__ << ")\n"; } } while (0)

template <class Ex, class F>
bool throws(F f) {
    try { f(); } catch (const Ex&) { return true; } catch (...) { return false; }
    return false;
}

class TestDir {
public:
    TestDir() : root_(fs::temp_directory_path() / "copy_lab_tests") {
        std::error_code ec;
        fs::remove_all(root_, ec);
        fs::create_directories(root_);
    }
    ~TestDir() {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    const fs::path& root() const { return root_; }

private:
    fs::path root_;
};

void makeFile(const fs::path& path, std::size_t size, int seed) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    for (std::size_t i = 0; i < size; ++i) {
        out.put(static_cast<char>((seed * 31 + static_cast<int>(i) * 7) & 0xFF));
    }
}

std::uintmax_t sizeOf(const fs::path& path) {
    std::error_code ec;
    return fs::file_size(path, ec);
}

fs::path buildSourceTree(const fs::path& src) {
    makeFile(src / "a.txt", 1000, 1);
    makeFile(src / "empty.bin", 0, 2);
    makeFile(src / "sub" / "b.bin", 70000, 3);
    makeFile(src / "sub" / "deep" / "c.txt", 5, 4);
    makeFile(src / fs::path(u8"тест.txt"), 333, 5);
    fs::create_directories(src / "sub" / "emptydir");
    for (int i = 0; i < 20; ++i) {
        makeFile(src / "many" / ("f" + std::to_string(i) + ".dat"), static_cast<std::size_t>(i) * 1237, i);
    }
    return src;
}

CopyOutcome copyAll(ICopyRunner& runner, const ScanResult& scan, const fs::path& target, std::size_t blockSize) {
    prepareTargetDirectories(target, scan.directories);
    CopyOutcome outcome = runner.run(scan.files);
    verifyResults(scan.files, outcome.results, blockSize);
    return outcome;
}

void checkAllCopied(const ScanResult& scan, const CopyOutcome& outcome, const fs::path& target) {
    CHECK(outcome.results.size() == scan.files.size());
    for (std::size_t i = 0; i < outcome.results.size(); ++i) {
        const FileCopyResult& r = outcome.results[i];
        CHECK(r.success);
        CHECK(r.verified);
        CHECK(r.bytesCopied == sizeOf(scan.files[i].source));
        CHECK(filesEqual(scan.files[i].source, scan.files[i].target));
    }
    CHECK(fs::is_directory(target / "sub" / "emptydir"));
    CHECK(fs::exists(target / "empty.bin") && sizeOf(target / "empty.bin") == 0);
    CHECK(fs::exists(target / fs::path(u8"тест.txt")));
}

void checkOrders(const CopyOutcome& outcome, std::size_t n) {
    std::vector<std::size_t> started = outcome.startOrder;
    std::vector<std::size_t> completed = outcome.completionOrder;
    std::sort(started.begin(), started.end());
    std::sort(completed.begin(), completed.end());
    std::vector<std::size_t> expected(n);
    for (std::size_t i = 0; i < n; ++i) expected[i] = i;
    CHECK(started == expected);
    CHECK(completed == expected);
}

void testScanner() {
    TestDir dir;
    const fs::path src = buildSourceTree(dir.root() / "src");
    bool linkCreated = true;
    try {
        fs::create_symlink(src / "a.txt", src / "link.txt");
    } catch (const std::exception&) {
        linkCreated = false;
    }

    DirectoryScanner scanner(src, dir.root() / "dst");
    const ScanResult scan = scanner.scan();
    CHECK(scan.files.size() == 25);
    CHECK(std::find(scan.directories.begin(), scan.directories.end(), fs::path("sub") / "emptydir") != scan.directories.end());
    CHECK(std::is_sorted(scan.files.begin(), scan.files.end(),
                         [](const FileTask& a, const FileTask& b) { return a.relative < b.relative; }));
    CHECK(scan.files.front().target == dir.root() / "dst" / scan.files.front().relative);
    if (linkCreated) {
        CHECK(scan.skipped.size() == 1);
        CHECK(std::none_of(scan.files.begin(), scan.files.end(),
                           [](const FileTask& t) { return t.relative == fs::path("link.txt"); }));
    }
}

void testSelfCopyForbidden() {
    TestDir dir;
    const fs::path src = buildSourceTree(dir.root() / "src");
    CHECK(throws<std::invalid_argument>([&] { DirectoryScanner(src, src); }));
    CHECK(throws<std::invalid_argument>([&] { DirectoryScanner(src, src / "inner"); }));
    CHECK(throws<std::invalid_argument>([&] { DirectoryScanner(src, src / "inner" / ""); }));
    CHECK(throws<std::invalid_argument>([&] { DirectoryScanner(src, src / "sub" / ".." / "x"); }));
    CHECK(!throws<std::invalid_argument>([&] { DirectoryScanner(src, dir.root() / "src_copy"); }));
    CHECK(throws<std::invalid_argument>([&] { DirectoryScanner(dir.root() / "missing", dir.root() / "dst"); }));
}

void testPrepareDirectories() {
    TestDir dir;
    const fs::path target = dir.root() / "dst";
    CHECK(prepareTargetDirectories(target, {fs::path("a") / "b", fs::path("c")}).empty());
    CHECK(fs::is_directory(target / "a" / "b"));
    CHECK(fs::is_directory(target / "c"));

    makeFile(dir.root() / "blocked" / "sub", 1, 1);
    CHECK(prepareTargetDirectories(dir.root() / "blocked", {fs::path("sub")}).size() == 1);
}

void testSyncCopy() {
    TestDir dir;
    const fs::path src = buildSourceTree(dir.root() / "src");
    const fs::path dst = dir.root() / "dst";
    const ScanResult scan = DirectoryScanner(src, dst).scan();
    for (std::size_t block : {std::size_t(1), std::size_t(7), std::size_t(4096), std::size_t(1) << 20}) {
        std::error_code ec;
        fs::remove_all(dst, ec);
        SyncCopyRunner runner{SyncFileCopier(block)};
        const CopyOutcome outcome = copyAll(runner, scan, dst, block);
        checkAllCopied(scan, outcome, dst);
        checkOrders(outcome, scan.files.size());
        CHECK(outcome.peakConcurrency == 1);
    }
}

void testAsyncCopy() {
    TestDir dir;
    const fs::path src = buildSourceTree(dir.root() / "src");
    const fs::path dst = dir.root() / "dst";
    const ScanResult scan = DirectoryScanner(src, dst).scan();
    for (std::size_t block : {std::size_t(7), std::size_t(4096), std::size_t(1) << 20}) {
        std::error_code ec;
        fs::remove_all(dst, ec);
        AsyncCopyRunner runner{AsyncFileCopier(block), 5};
        const CopyOutcome outcome = copyAll(runner, scan, dst, block);
        checkAllCopied(scan, outcome, dst);
        checkOrders(outcome, scan.files.size());
        CHECK(outcome.peakConcurrency <= 5);
        CHECK(outcome.peakConcurrency > 1);
    }
}

void testConcurrencyLimit() {
    TestDir dir;
    const fs::path src = buildSourceTree(dir.root() / "src");
    const ScanResult scan = DirectoryScanner(src, dir.root() / "dst").scan();
    for (std::size_t limit : {std::size_t(1), std::size_t(2), std::size_t(3), std::size_t(100)}) {
        std::error_code ec;
        fs::remove_all(dir.root() / "dst", ec);
        AsyncCopyRunner runner{AsyncFileCopier(512), limit};
        const CopyOutcome outcome = copyAll(runner, scan, dir.root() / "dst", 512);
        CHECK(outcome.peakConcurrency <= limit);
        CHECK(outcome.peakConcurrency <= scan.files.size());
        if (limit > 1) CHECK(outcome.peakConcurrency > 1);
        if (limit == 1) CHECK(outcome.peakConcurrency == 1);
    }
}

void testSyncAndAsyncGiveSameResults() {
    TestDir dir;
    const fs::path src = buildSourceTree(dir.root() / "src");
    const ScanResult scanA = DirectoryScanner(src, dir.root() / "dstA").scan();
    const ScanResult scanB = DirectoryScanner(src, dir.root() / "dstB").scan();
    SyncCopyRunner syncRunner{SyncFileCopier(100)};
    AsyncCopyRunner asyncRunner{AsyncFileCopier(100), 4};
    const CopyOutcome a = copyAll(syncRunner, scanA, dir.root() / "dstA", 100);
    const CopyOutcome b = copyAll(asyncRunner, scanB, dir.root() / "dstB", 100);
    CHECK(a.results.size() == b.results.size());
    for (std::size_t i = 0; i < a.results.size(); ++i) {
        CHECK(a.results[i].relative == b.results[i].relative);
        CHECK(a.results[i].bytesCopied == b.results[i].bytesCopied);
        CHECK(a.results[i].success == b.results[i].success);
    }
}

void testErrorsDoNotAffectOtherFiles(ICopyRunner& runner, const fs::path& root) {
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path src = buildSourceTree(root / "src");
    const fs::path dst = root / "dst";
    const ScanResult scan = DirectoryScanner(src, dst).scan();
    fs::remove(src / "a.txt");
    fs::remove(src / "sub" / "b.bin");

    const CopyOutcome outcome = copyAll(runner, scan, dst, 4096);
    CHECK(countFailed(outcome.results) == 2);
    CHECK(countSucceeded(outcome.results) == scan.files.size() - 2);
    for (std::size_t i = 0; i < outcome.results.size(); ++i) {
        const bool missing = scan.files[i].relative == fs::path("a.txt") || scan.files[i].relative == fs::path("sub") / "b.bin";
        CHECK(outcome.results[i].success == !missing);
        if (missing) CHECK(!outcome.results[i].error.empty());
    }
    checkOrders(outcome, scan.files.size());
}

void testErrors() {
    TestDir dir;
    SyncCopyRunner syncRunner{SyncFileCopier(4096)};
    AsyncCopyRunner asyncRunner{AsyncFileCopier(4096), 3};
    testErrorsDoNotAffectOtherFiles(syncRunner, dir.root() / "sync");
    testErrorsDoNotAffectOtherFiles(asyncRunner, dir.root() / "async");
}

void testTargetDirectoryBlocked() {
    TestDir dir;
    const fs::path src = buildSourceTree(dir.root() / "src");
    const fs::path dst = dir.root() / "dst";
    const ScanResult scan = DirectoryScanner(src, dst).scan();
    makeFile(dst / "sub", 1, 1);
    const std::vector<std::string> errors = prepareTargetDirectories(dst, scan.directories);
    CHECK(!errors.empty());
    AsyncCopyRunner runner{AsyncFileCopier(4096), 4};
    CopyOutcome outcome = runner.run(scan.files);
    for (std::size_t i = 0; i < outcome.results.size(); ++i) {
        const bool underSub = scan.files[i].relative.begin()->string() == "sub";
        CHECK(outcome.results[i].success == !underSub);
    }
}

void testVerification() {
    TestDir dir;
    makeFile(dir.root() / "x.bin", 100000, 1);
    makeFile(dir.root() / "y.bin", 100000, 1);
    CHECK(filesEqual(dir.root() / "x.bin", dir.root() / "y.bin", 4096));
    CHECK(filesEqual(dir.root() / "x.bin", dir.root() / "y.bin", 100000));
    {
        std::fstream f(dir.root() / "y.bin", std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(99999);
        f.put('Z');
    }
    CHECK(!filesEqual(dir.root() / "x.bin", dir.root() / "y.bin", 4096));
    makeFile(dir.root() / "short.bin", 99999, 1);
    CHECK(!filesEqual(dir.root() / "x.bin", dir.root() / "short.bin", 4096));
    CHECK(!filesEqual(dir.root() / "x.bin", dir.root() / "missing.bin"));

    FileTask task{dir.root() / "x.bin", dir.root() / "y.bin", "x.bin"};
    std::vector<FileCopyResult> results{succeededResult(task, 100000)};
    verifyResults({task}, results, 4096);
    CHECK(!results[0].success && !results[0].verified);
}

void testInvalidParameters() {
    CHECK(throws<std::invalid_argument>([] { SyncFileCopier(0); }));
    CHECK(throws<std::invalid_argument>([] { AsyncFileCopier(0); }));
    CHECK(throws<std::invalid_argument>([] { AsyncCopyRunner(AsyncFileCopier(10), 0); }));
}

void testEmptyTaskList() {
    SyncCopyRunner syncRunner{SyncFileCopier(10)};
    AsyncCopyRunner asyncRunner{AsyncFileCopier(10), 3};
    CHECK(syncRunner.run({}).results.empty());
    const CopyOutcome outcome = asyncRunner.run({});
    CHECK(outcome.results.empty() && outcome.completionOrder.empty() && outcome.peakConcurrency == 0);
}

void testReportAndUtf8() {
    CHECK(toUtf8(fs::path(u8"тест.txt")) == "\xD1\x82\xD0\xB5\xD1\x81\xD1\x82.txt");

    CopySummary summary;
    summary.modeName = "async";
    summary.outcome.results.push_back(succeededResult(FileTask{"s", "t", "a.txt"}, 10));
    summary.outcome.results.push_back(failedResult(FileTask{"s", "t", "b.txt"}, 0, "ошибка чтения"));
    summary.outcome.startOrder = {0, 1};
    summary.outcome.completionOrder = {1, 0};
    const std::string text = formatReport(summary);
    CHECK(text.find("a.txt") != std::string::npos);
    CHECK(text.find("успешно 1, с ошибками 1") != std::string::npos);
    CHECK(text.find("Порядок завершения:\n  1. b.txt\n  2. a.txt") != std::string::npos);

    TestDir dir;
    saveReport(dir.root() / "r.txt", text);
    CHECK(sizeOf(dir.root() / "r.txt") == text.size());
    CHECK(throws<std::runtime_error>([&] { saveReport(dir.root() / "no_dir" / "r.txt", text); }));
}

int main() {
    testScanner();
    testSelfCopyForbidden();
    testPrepareDirectories();
    testSyncCopy();
    testAsyncCopy();
    testConcurrencyLimit();
    testSyncAndAsyncGiveSameResults();
    testErrors();
    testTargetDirectoryBlocked();
    testVerification();
    testInvalidParameters();
    testEmptyTaskList();
    testReportAndUtf8();
    std::cout << (failed == 0 ? "All tests passed\n" : "Some tests FAILED\n");
    return failed == 0 ? 0 : 1;
}
