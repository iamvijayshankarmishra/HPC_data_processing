#include "DataStore.hpp"
#include "DateParser.hpp"
#include "../benchmark/Benchmark.hpp"
#include <iostream>
#include <iomanip>

static void printResult(const BenchmarkResult& r) {
    std::cout << std::left  << std::setw(28) << ("[" + r.name + "]")
              << std::right << std::setw(10) << r.result_count << " results   "
              << std::fixed << std::setprecision(2) << std::setw(10) << r.elapsed_ms
              << " ms\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_csv>\n";
        return 1;
    }

    DataStore ds;

    // ── load ──────────────────────────────────────────────────────────────
    auto loadResult = Benchmark::measureLoad(ds, argv[1]);
    printResult(loadResult);

    // ── searchByZip: zip range 10001–10099 (Manhattan) ────────────────────
    auto zipResult = Benchmark::measureSearchByZip(ds, 10001, 10099);
    printResult(zipResult);

    // ── searchByDate: year 2023 ───────────────────────────────────────────
    uint32_t from = toEpoch(2023, 1,  1,  0,  0,  0);
    uint32_t to   = toEpoch(2023, 12, 31, 23, 59, 59);
    auto dateResult = Benchmark::measureSearchByDate(ds, from, to);
    printResult(dateResult);

    // ── searchByBoundingBox: roughly Brooklyn ─────────────────────────────
    auto bboxResult = Benchmark::measureSearchByBoundingBox(
        ds,
        40.57, 40.74,   // lat_min, lat_max
       -74.04, -73.83   // lon_min, lon_max
    );
    printResult(bboxResult);

    return 0;
}
