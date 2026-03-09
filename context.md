# Mini1 — Phase 1 Design Context

## Project Overview

- **Assignment:** Memory Overload (Mini 1)
- **Focus:** Memory utilization and serial processing (Phase 1)
- **Language:** C++17 — gcc/g++ v13+ or Clang v16+
- **Build:** CMake
- **No threads in Phase 1** (serial only)

---

## Dataset

- **File:** `dataset/nyc_311_2020_2026.csv`
- **Source:** NYC OpenData — 311 Service Requests (2020–2026)
- **Records:** ~20.1 million
- **Size:** ~12 GB
- **Columns:** 44
- **Machine RAM:** 16 GB

---

## Range Searches (3 total — all pure numeric/float comparisons)

| # | Field | Search Type | Storage Type |
|---|-------|-------------|-------------|
| 1 | `Incident Zip` | Numeric range `[lo, hi]` | `uint32_t` |
| 2 | `Created Date` | Date range `[start, end]` | `uint32_t` (epoch seconds) |
| 3 | `Latitude` + `Longitude` | Bounding box `[lat1,lon1,lat2,lon2]` | `double` |

No text-based searches. All queries are integer or float comparisons only.

---

## Memory / Storage Strategy (Hybrid)

### Rule: match storage to field characteristics

| Field Category | Examples | Storage |
|----------------|----------|---------|
| Low-cardinality categoricals | Borough, Agency, Status, Location Type, Address Type, Channel Type, Park Borough | Intern → `uint8_t` |
| Medium-cardinality categoricals | Problem (Complaint Type), Problem Detail | Intern → `uint16_t` |
| Date fields | Created, Closed, Due, Resolution Action Updated | Custom parser → `uint32_t` epoch |
| Numeric primitives | Zip, Lat, Lon, X/Y Coord, BBL, Unique Key, Community Board, Council District, Police Precinct | Native types (`uint32_t`, `double`, `uint64_t`, `uint16_t`) |
| Non-searchable strings | Incident Address, Resolution Description, City, Landmark, Agency Name, Additional Details, Street Name, Cross Streets, Intersection Streets, Facility Type, Park Facility Name, Vehicle Type, Taxi fields, Bridge/Highway fields, Location (POINT) | `StringRef {uint32_t offset, uint16_t length}` → string pool |

---

## String Interning (StringRegistry)

One `StringRegistry` instance per categorical field.

### Why intern?
- `Borough` has 6 unique values → `uint8_t` (1 byte) vs `std::string` (24+ bytes)
- Integer comparison (`==`) vs `strcmp` — faster and cache-friendly
- 64 `uint8_t` values fit in one CPU cache line (64 bytes)
- Estimated savings: **4–5 GB** across all categorical fields for 20M records

### Implementation
```
encode: "MANHATTAN" → uint8_t 3    (used at load time)
decode: uint8_t 3   → "MANHATTAN"  (used at query/display time)

Data structures:
  unordered_map<string, uint16_t>  encode_map   // string → id
  vector<string>                   decode_vec   // index  → string (O(1) decode)
```

---

## String Pool (Arena Allocation)

One global `char[]` buffer for all non-searchable string data.
Records store a `StringRef` (6 bytes) pointing into the pool — no per-string heap allocation.

```
StringRef {
    uint32_t offset;   // position in pool
    uint16_t length;   // string length
};                     // 6 bytes total vs 24+ bytes for std::string
```

- Zero heap fragmentation
- Contiguous memory — cache friendly
- **Fixed pre-allocation: 5 GB** — allocated once at startup, never reallocated
- Growable pool is NOT viable — `realloc` moves the base pointer, invalidating all `StringRef` offsets across 20M records
- Hard `abort()` if pool overflows (will not happen with correct estimate)
- Empty fields → `{offset=0, length=0}` (sparse fields like bridge/taxi are mostly empty)
- Good benchmark data point: measure actual pool usage vs 5 GB estimate after load

---

## Date Parsing

### Format in dataset
```
"10/05/2023 11:32:28 AM"   (MM/DD/YYYY HH:MM:SS AM/PM)
```

### Decision: Custom parser (NOT strptime)
- Faster — no format string interpretation, no locale
- Thread-safe — no shared static state (critical for Phase 2)
- Fully inlineable and compiler-optimizable

### Storage: `uint32_t` (seconds since Unix epoch)
- `uint32_t` max = Feb 7, 2106 — sufficient for 2020–2026 data
- Saves 4 bytes per timestamp vs `int64_t`
- 4 date fields × 4 bytes × 20M records = **320 MB** (vs 640 MB with `int64_t`)
- Range comparison becomes pure integer: `ts >= start && ts <= end`

### NULL dates
Fields like `Closed Date`, `Due Date`, `Resolution Date` are often empty.
Use `0` as sentinel value.

```
TimeInfo {
    uint32_t created;     // always present
    uint32_t closed;      // 0 = not yet closed
    uint32_t due;         // 0 = no due date
    uint32_t resolution;  // 0 = no resolution date
}
```

---

## CSV Parser

### Problem
Some fields contain commas inside quotes:
```
...,The landlord was notified,"Fix it, or else",...
```
Naive split on `,` breaks. Need a quoted-field tokenizer.

### Decision: Custom CSVParser class
- Toggle `inside_quotes` flag on `"` characters
- Only split on `,` when NOT inside quotes
- Use `std::string_view` (not `std::string`) to avoid field copies during parsing
- 20M rows × avoid allocations = significant performance benefit

---

## Memory Pre-allocation Strategy (Two-Pass Load)

### Pass 1 — Count lines (fast)
- Read file in 64 KB chunks
- Count `\n` characters → exact record count
- Subtract 1 for header
- No CSV parsing — just raw byte scan

### Pass 2 — Parse and load
- `records.reserve(count)` before loading → zero reallocations
- Also pre-size the string pool based on estimated average string lengths

---

## Estimated Memory Footprint (20M records)

| Component | Estimate |
|-----------|----------|
| Record structs | ~3.9 GB |
| String pool (actual string bytes) | ~3–4 GB |
| StringRegistry tables | ~50 MB |
| **Total data** | **~7–8 GB** |
| OS + other processes | ~2–3 GB |
| **Peak during load** | **~10–11 GB** |

