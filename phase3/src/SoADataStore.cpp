#include "../include/SoADataStore.hpp"
#include "DateParser.hpp"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <unordered_map>
#include <omp.h>

static const int MAX_FIELDS = 50;
static const uint32_t POOL_BYTES = 3500u * 1024u * 1024u;  // 3.5 GB, fits in uint32_t
static const uint32_t RES_POOL_BYTES = 3200u * 1024u * 1024u; // 3.2 GB for resolution_desc

SoADataStore::SoADataStore(): pool_(POOL_BYTES), res_pool_(RES_POOL_BYTES) {}


static uint32_t parseUint32(std::string_view sv) {
    if (sv.empty()) return 0;
    char buf[32];
    size_t n = std::min(sv.size(), sizeof(buf)-1);
    std::memcpy(buf, sv.data(), n);
    buf[n] = '\0';
    return (uint32_t)std::strtoul(buf, nullptr, 10);
}

static uint64_t parseUint64(std::string_view sv) {
    if (sv.empty()) return 0;
    char buf[32];
    size_t n = std::min(sv.size(), sizeof(buf)-1);
    std::memcpy(buf, sv.data(), n);
    buf[n] = '\0';
    return (uint64_t)std::strtoull(buf, nullptr, 10);
}

static double parseDouble(std::string_view sv) {
    if (sv.empty()) return 0.0;
    char buf[64];
    size_t n = std::min(sv.size(), sizeof(buf)-1);
    std::memcpy(buf, sv.data(), n);
    buf[n] = '\0';
    return std::strtod(buf, nullptr);
}

static uint32_t parseDateField(std::string_view sv) {
    if (sv.empty()) return 0;
    return parseDateTime(sv.data(), (int)sv.size());
}


void SoADataStore::load(const std::string& path, int num_threads) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) { perror("open"); return; }

    struct stat st;
    fstat(fd, &st);
    size_t file_size = (size_t)st.st_size;

    const char* mapped = (const char*)mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (mapped == MAP_FAILED) { perror("mmap"); return; }

#ifdef MADV_SEQUENTIAL
    madvise((void*)mapped, file_size, MADV_SEQUENTIAL);
