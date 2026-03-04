#pragma once
#include "DataStore.hpp"
#include <string>

struct BenchmarkResult {
    std::string name;
    double      elapsed_ms;
    size_t      result_count;
};

class Benchmark {
public:
    static BenchmarkResult measureLoad(DataStore& ds, const std::string& path);

    static BenchmarkResult measureSearchByZip(const DataStore& ds,
                                               uint32_t zip_min, uint32_t zip_max);

    static BenchmarkResult measureSearchByDate(const DataStore& ds,
                                                uint32_t from, uint32_t to);

    static BenchmarkResult measureSearchByBoundingBox(const DataStore& ds,
                                                       double lat_min, double lat_max,
                                                       double lon_min, double lon_max);
};