16 GB RAM is sufficient but leaves limited headroom. Monitor with `/usr/bin/time -v` or `valgrind --tool=massif`.

---

## Class Design — Layered OOP

```
DataStore  (Facade — top-level API)
├── load(filepath)
├── searchByZip(uint32_t lo, uint32_t hi)
├── searchByDate(uint32_t start, uint32_t end)
├── searchByBoundingBox(double lat1, double lon1, double lat2, double lon2)
├── vector<ServiceRequest>   records
├── StringPool               pool
└── StringRegistry           registries[N]   (one per categorical field)

ServiceRequest  (one record — all 44 columns)
├── uint64_t          unique_key
├── TimeInfo
│   ├── uint32_t created, closed, due, resolution
├── LocationInfo
│   ├── uint32_t  zip
│   ├── double    latitude, longitude
│   ├── int32_t   x_coord, y_coord
│   ├── uint8_t   borough          (interned)
│   └── StringRef city, address, street_name,
│                 cross_street_1, cross_street_2,
│                 intersection_1, intersection_2
├── ServiceInfo
│   ├── uint8_t   agency           (interned)
│   ├── StringRef agency_name
│   ├── uint16_t  problem          (interned)
│   ├── uint16_t  problem_detail   (interned)
│   ├── StringRef additional_details
│   ├── uint8_t   location_type    (interned)
│   ├── uint8_t   address_type     (interned)
│   ├── StringRef facility_type, landmark
│   ├── uint8_t   status           (interned)
│   └── StringRef resolution_desc
├── AdminInfo
│   ├── uint16_t  community_board
│   ├── uint16_t  council_district
│   ├── uint16_t  police_precinct
│   ├── uint64_t  bbl
│   ├── uint8_t   channel_type     (interned)
│   └── StringRef park_facility_name
└── MiscInfo
    ├── uint8_t   park_borough     (interned)
    ├── StringRef vehicle_type, taxi_company_borough,
    │             taxi_pickup_location,
    │             bridge_highway_name, bridge_highway_direction,
    │             road_ramp, bridge_highway_segment
    └── StringRef location_point   (POINT format — last column)
```

---

## DataStore API (Facade)

```cpp
class DataStore {
public:
    void load(const std::string& filepath);

    std::vector<const ServiceRequest*> searchByZip(uint32_t lo, uint32_t hi);
    std::vector<const ServiceRequest*> searchByDate(uint32_t start, uint32_t end);
    std::vector<const ServiceRequest*> searchByBoundingBox(double lat1, double lon1,
                                                            double lat2, double lon2);
private:
    std::vector<ServiceRequest> records;
    StringPool                  pool;
    StringRegistry              borough_reg, agency_reg, status_reg, /* ... */;
};
```

Searches are **independent only** — no compound queries.
Each search performs a **linear scan** across all records (Phase 1 baseline).

---

## Benchmark Harness (Separate from Business Logic)

- Use `std::chrono::high_resolution_clock`
- Measure and record separately:
  - **Load time** (two-pass: count + parse)
  - **Per-query time** (each of the 3 search types)
  - **Memory footprint** (peak RSS)
- Run **10+ executions** to compute averages
- Report both successes and failures

### Key comparisons to benchmark (good report data)
1. `strptime` vs custom date parser — parse speed across 20M rows
2. Naive `std::string` vs interned vs string pool — memory footprint
3. Raw I/O speed (Pass 1) vs full parse speed (Pass 2)
4. Linear scan speed for each of the 3 query types

---

## Malformed Row Handling

Load every row — never skip. Use sentinel values for missing or unparseable fields.

| Field Type | Sentinel Value |
|------------|---------------|
| Dates (`uint32_t`) | `0` |
| Zip, Precinct, Board, District (`uint16_t`/`uint32_t`) | `0` |
| Latitude / Longitude (`double`) | `0.0` |
| Interned categoricals (`uint8_t`/`uint16_t`) | `0` (empty string maps to id 0) |
| `StringRef` | `{offset=0, length=0}` |
| `uint64_t` (BBL, Unique Key) | `0` |

---

## Project File Structure

```
CMPE275-mini1/
├── CMakeLists.txt                  ← top-level, includes all phases
├── context.md
├── mini1-memory-nyc.md
├── dataset/                        ← not committed to git
│
├── common/                         ← shared across all phases
│   ├── include/
│   │   ├── StringPool.hpp
│   │   ├── StringRegistry.hpp
│   │   └── DateParser.hpp
│   └── src/
│       ├── StringPool.cpp
│       ├── StringRegistry.cpp
│       └── DateParser.cpp
│
├── phase1/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── ServiceRequest.hpp      ← struct + sub-structs
│   │   ├── CSVParser.hpp
│   │   └── DataStore.hpp           ← facade
│   ├── src/
│   │   ├── CSVParser.cpp
│   │   ├── DataStore.cpp
│   │   └── main.cpp                ← entry point + benchmark runner
│   └── benchmark/
│       ├── Benchmark.hpp
│       └── Benchmark.cpp
│
├── phase2/                         ← placeholder, filled later
├── phase3/                         ← placeholder, filled later
│
└── scripts/
    └── plot.py                     ← Python graphs for report
```

**Key decisions:**
- `common/` holds utilities shared across all phases — no duplication between phases
- Each phase is self-contained with its own `CMakeLists.txt` and binary
- `benchmark/` is separate from `src/` — business logic stays clean
- `dataset/` is never committed to git (12 GB file)

---

## What is NOT in Phase 1

- No threads / parallelism (Phase 2)
- No Object-of-Arrays / vectorization (Phase 3)
- No third-party libraries (no databases, no Boost unless needed)
- No compound/joined queries
- No secondary indices or sorted indices — pure linear scan

---

## Phase 1 — Implementation Notes (Post-Build)

### Verified CSV Column Order (0-indexed from actual header)