#endif

    // skip header
    const char* body = (const char*)memchr(mapped, '\n', file_size);
    if (!body) { munmap((void*)mapped, file_size); return; }
    ++body;
    size_t body_len = file_size - (size_t)(body - mapped);

    // compute thread boundaries, snapped to newline
    std::vector<size_t> starts(num_threads + 1);
    starts[0] = 0;
    starts[num_threads] = body_len;
    for (int i = 1; i < num_threads; ++i) {
        size_t raw = (size_t)i * body_len / num_threads;
        const char* p = (const char*)memchr(body + raw, '\n', body_len - raw);
        starts[i] = p ? (size_t)(p - body) + 1 : body_len;
    }

    // per-thread local accumulators — single pass, no pre-sizing needed
    std::vector<std::vector<ServiceRequest>> t_records(num_threads);
    std::vector<std::vector<uint32_t>>       t_zip(num_threads);
    std::vector<std::vector<uint32_t>>       t_created(num_threads);
    std::vector<std::vector<float>>          t_lat(num_threads);
    std::vector<std::vector<float>>          t_lon(num_threads);

    #pragma omp parallel num_threads(num_threads)
    {
        int tid = omp_get_thread_num();
        size_t s = starts[tid], e = starts[tid + 1];

        std::unordered_map<std::string, uint8_t>  cache_agency, cache_borough,
                                                   cache_loc_type, cache_status,
                                                   cache_channel, cache_park_borough,
                                                   cache_addr_type;
        std::unordered_map<std::string, uint16_t> cache_problem, cache_problem_detail;

        auto enc8 = [&](StringRegistry<uint8_t>& reg,
                        std::unordered_map<std::string, uint8_t>& cache,
                        std::string_view sv) -> uint8_t {
            if (sv.empty()) return 0;
            std::string key(sv);
            auto it = cache.find(key);
            if (it != cache.end()) return it->second;
            uint8_t id = reg.encode(sv);
            cache[key] = id;
            return id;
        };

        auto enc16 = [&](StringRegistry<uint16_t>& reg,
                         std::unordered_map<std::string, uint16_t>& cache,
                         std::string_view sv) -> uint16_t {
            if (sv.empty()) return 0;
            std::string key(sv);
            auto it = cache.find(key);
            if (it != cache.end()) return it->second;
            uint16_t id = reg.encode(sv);
            cache[key] = id;
            return id;
        };

        if (s < e) {
            const char* p       = body + s;
            const char* end_ptr = body + e;
            std::string_view fields[MAX_FIELDS];

            while (p < end_ptr) {
                const char* nl = (const char*)memchr(p, '\n', (size_t)(end_ptr - p));
                size_t line_len = nl ? (size_t)(nl - p) : (size_t)(end_ptr - p);

                if (line_len > 0 && p[line_len - 1] == '\r') --line_len;

                if (line_len > 0) {
                    int nf = CSVParser::parseLine(p, (int)line_len, fields, MAX_FIELDS);

                    auto field = [&](int i) -> std::string_view {
                        return (i < nf) ? fields[i] : std::string_view{};
                    };
                    auto storeStr = [&](int i) -> StringRef {
                        auto sv = field(i);
                        if (sv.empty()) return {0, 0};
                        return pool_.store(sv.data(), (uint16_t)sv.size());
                    };

                    ServiceRequest r{};
                    r.unique_key                 = parseUint64(field(0));
                    r.time.created               = parseDateField(field(1));
                    r.time.closed                = parseDateField(field(2));
                    r.service.agency             = enc8(agency_reg_,          cache_agency,          field(3));
                    r.service.agency_name        = storeStr(4);
                    r.service.problem            = enc16(problem_reg_,        cache_problem,         field(5));
                    r.service.problem_detail     = enc16(problem_detail_reg_, cache_problem_detail,  field(6));
                    r.service.additional_details = storeStr(7);
                    r.service.location_type      = enc8(location_type_reg_,   cache_loc_type,        field(8));
                    r.location.zip               = parseUint32(field(9));
                    r.location.address           = storeStr(10);
                    r.location.street_name       = storeStr(11);
                    r.location.cross_street_1    = storeStr(12);
                    r.location.cross_street_2    = storeStr(13);
                    r.location.intersection_1    = storeStr(14);
                    r.location.intersection_2    = storeStr(15);
                    r.location.address_type      = enc8(address_type_reg_,    cache_addr_type,       field(16));
                    r.location.city              = storeStr(17);
                    r.location.landmark          = storeStr(18);
                    r.service.facility_type      = storeStr(19);
                    r.service.status             = enc8(status_reg_,          cache_status,          field(20));
                    r.time.due                   = parseDateField(field(21));
                    {
                        auto sv = field(22);
                        if (!sv.empty())
                            r.service.resolution_desc = res_pool_.store(sv.data(), (uint16_t)sv.size());
                    }
                    r.time.resolution            = parseDateField(field(23));
                    r.admin.community_board      = (uint16_t)parseUint32(field(24));
                    r.admin.council_district     = (uint16_t)parseUint32(field(25));
                    r.admin.police_precinct      = (uint16_t)parseUint32(field(26));
                    r.admin.bbl                  = parseUint64(field(27));
                    r.location.borough           = enc8(borough_reg_,         cache_borough,         field(28));
                    r.location.x_coord           = (int32_t)parseUint32(field(29));
                    r.location.y_coord           = (int32_t)parseUint32(field(30));
                    r.admin.channel_type         = enc8(channel_type_reg_,    cache_channel,         field(31));
                    r.admin.park_facility        = storeStr(32);
                    r.admin.park_borough         = enc8(park_borough_reg_,    cache_park_borough,    field(33));
                    r.misc.vehicle_type          = storeStr(34);
                    r.misc.taxi_borough          = storeStr(35);
                    r.misc.taxi_pickup           = storeStr(36);
                    r.misc.bridge_name           = storeStr(37);
                    r.misc.bridge_direction      = storeStr(38);
                    r.misc.road_ramp             = storeStr(39);
                    r.misc.bridge_segment        = storeStr(40);
                    r.location.latitude          = parseDouble(field(41));
                    r.location.longitude         = parseDouble(field(42));
                    r.location.location_point    = storeStr(43);

                    t_zip[tid].push_back(r.location.zip);
                    t_created[tid].push_back(r.time.created);
                    t_lat[tid].push_back((float)r.location.latitude);
                    t_lon[tid].push_back((float)r.location.longitude);
                    t_records[tid].push_back(std::move(r));
                }

                p = nl ? nl + 1 : end_ptr;
            }
        }
    }

    munmap((void*)mapped, file_size);

    // compute per-thread offsets
    std::vector<size_t> offsets(num_threads + 1, 0);
    for (int i = 0; i < num_threads; ++i)
        offsets[i + 1] = offsets[i] + t_records[i].size();
    size_t total = offsets[num_threads];

    // pre-size final arrays
    records_.resize(total);
    zip_.resize(total);
    created_.resize(total);
    lat_.resize(total);
    lon_.resize(total);

    // parallel merge — each thread copies its own chunk directly
    #pragma omp parallel num_threads(num_threads)
    {
        int tid = omp_get_thread_num();
        size_t base = offsets[tid];
        size_t n    = t_records[tid].size();

        for (size_t j = 0; j < n; ++j)
            records_[base + j] = std::move(t_records[tid][j]);

        std::memcpy(zip_.data()     + base, t_zip[tid].data(),     n * sizeof(uint32_t));
        std::memcpy(created_.data() + base, t_created[tid].data(), n * sizeof(uint32_t));
        std::memcpy(lat_.data()     + base, t_lat[tid].data(),     n * sizeof(float));
        std::memcpy(lon_.data()     + base, t_lon[tid].data(),     n * sizeof(float));
    }
}

