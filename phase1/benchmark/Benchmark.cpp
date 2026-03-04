#include "Benchmark.hpp"
#include <chrono>

BenchmarkResult Benchmark::measureLoad(DataStore& ds, const std::string& path) {
    auto t0 = std::chrono::steady_clock::now();
    ds.load(path);
    auto t1 = std::chrono::steady_clock::now();

    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return {"load", ms, ds.size()};
}

BenchmarkResult Benchmark::measureSearchByZip(const DataStore& ds,
                                               uint32_t zip_min, uint32_t zip_max) {
    auto t0 = std::chrono::steady_clock::now();
    auto result = ds.searchByZip(zip_min, zip_max);
    auto t1 = std::chrono::steady_clock::now();

    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return {"searchByZip", ms, result.size()};
}

BenchmarkResult Benchmark::measureSearchByDate(const DataStore& ds,
                                                uint32_t from, uint32_t to) {
    auto t0 = std::chrono::steady_clock::now();
    auto result = ds.searchByDate(from, to);
    auto t1 = std::chrono::steady_clock::now();

    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return {"searchByDate", ms, result.size()};
}

BenchmarkResult Benchmark::measureSearchByBoundingBox(const DataStore& ds,
                                                       double lat_min, double lat_max,
                                                       double lon_min, double lon_max) {
    auto t0 = std::chrono::steady_clock::now();
    auto result = ds.searchByBoundingBox(lat_min, lat_max, lon_min, lon_max);
    auto t1 = std::chrono::steady_clock::now();

    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return {"searchByBoundingBox", ms, result.size()};
}