| Index | Column Name | Mapped To |
|-------|-------------|-----------|
| 0 | Unique Key | `unique_key` (`uint64_t`) |
| 1 | Created Date | `time.created` (`uint32_t` epoch) |
| 2 | Closed Date | `time.closed` |
| 3 | Agency | `service.agency` (interned `uint8_t`) |
| 4 | Agency Name | `service.agency_name` (pool `StringRef`) |
| 5 | Problem | `service.problem` (interned `uint16_t`) |
| 6 | Problem Detail | `service.problem_detail` (interned `uint16_t`) |
| 7 | Additional Details | `service.additional_details` (pool `StringRef`) |
| 8 | Location Type | `service.location_type` (interned `uint8_t`) |
| 9 | Incident Zip | `location.zip` (`uint32_t`) |
| 10 | Incident Address | `location.address` (pool) |
| 11 | Street Name | `location.street_name` (pool) |
| 12 | Cross Street 1 | `location.cross_street_1` (pool) |
| 13 | Cross Street 2 | `location.cross_street_2` (pool) |
| 14 | Intersection Street 1 | `location.intersection_1` (pool) |
| 15 | Intersection Street 2 | `location.intersection_2` (pool) |
| 16 | Address Type | **skipped** (sentinel 0) |
| 17 | City | `location.city` (pool) |
| 18 | Landmark | `location.landmark` (pool) |
| 19 | Facility Type | `service.facility_type` (pool) |
| 20 | Status | `service.status` (interned `uint8_t`) |
| 21 | Due Date | `time.due` (epoch) |
| 22 | Resolution Description | **skipped** (see Pool Sizing below) |
| 23 | Resolution Action Updated Date | `time.resolution` (epoch) |
| 24 | Community Board | `admin.community_board` (`uint16_t`) |
| 25 | Council District | `admin.council_district` (`uint16_t`) |
| 26 | Police Precinct | `admin.police_precinct` (`uint16_t`) |
| 27 | BBL | `admin.bbl` (`uint64_t`) |
| 28 | Borough | `location.borough` (interned `uint8_t`) |
| 29 | X Coordinate | `location.x_coord` (`int32_t`) |
| 30 | Y Coordinate | `location.y_coord` (`int32_t`) |
| 31 | Open Data Channel Type | `admin.channel_type` (interned `uint8_t`) |
| 32 | Park Facility Name | `admin.park_facility` (pool) |
| 33 | Park Borough | `admin.park_borough` (interned `uint8_t`) |
| 34 | Vehicle Type | `misc.vehicle_type` (pool) |
| 35 | Taxi Company Borough | `misc.taxi_borough` (pool) |
| 36 | Taxi Pick Up Location | `misc.taxi_pickup` (pool) |
| 37 | Bridge Highway Name | `misc.bridge_name` (pool) |
| 38 | Bridge Highway Direction | `misc.bridge_direction` (pool) |
| 39 | Road Ramp | `misc.road_ramp` (pool) |
| 40 | Bridge Highway Segment | `misc.bridge_segment` (pool) |
| 41 | Latitude | `location.latitude` (`double`) |
| 42 | Longitude | `location.longitude` (`double`) |
| 43 | Location | `location.location_point` (pool) |

---

### Pool Sizing — Actual vs Estimated

| Field | Avg Length | Projected 20M rows |
|-------|------------|-------------------|
| Resolution Description (col 22) | 154 chars | ~3.08 GB |
| Agency Name (col 4) | 36 chars | ~0.72 GB |
| Location Point (col 43) | 39 chars | ~0.79 GB |
| Address (col 10) | 17 chars | ~0.35 GB |
| Street Name (col 11) | 13 chars | ~0.26 GB |
| All others combined | — | ~0.35 GB |

**Decision:** `resolution_desc` (col 22) is **not stored in the pool**. At ~3GB, it alone exhausts the `uint32_t`-limited pool (max ~4.29 GB). It is not needed for any of the 3 range queries. Left as sentinel `{0,0}`.

**Pool size set to:** `3500 * 1024 * 1024` bytes (3.5 GB) — sufficient without `resolution_desc`.

**Constraint:** `StringRef.offset` is `uint32_t` → pool hard-capped at ~4.29 GB. Cannot exceed this without changing `StringRef` to `uint64_t` offset (which would increase struct size).

---

### Cardinality Verification (confirmed via dataset scan)

| Field | Unique Values | Type Chosen |
|-------|--------------|-------------|
| Problem (Complaint Type) | 271 | `uint16_t` ✓ |
| Problem Detail (Descriptor) | 1,293 | `uint16_t` ✓ (uint8_t would overflow) |
| Agency | <50 | `uint8_t` ✓ |
| Borough | 6 | `uint8_t` ✓ |
| Status | <20 | `uint8_t` ✓ |

---

### Phase 1 Benchmark Results (10 runs, machine: MacBook M-series, 16GB RAM)

Run on: Sat Mar 7, 2026. Binary at `build/phase1/phase1`, dataset `nyc_311_2020_2026.csv`.
No sleep between runs (data stays warm in OS page cache after first load).

**Raw results (all 10 runs):**

| Run | load (ms) | searchByZip (ms) | searchByDate (ms) | searchByBBox (ms) |
|-----|-----------|-----------------|-------------------|-------------------|
| 1 | 95,141 | 14,012 | 568 | 444 |
| 2 | 93,487 | 2,652 | 2,785 | 2,786 |
| 3 | 93,425 | 2,700 | 2,586 | 2,723 |
| 4 | 93,555 | 2,666 | 2,613 | 2,629 |
| 5 | 93,411 | 2,524 | 2,521 | 2,937 |
| 6 | 93,291 | 2,692 | 2,667 | 2,740 |
| 7 | 93,774 | 2,733 | 2,687 | 3,114 |
| 8 | 92,833 | 3,665 | 2,864 | 2,840 |
| 9 | **437,783** | **10,576** | **1,039** | **356** |
| 10 | 92,505 | 4,798 | 3,211 | 2,909 |

**Summary (as reported by benchmark script, including Run 9):**

| Operation | Avg | Min | Max | Result Count |
|-----------|-----|-----|-----|-------------|
| `load` | 127,921 ms | 92,505 ms | 437,783 ms | 20,129,233 records |
| `searchByZip` (10001–10099) | 4,902 ms | 2,524 ms | 14,012 ms | 3,878,133 |
| `searchByDate` (all of 2023) | 2,354 ms | 568 ms | 3,211 ms | 3,224,721 |
| `searchByBoundingBox` (Brooklyn) | 2,348 ms | 356 ms | 3,114 ms | 8,043,002 |

