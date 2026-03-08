#include "../include/SoADataStore.hpp"
#include "../benchmark/Benchmark.hpp"
#include "DateParser.hpp"
#include <iostream>
#include <iomanip>

static void printResult(const BenchmarkResult& r) {
    std::string tag = "[" + r.name + "|T=" + std::to_string(r.num_threads) + "]";
    std::cout << std::left  << std::setw(32) << tag
              << std::right << std::setw(10) << r.result_count << " results   "
              << std::fixed << std::setprecision(2) << std::setw(10) << r.elapsed_ms
              << " ms\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_csv>\n";
        return 1;
    }

    const char* path = argv[1];
    const int LOAD_THREADS   = 4;
    const int SEARCH_THREADS = 8;

    uint32_t from = toEpoch(2023,  1,  1,  0,  0,  0);
    uint32_t to   = toEpoch(2023, 12, 31, 23, 59, 59);

    SoADataStore ds;

    auto loadResult = Benchmark::measureLoad(ds, path, LOAD_THREADS);
    printResult(loadResult);

    auto zipResult  = Benchmark::measureSearchByZip(ds, 10001, 10099, SEARCH_THREADS);
    printResult(zipResult);

    auto dateResult = Benchmark::measureSearchByDate(ds, from, to, SEARCH_THREADS);
    printResult(dateResult);

    auto bboxResult = Benchmark::measureSearchByBoundingBox(
        ds, 40.57, 40.74, -74.04, -73.83, SEARCH_THREADS);
    printResult(bboxResult);

    return 0;
}
