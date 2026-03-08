#pragma once
#include "../include/SoADataStore.hpp"
#include <string>
#include <cstdint>

struct BenchmarkResult {
    std::string name;
    double elapsed_ms;
    size_t result_count;
    int num_threads;
};

struct Benchmark {
    static BenchmarkResult measureLoad(SoADataStore& ds,
                                       const std::string& path, int num_threads);

    static BenchmarkResult measureSearchByZip(const SoADataStore& ds,
                                              uint32_t zip_min, uint32_t zip_max,
                                              int num_threads);

    static BenchmarkResult measureSearchByDate(const SoADataStore& ds,
                                               uint32_t from, uint32_t to,
                                               int num_threads);

    static BenchmarkResult measureSearchByBoundingBox(const SoADataStore& ds,
                                                      double lat_min, double lat_max,
                                                      double lon_min, double lon_max,
                                                      int num_threads);
};