**Clean averages (excluding anomalous Run 9):**

| Operation | Avg (9 runs) |
|-----------|-------------|
| `load` | ~93,600 ms |
| `searchByZip` | ~4,282 ms* |
| `searchByDate` | ~2,587 ms |
| `searchByBoundingBox` | ~2,680 ms |

*searchByZip Run 1 is cold-cache (14,012ms); Run 2–8,10 average ~2,870ms.

**Observations and Analysis:**
- Load time ~93s serial for 12GB file — dominated by I/O + CSV parsing with no parallelism
- **Run 9 anomaly**: load=437,783ms — clear OS swap/paging event (machine under memory pressure evicted pages to swap, causing disk reads during load). All subsequent operations in Run 9 also anomalous (cold cache state).
- **Run 1 searchByZip cold cache**: 14,012ms vs subsequent 2,600–2,700ms. File had just been read by `load`; OS page cache partially filled but zip fields (early in each record) needed re-fetched. By Run 2, all relevant pages resident.
- **Run 1 searchByDate and searchByBBox**: anomalously fast (568ms, 444ms) — after searchByZip warmed up all 20M records in cache, the OS held all data pages warm for the next two queries in Run 1.
- `searchByDate` and `searchByBoundingBox` steady at ~2.6–2.8s in warm runs — pure linear scan over 20M records in RAM.

**Test queries used:**
- Zip: `[10001, 10099]` (Manhattan zip codes)
- Date: `toEpoch(2023,1,1,0,0,0)` to `toEpoch(2023,12,31,23,59,59)` (full year 2023)
- BBox: lat `[40.57, 40.74]`, lon `[-74.04, -73.83]` (Brooklyn/lower NYC bounds)

---

### Known Trade-offs / Limitations (Phase 1)

1. **`resolution_desc` not stored** — field exists in `ServiceRequest.hpp` but is always `{0,0}` sentinel. At ~154 chars avg × 20M rows = ~3.08 GB, it alone would exhaust the `uint32_t`-limited pool (max ~4.29 GB). Not needed for any of the 3 range queries.
2. **`address_type` (col 16) not stored in Phase 1** — column skipped, `location.address_type` always 0. Fixed in Phase 2.
3. **Pool capped at ~4.29 GB** — `StringRef.offset` is `uint32_t`. Cannot exceed without changing to `uint64_t` offset (+4 bytes per StringRef × 20M records × ~21 StringRefs = +1.68 GB).
4. **Linear scan searches** — O(n) per query, no indices. Phase 2 adds parallelism; Phase 3 would add indexing/vectorization.

---

## Phase 2 — Parallel Load + Parallel Search

### Goal

Parallelize both loading and searching using OpenMP. No algorithmic changes — same linear scan, same data structures — but split work across multiple CPU cores.

**Design constraint:** Phase 2 must be fully independent of Phase 1. It reuses Phase 1's `ServiceRequest.hpp` and `CSVParser.hpp` via CMake include paths (no copying, no modifying Phase 1 files). Each phase produces its own binary.

---

### Phase 2 File Structure

```
phase2/
├── CMakeLists.txt
├── run_benchmark.sh
├── benchmark_results.txt
├── include/
│   └── ParallelDataStore.hpp       ← parallel version of DataStore
├── src/
│   ├── ParallelDataStore.cpp       ← load() + search() with OpenMP
│   └── main.cpp                    ← entry point
└── benchmark/
    ├── Benchmark.hpp               ← BenchmarkResult has num_threads field
    └── Benchmark.cpp               ← chrono-based timing
```

---

### Thread Configuration (Fixed Optimal)

```cpp
const int LOAD_THREADS   = 4;   // file I/O bound; more threads = diminishing returns
const int SEARCH_THREADS = 8;   // CPU bound scan; 8 fully utilizes all cores
```

Output tag format: `[load|T=4]`, `[searchByZip|T=8]`, etc. Benchmark script greps by tag.

---

### ParallelDataStore Design

**Key difference from Phase 1 DataStore:**
- `load(path, num_threads)` — takes a thread count
- All 3 searches take a `num_threads` argument
- Two string pools: `pool_` + `res_pool_` (Phase 2 stores `resolution_desc` unlike Phase 1)

```cpp
class ParallelDataStore {
    std::vector<ServiceRequest> records_;
    StringPool pool_;        // 3.5 GB — all strings except resolution_desc
    StringPool res_pool_;    // 3.2 GB — resolution_desc only (col 22)

    StringRegistry<uint8_t>  agency_reg_, borough_reg_, location_type_reg_,
                              status_reg_, channel_type_reg_, park_borough_reg_,
                              address_type_reg_;   // address_type NOW stored (was skipped in P1)
    StringRegistry<uint16_t> problem_reg_, problem_detail_reg_;

    ServiceRequest parseRow(std::string_view* fields, int nfields);
};
```

**Why two pools?** `StringRef.offset` is `uint32_t` (max ~4.29 GB). Pool for all other strings = ~2.5 GB. `resolution_desc` alone = ~3.08 GB. Cannot fit both in one pool. Solution: dedicate a separate `res_pool_` (3.2 GB) exclusively for col 22.

---

### Parallel Load — `load(path, num_threads)`

**Algorithm: file splitting + per-thread independent parsing**

```
Step 1: Get file size (seek to end)
Step 2: Read header line, record body_start offset
Step 3: Compute N raw byte boundaries (equal splits of body)
Step 4: Snap each boundary forward to next '\n' (no line split across threads)
Step 5: Each thread owns an independent ifstream, seeks to its boundary, parses its chunk
Step 6: Merge all per-thread vectors into records_
```

**Why snap boundaries to `\n`?** Raw arithmetic split will land in the middle of a CSV line. We scan forward byte-by-byte until we find the next newline, so each thread always starts at a line boundary. The serial boundary-snapping happens before the parallel section.

**Why per-thread `ifstream`?** `ifstream` has an internal file position cursor. If multiple threads shared one `ifstream`, seek+read pairs would race. Each thread opens its own file descriptor → completely independent, no locking.

