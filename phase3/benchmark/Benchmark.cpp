#include "Benchmark.hpp"
#include <chrono>

BenchmarkResult Benchmark::measureLoad(SoADataStore& ds,
                                        const std::string& path,
                                        int num_threads) {
    auto t0 = std::chrono::steady_clock::now();
    ds.load(path, num_threads);
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return {"load", ms, ds.size(), num_threads};
}

BenchmarkResult Benchmark::measureSearchByZip(const SoADataStore& ds,
                                               uint32_t zip_min, uint32_t zip_max,
                                               int num_threads) {
    auto t0 = std::chrono::steady_clock::now();
    auto result = ds.searchByZip(zip_min, zip_max, num_threads);
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return {"searchByZip", ms, result.size(), num_threads};
}

BenchmarkResult Benchmark::measureSearchByDate(const SoADataStore& ds,
                                                uint32_t from, uint32_t to,
                                                int num_threads) {
    auto t0 = std::chrono::steady_clock::now();
    auto result = ds.searchByDate(from, to, num_threads);
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return {"searchByDate", ms, result.size(), num_threads};
}

BenchmarkResult Benchmark::measureSearchByBoundingBox(const SoADataStore& ds,
                                                       double lat_min, double lat_max,
                                                       double lon_min, double lon_max,
                                                       int num_threads) {
    auto t0 = std::chrono::steady_clock::now();
    auto result = ds.searchByBoundingBox(lat_min, lat_max, lon_min, lon_max, num_threads);
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return {"searchByBoundingBox", ms, result.size(), num_threads};
}