std::vector<const ServiceRequest*>
SoADataStore::searchByZip(uint32_t zip_min, uint32_t zip_max, int num_threads) const {
    std::vector<std::vector<const ServiceRequest*>> local(num_threads);

    #pragma omp parallel num_threads(num_threads)
    {
        int tid = omp_get_thread_num();
        #pragma omp for nowait schedule(static)
        for (int i = 0; i < (int)zip_.size(); ++i)
            if (zip_[i] >= zip_min && zip_[i] <= zip_max)
                local[tid].push_back(&records_[i]);
    }

    std::vector<const ServiceRequest*> result;
    for (auto& v : local) result.insert(result.end(), v.begin(), v.end());
    return result;
}

std::vector<const ServiceRequest*>
SoADataStore::searchByDate(uint32_t from, uint32_t to, int num_threads) const {
    std::vector<std::vector<const ServiceRequest*>> local(num_threads);

    #pragma omp parallel num_threads(num_threads)
    {
        int tid = omp_get_thread_num();
        #pragma omp for nowait schedule(static)
        for (int i = 0; i < (int)created_.size(); ++i)
            if (created_[i] >= from && created_[i] <= to)
                local[tid].push_back(&records_[i]);
    }

    std::vector<const ServiceRequest*> result;
    for (auto& v : local) result.insert(result.end(), v.begin(), v.end());
    return result;
}

std::vector<const ServiceRequest*>
SoADataStore::searchByBoundingBox(double lat_min, double lat_max,
                                   double lon_min, double lon_max,
                                   int num_threads) const {
    std::vector<std::vector<const ServiceRequest*>> local(num_threads);

    float flt_lat_min = (float)lat_min, flt_lat_max = (float)lat_max;
    float flt_lon_min = (float)lon_min, flt_lon_max = (float)lon_max;

    #pragma omp parallel num_threads(num_threads)
    {
        int tid = omp_get_thread_num();
        #pragma omp for nowait schedule(static)
        for (int i = 0; i < (int)lat_.size(); ++i)
            if (lat_[i] >= flt_lat_min && lat_[i] <= flt_lat_max &&
                lon_[i] >= flt_lon_min && lon_[i] <= flt_lon_max)
                local[tid].push_back(&records_[i]);
    }

    std::vector<const ServiceRequest*> result;
    for (auto& v : local) result.insert(result.end(), v.begin(), v.end());
    return result;
}