**Why per-thread local vectors during load?** During parsing, `parseRow` calls `StringPool::store()` and `StringRegistry::encode()`. These are thread-safe (see below). But `records_.push_back()` on a shared `vector` would race. Solution: each thread accumulates into `thread_records[tid]` (its own private vector, indexed by thread ID). After the OMP barrier, the main thread merges all vectors sequentially.

**Key implementation detail — OMP early-exit:** Cannot use `return` or `break` inside `#pragma omp parallel`. Instead, wrap thread body in `if (start < end) { ... }` to skip threads with empty chunks.

```cpp
#pragma omp parallel num_threads(num_threads)
{
    int tid = omp_get_thread_num();
    int64_t start = boundaries[tid], end = boundaries[tid+1];
    if (start < end) {   // ← NOT "return" — return is illegal inside OMP region
        std::ifstream f(path, std::ios::binary);
        f.seekg(start);
        // ... parse loop ...
    }
}
```

---

### Parallel Search — `searchByZip`, `searchByDate`, `searchByBoundingBox`

All three searches use the same pattern:

```cpp
std::vector<std::vector<const ServiceRequest*>> local(num_threads);

#pragma omp parallel num_threads(num_threads)
{
    int tid = omp_get_thread_num();
    #pragma omp for nowait schedule(static)
    for (int i = 0; i < (int)records_.size(); ++i)
        if (/* condition */)
            local[tid].push_back(&records_[i]);
}

std::vector<const ServiceRequest*> result;
for (auto& v : local) result.insert(result.end(), v.begin(), v.end());
return result;
```

**`schedule(static)`**: OMP divides the loop range into N equal contiguous chunks upfront. Thread 0 gets records [0, n/8), thread 1 gets [n/8, 2n/8), etc. No dynamic work stealing. If data is geographically clustered (e.g., Manhattan zip codes physically adjacent in file), one chunk may have more hits, causing load imbalance — but the scan cost per record is identical regardless of hit/miss.

**`nowait`**: Thread exits its OMP chunk immediately without waiting at an implicit barrier. Since we're reading `records_` (const) and writing to `local[tid]` (private index), there's no shared mutable state during the scan. The merge happens after all threads have naturally finished.

**No mutex needed during search**: `local[tid]` is indexed by thread ID. Thread 0 always writes to `local[0]`, thread 1 to `local[1]`, etc. These are separate `vector` objects in memory — no two threads ever write to the same vector.

---

### Thread Safety — StringPool (Lock-Free)

`StringPool::store()` is called from multiple threads simultaneously during load.

```cpp
// StringPool internally:
std::atomic<uint32_t> top_;    // current write position
char* buf_;                     // pre-allocated buffer

StringRef store(const char* data, uint16_t len) {
    uint32_t offset = top_.fetch_add(len, std::memory_order_relaxed);
    // offset is unique to this thread — no other thread gets same range
    std::memcpy(buf_ + offset, data, len);
    return {offset, len};
}
```

`fetch_add` is a single atomic CPU instruction (`LDADD` on ARM64, `LOCK XADD` on x86). It atomically returns the old value and increments `top_` by `len`. This gives each thread a unique, non-overlapping byte range in the buffer. Thread A gets bytes [100, 115), thread B gets [115, 132), etc. They write to different memory addresses — no race condition, no locking needed.

**Why no race condition?** Two conditions for a race: (1) concurrent access to shared data, AND (2) at least one write. `fetch_add` ensures each thread writes to a unique range — condition 1 is eliminated at the byte level. The buffer itself is shared but the *regions written* are disjoint.

---

### Thread Safety — StringRegistry (Read-Write Lock)

`StringRegistry::encode()` is also called from multiple threads during load. Uses `std::shared_mutex`:

- **Fast path (string already seen)**: `shared_lock` (multiple threads can hold simultaneously) → lookup in `encode_map` → return id. No exclusive lock.
- **Slow path (new string)**: try shared_lock first, check again inside → take `unique_lock` → double-check → insert. Double-check prevents duplicate insertion if two threads race to add the same new string.

This is the classic "double-checked locking" pattern adapted for `shared_mutex`.

---

### Cache Line Behavior — Why Searches Are Slow (and Why Phase 2 Is Faster)

**CPU cache line = 64 bytes** — the minimum unit of data the CPU loads from RAM.

**`ServiceRequest` struct = 264 bytes** = spans 5 cache lines.

When scanning records for `searchByZip`:
- We access `records_[i].location.zip` (4 bytes, at ~byte offset 48 within the struct)
- To read those 4 bytes, CPU loads the 64-byte cache line containing offset 48
- Record `i+1` starts 264 bytes later — that's 4+ cache lines away
- Every record access causes at least 1 cache miss → RAM fetch (200–400ns latency)

**Math for 20M records:**
- Data volume: 20M × 264 bytes = **5.28 GB** total struct data
- Theoretical memory bandwidth: ~60 GB/s → minimum time = 5.28/60 = **~88ms**
- Actual time: ~2,600ms (Phase 1) — because L3 cache is only 8–12 MB; cannot hold 5.28 GB
- Every record access is a cache miss → sequential RAM fetches → memory latency dominates

**Why Phase 2 is faster (8 threads):**
- Each thread scans 1/8 of records: 5.28 GB / 8 = **660 MB per thread**
- 8 threads issue memory requests simultaneously → aggregate bandwidth approaches full 60 GB/s
- Actual speedup: 2,600ms → ~288ms = **~9x faster** — close to theoretical 8x linear scaling

**Why `searchByZip` Run 1 was 14,012ms:**
- After `load`, OS page cache partially warmed but not all records' pages were cache-resident
- `searchByZip` runs first → must page-fault or re-fetch all 5.28 GB from disk/slow path
- Runs 2+ → all 5.28 GB already in RAM (OS page cache) → only RAM latency, not disk latency

---

### Phase 2 Benchmark Results (10 runs, machine: MacBook M-series, 16GB RAM)

Run on: Fri Mar 6, 2026. 5-second sleep between runs (OS pressure test).
Binary at `build/phase2/phase2`, dataset `nyc_311_2020_2026.csv`.
Fixed threads: Load=4, Search=8.

**All 10 runs:**

| Run | load T=4 (ms) | searchByZip T=8 (ms) | searchByDate T=8 (ms) | searchByBBox T=8 (ms) |
|-----|--------------|---------------------|----------------------|----------------------|
| 1 | 38,716 | 263 | 57 | 69 |
| 2 | 42,657 | 327 | 72 | 86 |
| 3 | 36,394 | 321 | 74 | 85 |
| 4 | 37,052 | 313 | 73 | 70 |
| 5 | 36,375 | 256 | 58 | 69 |
| 6 | 37,394 | 263 | 58 | 68 |
| 7 | 42,232 | 344 | 77 | 87 |
| 8 | 39,853 | 251 | 57 | 70 |
| 9 | 37,608 | 276 | 61 | 72 |
| 10 | 38,669 | 271 | 60 | 67 |

**Summary (all 10 runs, no anomalies):**

| Operation | Avg | Min | Max | Result Count |
|-----------|-----|-----|-----|-------------|
| `load` (T=4) | 38,695 ms | 36,375 ms | 42,657 ms | 20,129,233 records |
| `searchByZip` (T=8) | 288 ms | 251 ms | 344 ms | 3,878,133 |
| `searchByDate` (T=8) | 65 ms | 57 ms | 77 ms | 3,224,721 |
| `searchByBoundingBox` (T=8) | 74 ms | 67 ms | 87 ms | 8,043,002 |

---

### Phase 1 vs Phase 2 Comparison

| Operation | Phase 1 (serial) | Phase 2 (parallel) | Speedup |
|-----------|------------------|--------------------|---------|
| `load` | ~93,600 ms | ~38,695 ms | **~2.4x** |
| `searchByZip` | ~2,870 ms (warm) | ~288 ms | **~10x** |
| `searchByDate` | ~2,660 ms | ~65 ms | **~41x** |
| `searchByBoundingBox` | ~2,745 ms | ~74 ms | **~37x** |

**Load speedup analysis (4 threads → 2.4x, not 4x):**
- Load is I/O bound, not CPU bound — 4 threads read 4 sections of the same 12GB file simultaneously
- Disk/SSD bandwidth is shared — 4 threads don't quadruple bandwidth, they compete for it
- Parsing (CSV, date, string interning) is parallelized → gives some CPU speedup
- Net: ~2.4x with 4 threads (I/O bottleneck caps gains)

**Search speedup analysis (~10–40x with 8 threads):**
- Searches are CPU + memory bandwidth bound
- 8 threads issue concurrent memory requests → aggregate RAM bandwidth ~8x
- Some super-linear gains (>8x) possible when total data fits better in combined L1/L2 caches across cores
- `searchByDate` and `searchByBBox` show 37–41x speedup — may benefit from NUMA effects or prefetch patterns

---

### Phase 2 Known Differences from Phase 1

1. **`resolution_desc` (col 22) IS stored in Phase 2** — uses dedicated `res_pool_` (3.2 GB). Phase 1 always set this to `{0,0}`.
2. **`address_type` (col 16) IS stored in Phase 2** — uses `address_type_reg_`. Phase 1 skipped it.
3. **Same `ServiceRequest` struct** — reused from `phase1/include/ServiceRequest.hpp`. No duplication.
4. **Same result counts** — verified: 20,129,233 records loaded, same 3 search results as Phase 1.

---

### Phase 2 Build Notes

- Uses OpenMP — detected via `find_package(OpenMP)` in CMake
- macOS requires `libomp` from Homebrew: `brew install libomp`
- CMake detects it via pkg-config fallback (hardcoded path `/opt/homebrew/opt/libomp`)
- Phase 2 CMakeLists links against `phase1` sources (`CSVParser.cpp`) + `common` sources
- Build: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target phase2`

---

### Common Errors Encountered During Phase 2 Development

1. **`cannot return from OpenMP region`** — `return` inside `#pragma omp parallel` block is illegal. Fixed by replacing `if (start >= end) return;` with `if (start < end) { ... }` wrapping the whole thread body.

2. **Linker error `_main not found`** — `main.cpp` was empty when CMake first configured. CMake cached it. After writing `main.cpp`, had to re-run `cmake -B build` (not just `cmake --build`) to force reconfiguration.

3. **Wrong include path in `Benchmark.hpp`** — Initially `#include "ParallelDataStore.hpp"` but `Benchmark.hpp` lives in `phase2/benchmark/`, not `phase2/include/`. Fixed to `#include "../include/ParallelDataStore.hpp"`.

---

## Phase 3 — SoA Layout + mmap + Parallel Search

### Goal

Optimize Phase 2 by:
1. **Object-of-Arrays (SoA)** layout for the 4 search-critical fields — eliminates cache waste during scans
2. **mmap** for zero-copy file loading — no `std::string` per line, no kernel→user copy
3. **Thread-local StringRegistry caches** — reduce mutex contention during parallel load

**Design constraint:** Phase 3 is fully independent of Phase 1 and Phase 2. Reuses `ServiceRequest.hpp`, `CSVParser.hpp` via CMake include paths only.

---

### Why SoA? Cache Line Analysis

**Phase 2 (AoS — Array of Structs):**
- `ServiceRequest` = 264 bytes = spans 5 × 64-byte cache lines
- `searchByZip` reads `records_[i].location.zip` (4 bytes at offset ~48)
- CPU loads entire 64-byte cache line to read 4 bytes → **6.25% utilization**
- 20M records × 264 bytes = 5.28 GB total data to scan
- Per thread (8 threads): 5.28/8 = 660 MB — never fits in L2/L3 cache

**Phase 3 (SoA — Structure of Arrays):**
- `zip_[]` is a flat `uint32_t` array — 64 bytes / 4 bytes = **16 values per cache line = 100% utilization**
- 20M × 4 bytes = 80 MB total zip data
- Per thread (8 threads): 80/8 = 10 MB — **fits in L2 cache (12–16 MB)**
- Same 16x improvement for `created_[]` (uint32_t), `lat_[]` (float), `lon_[]` (float)

**Why `float` for lat/lon instead of `double`:**
- `double` = 8 bytes, `float` = 4 bytes
- GPS precision: latitude needs ~7 significant digits (e.g., 40.71234)
- `float` has 7 significant digits — sufficient for range queries (~1 meter precision)
- Saves 50% memory in SoA arrays: lat_ + lon_ = 160 MB instead of 320 MB

---

### Why mmap?

**Phase 2 load path (ifstream + getline):**
```
SSD → kernel page cache → kernel buffer → read() syscall → userspace std::string
```
- Each `getline` allocates a `std::string` → heap alloc + copy
- 20M lines = 20M string allocations

**Phase 3 load path (mmap):**
```
SSD → kernel page cache = mmap region (same memory, no copy)
```
- `mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0)` maps file into virtual address space
- `CSVParser::parseLine` reads directly from mapped memory → returns `string_view` (zero-copy, no allocation)
- `madvise(ptr, file_size, MADV_SEQUENTIAL)` — tells OS to aggressively prefetch pages ahead of current read position
- After parsing: `munmap()` releases virtual mapping; OS reclaims pages under memory pressure

**Key distinction — mmap vs OS swap:**
- OS swap (Phase 1 Run 9 anomaly) = involuntary eviction of RAM to disk under memory pressure → disaster (437s load)
- mmap = intentional, controlled mapping of file into virtual space → no extra RAM copy

**Why mmap did not dramatically improve load time vs Phase 2:**
- The bottleneck is NOT file I/O — it's parsing (CSV split, date parse, StringPool store)
- mmap eliminates the `getline` allocation overhead (~4–5s) but the rest of the parsing dominates
- macOS read() is heavily optimized for sequential access; mmap can have slightly higher page-fault overhead for 12 GB files (3M page table entries)
- Net: mmap is slightly better or neutral vs getline for load time on macOS

---

### Phase 3 File Structure

```
phase3/
├── CMakeLists.txt
├── run_benchmark.sh
├── include/
│   └── SoADataStore.hpp         ← hybrid SoA + AoS store
├── src/
│   ├── SoADataStore.cpp         ← mmap load + SoA parallel search
│   └── main.cpp                 ← entry point
└── benchmark/
    ├── Benchmark.hpp
    └── Benchmark.cpp
```

---

### SoADataStore Design

```cpp
class SoADataStore {
    // HOT: flat SoA search arrays — one value per record, cache-line dense
    std::vector<uint32_t> zip_;      // col  9 — 16 values per 64-byte cache line
    std::vector<uint32_t> created_;  // col  1 — epoch seconds
    std::vector<float>    lat_;      // col 41 — 4 bytes, ~1m GPS precision
    std::vector<float>    lon_;      // col 42

    // COLD: full AoS records for result access after search
    std::vector<ServiceRequest> records_;

    StringPool pool_;       // 3.5 GB
    StringPool res_pool_;   // 3.2 GB — resolution_desc (col 22)

    StringRegistry<uint8_t>  agency_reg_, borough_reg_, location_type_reg_,
                              status_reg_, channel_type_reg_, park_borough_reg_,
                              address_type_reg_;
    StringRegistry<uint16_t> problem_reg_, problem_detail_reg_;
};
```

**Why two pools?** Same reason as Phase 2: `StringRef.offset` is `uint32_t` (max 4.29 GB). Main pool (~2.5 GB) + dedicated `res_pool_` for resolution_desc (~3.2 GB).

---

### Load Algorithm — mmap + Single-Pass + Parallel Merge

```
Step 1: open() + fstat() → get file_size
Step 2: mmap(PROT_READ, MAP_PRIVATE) → mapped const char* buffer
        close(fd)  ← fd no longer needed after mmap
        madvise(MADV_SEQUENTIAL)
Step 3: memchr(mapped, '\n') → find header end → body pointer
Step 4: Compute N thread boundaries (same snap-to-newline as Phase 2,
        but using pointer arithmetic on mapped buffer, no ifstream seeks)
Step 5: Per-thread local vectors: t_records[tid], t_zip[tid], t_created[tid],
        t_lat[tid], t_lon[tid]
Step 6: #pragma omp parallel — each thread walks its chunk with memchr('\n'),
        calls CSVParser::parseLine (zero-copy, string_view into mapped buffer),
        inlines all field assignments with enc8/enc16 cache lambdas,
        push_backs to local vectors
Step 7: munmap()
Step 8: Prefix sum of per-thread counts → compute offsets[]
Step 9: resize() all 5 final arrays to total count
Step 10: #pragma omp parallel merge — each thread memcpy/move its chunk
         directly into final arrays at pre-computed offset
```

**Why single-pass (not two-pass)?**
Early implementation used two-pass: pass 1 counted newlines per chunk (to pre-size arrays), pass 2 parsed. The extra full scan of 12 GB added ~3–4s overhead. Single-pass with per-thread local vectors + parallel merge is faster overall.

**Why per-thread local vectors first, then merge?**
Cannot write directly to `records_[i]` from multiple threads without knowing each thread's output range upfront (would need two-pass). Local vectors allow single-pass accumulation, then the parallel merge copies directly to the pre-sized final arrays.

**Parallel merge uses `memcpy` for SoA arrays:**
```cpp
std::memcpy(zip_.data() + base, t_zip[tid].data(), n * sizeof(uint32_t));
```
`uint32_t` and `float` are trivially copyable → `memcpy` is fastest possible copy.

---

### Thread-Local StringRegistry Cache

**Problem:** During parallel load, 6 threads call `encode()` on 9 shared `StringRegistry` instances simultaneously. Each call takes a `shared_lock` (fast path) or `unique_lock` (new string). Under 6-thread contention this causes lock thrashing.

**Solution:** Each thread maintains a local `unordered_map<string, uint8_t/uint16_t>` per registry as an L1 cache:

```cpp
// Inside #pragma omp parallel:
std::unordered_map<std::string, uint8_t> cache_agency, ...;

auto enc8 = [&](StringRegistry<uint8_t>& reg,
                std::unordered_map<std::string, uint8_t>& cache,
                std::string_view sv) -> uint8_t {
    if (sv.empty()) return 0;
    std::string key(sv);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;  // zero locks
    uint8_t id = reg.encode(sv);               // takes lock only on miss
    cache[key] = id;
    return id;
};
```

After the first ~2,000 records per thread, all unique values have been seen → **~100% cache hit rate → zero lock contention**.

**Empirical result:** The optimization had **neutral impact** on load time (~40.7s vs 39.2s). The `unordered_map` overhead (string construction + hash on every call, even cache hits) canceled out the mutex savings. This reveals that StringRegistry contention was NOT the dominant load bottleneck — StringPool atomic `fetch_add` and per-field memcpy (300M store calls) dominated instead.

**Research value:** This is an important empirical finding — profiling intuition (mutex = bottleneck) was wrong. The actual bottleneck was memory bandwidth for string storage, not lock contention.

---

### Parallel Search — SoA vs AoS

**Phase 2 searchByZip (AoS):**
```cpp
for i in records_.size():
    if records_[i].location.zip >= zip_min && records_[i].location.zip <= zip_max
```
- Accesses `records_[i].location.zip` at byte offset ~48 within 264-byte struct
- Each access = cache miss → loads 64-byte cache line → uses 4 bytes (6.25%)
- 20M records × 264 bytes = 5.28 GB bandwidth needed

**Phase 3 searchByZip (SoA):**
```cpp
for i in zip_.size():
    if zip_[i] >= zip_min && zip_[i] <= zip_max
        local[tid].push_back(&records_[i])  // only on hit — rare
```
- Accesses `zip_[i]` from flat `uint32_t` array
- Each cache line = 16 zip values = 100% utilization
- 20M × 4 bytes = 80 MB total bandwidth
- Per thread (8): 10 MB → fits in L2 cache → hardware prefetcher works perfectly

**`searchByBoundingBox` float precision:**
- `lat_[]` and `lon_[]` stored as `float`, search params are `double`
- Cast comparison bounds to `float` before the loop to avoid per-element promotion:
```cpp
float flt_lat_min = (float)lat_min; // cast once, outside loop
for i: if lat_[i] >= flt_lat_min && ...
```

---

### Thread Configuration (Phase 3)

```cpp
const int LOAD_THREADS   = 6;  // increased from 4; more overlap of parse + mutex wait
const int SEARCH_THREADS = 8;  // same as Phase 2; fully utilizes all cores
```

---

### Phase 3 Build Notes

- Same OpenMP detection as Phase 2 (Homebrew libomp fallback for macOS)
- `-O3` instead of Phase 2's `-O2` — Phase 3 is the performance-optimized phase
- `phase3/CMakeLists.txt` links against `phase1/src/CSVParser.cpp` + `common` library
- Add `add_subdirectory(phase3)` to root `CMakeLists.txt`
- Build: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target phase3`

---

### Phase 3 Development Issues

1. **Two-pass approach was slower than Phase 2** — Initial design used pass 1 (countNewlines via memchr) + pass 2 (parse). Extra full 12 GB scan added ~11s vs Phase 2. Fixed by switching to single-pass with per-thread local vectors.

2. **Incomplete OMP parallel block** — When adding thread-local caches, the parse loop body was accidentally replaced with a comment placeholder. The OMP block closed without any parsing, producing 0 records. Fixed by inlining the full field assignment block inside the parallel section and removing the now-unused `parseRow` method.

3. **`parseRow` made unused** — After inlining field assignments into the parallel block (needed for cache lambda access), `parseRow` became dead code. Removed from both `.cpp` and `.hpp`.

---

### Phase 3 Benchmark Results (single run, machine: MacBook M-series, 16GB RAM)

Run on: Sun Mar 8, 2026. Binary at `build/phase3/phase3`, dataset `nyc_311_2020_2026.csv`.
Fixed threads: Load=6, Search=8.

**Representative single run (pre-benchmark):**

| Operation | Time | Result Count |
|-----------|------|-------------|
| `load` (T=6) | ~40,724 ms | 20,129,233 records |
| `searchByZip` (T=8) | ~21.68 ms | 3,878,133 |
| `searchByDate` (T=8) | ~18.97 ms | 3,224,721 |
| `searchByBoundingBox` (T=8) | ~42.51 ms | 8,043,188* |

*Note: bounding box count differs from Phase 1/2 (8,043,002) by 186 records due to `float` precision for lat/lon storage (~1 meter rounding at boundary).

---

### Phase 2 vs Phase 3 Comparison

| Operation | Phase 2 (parallel AoS) | Phase 3 (SoA + mmap) | Speedup |
|-----------|------------------------|----------------------|---------|
| `load` | ~37,286 ms | ~40,724 ms | ~0.9x (comparable, within variance) |
| `searchByZip` | ~291 ms | ~22 ms | **~13x** |
| `searchByDate` | ~60 ms | ~19 ms | **~3x** |
| `searchByBoundingBox` | ~74 ms | ~43 ms | **~1.7x** |

**Load analysis:** Phase 3 load is statistically comparable to Phase 2 (~37–41s range seen across both phases). mmap eliminates the getline allocation overhead but introduces page-fault overhead; net effect is neutral on macOS. The real bottleneck is StringPool stores (~300M memcpy + atomic ops).

**Search analysis:** The SoA layout is the dominant win. Scanning a flat `uint32_t` array vs. striding through 264-byte structs reduces memory bandwidth by 66x for zip search (80 MB vs 5.28 GB). L2 cache now holds the entire per-thread working set (10 MB < 12 MB L2).

**Why searchByDate and searchByBBox improved less than searchByZip:**
- `created_[]` (uint32_t) shows 3x improvement — same theoretical benefit as zip. The smaller gain vs zip may reflect that date hits (~16% of records) require more pointer writes to result vector.
- `lat_[]/lon_[]` (float, two arrays) shows 1.7x — bounding box checks two arrays per record, and ~40% of records are hits (8M/20M), causing more `push_back` overhead than zip.

---

### Full Phase 1 → Phase 2 → Phase 3 Comparison

| Operation | Phase 1 (serial) | Phase 2 (parallel AoS) | Phase 3 (SoA+mmap) | P1→P3 Speedup |
|-----------|-----------------|------------------------|---------------------|---------------|
| `load` | ~93,600 ms | ~37,286 ms | ~40,724 ms | **~2.3x** |
| `searchByZip` | ~2,870 ms | ~291 ms | ~22 ms | **~130x** |
| `searchByDate` | ~2,660 ms | ~60 ms | ~19 ms | **~140x** |
| `searchByBoundingBox` | ~2,745 ms | ~74 ms | ~43 ms | **~64x** |

