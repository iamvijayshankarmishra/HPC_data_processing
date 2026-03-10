# Mini1 — Memory Overload: NYC 311 Service Requests
## Performance Research Report

**Course:** CMPE 275 — Memory and Concurrent Processing
**Dataset:** NYC 311 Service Requests (2020–2026), NYC OpenData
**Language:** C++17 (Clang, Homebrew, macOS)
**Build System:** CMake 3.16+
**Parallelism:** OpenMP (libomp via Homebrew)

---

## 1. Introduction

This project investigates three progressive strategies for loading and querying a 12 GB CSV dataset containing over 20 million records. The research questions driving this investigation were: (1) How much memory overhead does naive string storage impose on a fixed-RAM machine? (2) How much of the load and search latency is recoverable through parallelism alone? (3) What is the impact of restructuring memory layout from Array-of-Structs (AoS) to Structure-of-Arrays (SoA) on search throughput?

Each phase builds on the previous design, and all three phases implement the same three range-search queries — zip code range, date range, and geographic bounding box — allowing direct apples-to-apples performance comparison across the phases.

---

## 2. Dataset and Environment

| Property | Value |
|---|---|
| File | `nyc_311_2020_2026.csv` |
| Source | NYC OpenData — 311 Service Requests |
| Records | 20,129,233 |
| File size | ~12 GB |
| Columns | 44 |
| Machine RAM | 16 GB |
| CPU | Apple M-series (ARM64), 8 performance cores |
| OS | macOS Darwin 24.x |
| Compiler | Clang (Homebrew), C++17 |

The dataset exceeds half the available physical RAM (12 GB file on a 16 GB machine), making memory layout and OS interaction a central concern throughout all three phases.

---

## 3. Search Queries Implemented

All three phases implement the identical set of range queries to ensure benchmark comparability:

| Query | Field | Type | Description |
|---|---|---|---|
| `searchByZip` | Incident Zip | `uint32_t` range | Zip codes 10001–10099 (Manhattan south) |
| `searchByDate` | Created Date | `uint32_t` epoch range | Full year 2023 |
| `searchByBoundingBox` | Latitude + Longitude | `float`/`double` range | Manhattan bounding box (40.57–40.74°N, 74.04–73.83°W) |

These queries return 3.8M, 3.2M, and 8.0M records respectively, providing a high-selectivity, moderate-selectivity, and low-selectivity workload. No text-based searches are implemented — all comparisons are integer or float range checks.

---

## 4. Phase 1 — Serial Implementation

### 4.1 Design Philosophy

The central challenge of Phase 1 was fitting a 12 GB file into 16 GB of RAM while retaining all 44 fields for query and result access. Naively storing every field as a `std::string` would consume far more memory than the raw file size (each `std::string` adds 24 bytes of overhead on the heap, plus heap fragmentation). The design solution was a **hybrid memory layout** matching storage type to field characteristics.

### 4.2 Memory Strategy

The design applies the principle of **type-appropriate storage**: fields are stored in the smallest type that preserves the information needed.

**String Interning via `StringRegistry<T>`:** Low-cardinality categorical fields (Borough: 6 values, Agency: ~50 values, Status: ~20 values, Location Type, Address Type, Channel Type, Park Borough) are interned to `uint8_t` codes. Medium-cardinality fields (Complaint Type: 271 values, Complaint Detail: 1,293 values) are interned to `uint16_t` codes. This replaces string storage with a 1–2 byte integer, reducing per-record cost for these 9 fields from ~200 bytes to ~9 bytes total.

`StringRegistry<T>` uses an `unordered_map<string, T>` for encoding and a `vector<string>` for decoding. It is templated on the code type so the same structure works for both `uint8_t` and `uint16_t` registries.

**String Pool via `StringPool`:** Non-searchable string fields that cannot be reduced to codes (Incident Address, Street Name, Agency Name, Resolution Description, City, Landmark, Cross Streets, Intersection Streets, Park Facility Name, Vehicle Type, Taxi fields, Bridge/Highway fields) are stored via a pre-allocated arena allocator. A single `char* buffer_` of fixed capacity is pre-allocated at startup; each string is written contiguously with an atomic `fetch_add` offset counter. Fields are referenced via `StringRef { uint32_t offset; uint16_t length; }` — an 8-byte value that replaces what would otherwise be a 24-byte `std::string` pointer.

Two pools were required: the main pool (3.5 GB) for most string fields, and a dedicated resolution pool (3.2 GB) for the `resolution_desc` field (col 22) which averages 154 characters × 20M records ≈ 3 GB. A single pool would exceed the `uint32_t` offset limit of 4.29 GB.

**Date Fields:** All four date fields (`Created Date`, `Closed Date`, `Due Date`, `Resolution Action Updated Date`) are parsed from the NYC format string (`MM/DD/YYYY HH:MM:SS AM/PM`) into `uint32_t` Unix epoch seconds using a custom `parseDateTime()` function. This reduces 25-character date strings to 4 bytes each, and makes date range queries simple integer comparisons rather than string parsing at query time.

**Numeric Fields:** Latitude and longitude are stored as `double` (8 bytes). Zip code as `uint32_t`. X/Y coordinates as `int32_t`. BBL and Unique Key as `uint64_t`. Community Board, Council District, Police Precinct as `uint16_t`.

The resulting `ServiceRequest` struct is 264 bytes — significantly below a naive string-based representation which would exceed 1,500 bytes per record.

### 4.3 Phase 1 Peak Memory

**Observed: 11.30 GB (Activity Monitor)**

This is dominated by:
- `records_[]` AoS: 20.1M × 264 bytes = 5.28 GB
- Main StringPool: 3.5 GB pre-allocated
- Resolution StringPool: 3.2 GB pre-allocated
- OS page cache holding portions of the 12 GB file

The machine did not swap in normal runs, confirming the design kept working set within 16 GB.

### 4.4 Phase 1 Benchmark Results (10 runs)

All runs on the same machine, binary at `build/phase1/phase1`, 5-second sleep between runs.

| Run | Load (ms) | searchByZip (ms) | searchByDate (ms) | searchByBBox (ms) |
|---|---|---|---|---|
| 1 | 95,141 | 14,012 | 568 | 444 |
| 2 | 93,487 | 2,652 | 2,785 | 2,786 |
| 3 | 93,425 | 2,700 | 2,586 | 2,723 |
| 4 | 93,555 | 2,666 | 2,613 | 2,629 |
| 5 | 93,411 | 2,524 | 2,521 | 2,937 |
| 6 | 93,291 | 2,692 | 2,667 | 2,740 |
| 7 | 93,774 | 2,733 | 2,687 | 3,114 |
| 8 | 92,833 | 3,665 | 2,864 | 2,840 |
| **9** | **437,783** | **10,576** | **1,039** | **356** |
| 10 | 92,505 | 4,798 | 3,211 | 2,909 |
| **Avg (all 10)** | **127,921** | **4,902** | **2,354** | **2,348** |
| **Avg (excl. Run 9)** | **93,269** | **3,827** | **2,411** | **2,680** |

**Run 9 Anomaly (OS Swap Event):** Run 9 produced a load time of 437,783 ms — 4.7× slower than baseline. This was an involuntary OS swap event: the 16 GB machine had insufficient free RAM after 8 prior runs filled the page cache, forcing the OS to evict and re-read pages from SSD. This is distinct from the intentional mmap strategy in Phase 3; here the OS made an uncontrolled decision to swap. The event is documented as a data point illustrating the danger of operating near RAM limits.

**Run 1 Anomaly (Cache State):** Run 1 shows unusually high `searchByZip` (14,012 ms) but unusually low `searchByDate` (568 ms) and `searchByBBox` (444 ms). Immediately after loading, the record data is partially warm in L3 cache. The serial zip scan (traversing the `zip` field at a fixed offset within each 264-byte struct) triggered heavy TLB pressure on first traversal. The date and bbox scans, executed immediately after, found more data already in cache — hence the surprisingly low times. By Run 2, all three searches are cold and show consistent DRAM-speed access (~2,600–2,900 ms).

**Stable baseline (Runs 2–8, 10 — excluding anomalies):**
- Load: avg ≈ **93,285 ms**
- searchByZip: avg ≈ **3,053 ms**
- searchByDate: avg ≈ **2,741 ms**
- searchByBoundingBox: avg ≈ **2,835 ms**

### 4.5 Phase 1 CPU Utilization

**Observed: ~99.5% (single core)**

As expected for a serial implementation. The CPU is fully busy during load and search — no idle time, but no parallelism either. The bottleneck alternates between memory bandwidth (StringPool writes) and compute (date parsing, CSV tokenization).

---

## 5. Phase 2 — Parallel Implementation (OpenMP)

### 5.1 Design Philosophy

Phase 2 introduces OpenMP-based parallelism to both the load and search operations while reusing the entire Phase 1 data model (`ServiceRequest`, `StringPool`, `StringRegistry`). The `ParallelDataStore` class encapsulates the same 44-field storage strategy but adds thread coordination for both file ingestion and query execution.

Phase 2 is implemented as an independent module — it does not inherit from or modify Phase 1 code. It includes Phase 1 headers via CMake include paths only.

### 5.2 Parallel Load Design

**File partitioning (snap-to-newline algorithm):** The file is divided into N equal-byte chunks. Since a naive byte split would cut mid-record, each boundary is snapped forward to the next `\n` using `ifstream::seekg` + character scan. This guarantees each thread processes complete lines with no synchronization needed during parsing.

Each thread opens its own `ifstream` handle to the file, seeks to its partition start, and reads lines independently using `std::getline`. Parsed records are accumulated in a per-thread `vector<ServiceRequest>`. After all threads complete, results are merged into the shared `records_` vector serially.

**Thread configuration:** Load uses T=4 threads. Increasing beyond 4 showed diminishing returns due to StringRegistry mutex contention (discussed below). Search uses T=8 threads to fully saturate all CPU cores.

### 5.3 Thread Safety Design

Two critical shared data structures required careful synchronization:

**StringPool (lock-free):** The pool's write position is a `std::atomic<uint32_t>`. Each `store()` call uses `fetch_add` to atomically claim a region of the buffer and then writes via `memcpy`. No mutex is needed; multiple threads write to disjoint regions simultaneously. This is safe because the buffer is pre-allocated and fixed-size.

**StringRegistry (shared_mutex / MRSW):** Each registry uses a `std::shared_mutex`. The read path (already-seen string → return code) takes a `shared_lock`, allowing all threads to read concurrently. The write path (new string → assign code and insert) takes a `unique_lock`. In practice, most strings are already interned within the first few thousand records; the write path is rarely contended after warmup. Nine separate registries exist (one per categorical field), reducing per-registry contention further.

### 5.4 Parallel Search Design

All three search functions use the same pattern:
```
Per-thread local result vectors → #pragma omp for schedule(static) → merge
```

Each thread maintains its own `vector<const ServiceRequest*>`. The OMP loop distributes index ranges evenly across threads with `schedule(static)`. After the parallel section, results are concatenated serially. No locks are needed during search — `records_` is read-only after load.

`#pragma omp for nowait` is used so threads do not synchronize at the end of the loop before the merge — they exit immediately upon finishing their share.

### 5.5 Phase 2 Peak Memory

**Observed: 11.54 GB (Activity Monitor)**

Slightly higher than Phase 1 (~240 MB more) due to:
- Per-thread `vector<ServiceRequest>` accumulation during load (each thread holds its parsed chunk before merge — up to 4 × ~1.3 GB = ~5.2 GB temporarily, but the OS reuses freed per-thread memory after merge)
- In practice the peak is during the merge phase when both the per-thread vectors and the final `records_` vector coexist briefly

### 5.6 Phase 2 Benchmark Results (10 runs, T=4 load, T=8 search)

| Run | Load (ms) | searchByZip (ms) | searchByDate (ms) | searchByBBox (ms) |
|---|---|---|---|---|
| 1 | 40,883 | 270 | 57 | 69 |
| 2 | 36,681 | 336 | 76 | 90 |
| 3 | 37,608 | 256 | 56 | 69 |
| 4 | 35,984 | 255 | 58 | 70 |
| 5 | 36,362 | 263 | 57 | 70 |
| 6 | 36,927 | 261 | 57 | 68 |
| 7 | 36,335 | 265 | 56 | 72 |
| 8 | 39,690 | 492 | 64 | 91 |
| 9 | 36,189 | 260 | 57 | 71 |
| 10 | 36,200 | 259 | 61 | 71 |
| **Avg** | **37,286** | **292** | **60** | **74** |
| **Min** | **35,984** | **255** | **56** | **68** |
| **Max** | **40,883** | **492** | **76** | **91** |

Run 8's searchByZip spike (492 ms vs 260 ms average) coincides with elevated load time (39,690 ms), suggesting momentary OS memory pressure causing page evictions between load and search. No full swap event occurred.

### 5.7 Phase 2 CPU Utilization

**Observed: ~170% (Activity Monitor, out of 800% max for 8 cores)**

This reflects 4 threads actively parsing during load (I/O bound + parse bound), and 8 threads scanning during search (compute and memory bound). The relatively low CPU% compared to 400% (4 threads × 100%) reflects that threads spend time waiting on `ifstream` I/O and StringRegistry locks rather than pure compute.

---

## 6. Phase 3 — SoA Layout + mmap + Parallel Merge

### 6.1 Design Philosophy

Phase 3's goal is to maximize search throughput by addressing the fundamental cache inefficiency of the AoS layout used in Phases 1 and 2. A secondary goal is to reduce load time through memory-mapped I/O. Phase 3 maintains the full 44-field `ServiceRequest` record for cold-path result access, while adding four flat Structure-of-Arrays (SoA) arrays exclusively for the hot search path.

### 6.2 Cache Line Analysis: Why AoS Hurts Search

The `ServiceRequest` struct is 264 bytes. A CPU cache line is 64 bytes. When `searchByZip` accesses `records_[i].location.zip`, the CPU loads the entire 64-byte cache line containing that offset — but only 4 bytes (the zip field) are used. Cache line utilization = 4/64 = **6.25%**.

Across 20.1 million records, the zip scan must touch:
```
20,129,233 × 264 bytes = 5.31 GB of data (AoS)
```

With 8 search threads, each thread scans 5.31/8 = **664 MB** — far exceeding L3 cache (typically 8–16 MB). Every access is a cache miss to DRAM.

With SoA (`std::vector<uint32_t> zip_`), 16 zip values pack into one 64-byte cache line (100% utilization). The entire zip array is:
```
20,129,233 × 4 bytes = 80.5 MB
```
Per thread: 80.5/8 = **10 MB** — fits entirely in L2/L3 cache. The hardware prefetcher can stay ahead of sequential access, making the scan essentially memory-latency-free after the first few cache lines.

The same analysis applies to `created_[]` (uint32_t, date search) and `lat_[]/lon_[]` (float, bounding box search).

### 6.3 Why `float` Instead of `double` for Lat/Lon

`double` is 8 bytes; `float` is 4 bytes. GPS coordinates for range queries need approximately 5 decimal places of precision (1 meter resolution). `float` provides 7 significant digits, which is sufficient (e.g., 40.71234°). Using `float` halves the SoA array size for lat and lon: 2 × 20M × 4 = 160 MB instead of 320 MB. The tradeoff is a maximum error of ~1 meter at bounding box boundaries — acceptable for a range query that is inherently an approximation.

A subtle implementation detail: search parameters arrive as `double`. The bounds are cast to `float` once before the loop (not inside the loop) to avoid per-element promotion overhead:
```cpp
float flt_lat_min = (float)lat_min;
for (int i = 0; i < (int)lat_.size(); ++i)
    if (lat_[i] >= flt_lat_min && ...)
```

### 6.4 Memory-Mapped I/O

Phase 2 load path: SSD → kernel page cache → `read()` syscall → userspace `std::string` buffer → `getline`. Each `getline` allocates a new `std::string` on the heap (20M allocations) and copies data from kernel to userspace.

Phase 3 load path: SSD → kernel page cache = mmap region. The `mmap(PROT_READ, MAP_PRIVATE)` call maps the file's kernel pages directly into the process virtual address space with no copy. `CSVParser::parseLine` receives a `const char*` pointer into the mapped buffer and returns `std::string_view` objects (zero-copy, zero allocation). `madvise(MADV_SEQUENTIAL)` informs the OS to aggressively prefetch pages ahead of the current read position, hiding page fault latency.

After parsing is complete, `munmap()` releases the virtual mapping. The OS can reclaim pages under memory pressure.

**Why mmap did not dramatically improve load time:** The bottleneck in load is not file I/O but string storage. `StringPool::store()` is called approximately 15 times per record × 20.1M records = 300M calls, each involving a `memcpy` of average ~20 bytes plus an atomic `fetch_add`. This dominates over the elimination of `getline` allocations. The net result is that mmap load time is statistically comparable to Phase 2's `ifstream` approach on macOS (where `read()` is heavily optimized for sequential access).

### 6.5 Two-Pass Load (Failed Attempt)

The initial Phase 3 load design used a two-pass approach. Pass 1 counted newlines per thread chunk using `memchr` in parallel, allowing pre-allocation of exact-sized flat arrays. Pass 2 then parsed and wrote directly into pre-sized arrays at pre-computed index offsets — no per-thread local vectors, no merge step.

**Result:** Load time was **50,252 ms** — 13 seconds *slower* than Phase 2.

**Root cause:** Pass 1 required a full sequential scan of 12 GB (via `memchr`) before any parsing could begin. Even though parallelized, this added ~4 seconds of pure I/O overhead. Additionally, the mmap approach on macOS incurs higher TLB pressure than `read()` for first-touch access of 12 GB (approximately 3 million 4 KB page table entries). The two-pass design was abandoned in favor of single-pass.

### 6.6 Single-Pass Load with Per-Thread Vectors

The revised design mirrors Phase 2's per-thread vector accumulation but uses the mmap buffer instead of `ifstream`. Each thread walks its chunk using `memchr('\n')` for line detection, calls `CSVParser::parseLine` (zero-copy `string_view` into mapped buffer), and accumulates records in per-thread local vectors for all 5 arrays: `t_records[tid]`, `t_zip[tid]`, `t_created[tid]`, `t_lat[tid]`, `t_lon[tid]`.

After the parallel parse section, a prefix sum over per-thread counts establishes each thread's write offset into the final pre-sized arrays. A second parallel section then performs the merge using `std::memcpy` for the trivially-copyable SoA arrays:

```cpp
std::memcpy(zip_.data() + base, t_zip[tid].data(), n * sizeof(uint32_t));
```

`memcpy` for contiguous `uint32_t` and `float` arrays is orders of magnitude faster than element-wise `push_back`, and the parallel merge ensures all threads contribute simultaneously.

### 6.7 Thread-Local StringRegistry Cache (Attempted Optimization)

**Hypothesis:** The nine shared `StringRegistry` instances, each protected by a `shared_mutex`, would be contention points under 6-thread parallel load. Providing each thread with a local `unordered_map<string, uint8_t/uint16_t>` as an L1 cache would eliminate lock calls for already-seen strings (the vast majority after warmup).

**Implementation:** Each thread maintained 9 local cache maps. Before calling `reg.encode(sv)`, the thread checked its local map. On a cache miss only, it called the shared registry and stored the result locally.

**Result:** Load time was **40,724 ms** — approximately the same as without the cache (39,249 ms), within run-to-run variance.

**Analysis (key negative finding):** The optimization had neutral impact because StringRegistry mutex contention was *not* the actual bottleneck. The `unordered_map` lookup itself has overhead: string key construction from `string_view` plus hash computation on every call, even hits. This overhead approximately canceled the mutex savings. The true bottleneck was StringPool — 300M atomic `fetch_add` operations plus 300M `memcpy` calls for string data. This finding demonstrates the danger of profiling intuition without measurement: the most visible synchronization primitive (`shared_mutex`) was not the limiting factor.

### 6.8 Phase 3 Peak Memory

**Observed: 11.30 GB (Activity Monitor) — identical to Phase 1**

mmap does not add a userspace copy of the file. The OS page cache serves as the mapped region; no additional buffer exists in userspace. The SoA arrays add:
- `zip_[]`: 20.1M × 4 = 80.5 MB
- `created_[]`: 80.5 MB
- `lat_[]`: 80.5 MB
- `lon_[]`: 80.5 MB
Total SoA overhead: ~322 MB

This is partially offset by eliminating the per-thread local vectors (freed after merge). Net peak memory is essentially the same as Phase 1, despite having both the AoS records and the SoA search arrays simultaneously. Phase 2's slightly higher peak (11.54 GB) was due to the concurrent existence of per-thread vectors plus the final records array during the merge phase.

### 6.9 Phase 3 Benchmark Results (10 runs, T=4 load, T=8 search)

| Run | Load (ms) | searchByZip (ms) | searchByDate (ms) | searchByBBox (ms) |
|---|---|---|---|---|
| 1 | 39,549 | 17.70 | 14.83 | 39.28 |
| 2 | 38,628 | 21.12 | 16.79 | 43.17 |
| 3 | 40,232 | 19.62 | 17.34 | 38.48 |
| 4 | 34,951 | 22.25 | 19.70 | 42.11 |
| 5 | 36,596 | 21.26 | 17.07 | 43.97 |
| 6 | 35,091 | 22.20 | 16.83 | 43.54 |
| 7 | 34,167 | 20.55 | 16.58 | 43.84 |
| 8 | 34,777 | 16.25 | 14.83 | 38.69 |
| 9 | 35,502 | 21.10 | 16.86 | 41.15 |
| 10 | 36,055 | 20.39 | 17.73 | 39.78 |
| **Avg** | **36,555** | **20.24** | **16.86** | **41.40** |
| **Min** | **34,167** | **16.25** | **14.83** | **38.48** |
| **Max** | **40,232** | **22.25** | **19.70** | **43.97** |

Search results are highly consistent across all 10 runs (coefficient of variation < 10%), confirming that the SoA arrays fit stably in cache and performance is not sensitive to OS page cache state.

**Note on bounding box result count:** Phase 3 returns 8,043,188 records vs 8,043,002 in Phases 1 and 2 — a difference of 186 records. This is due to `float` precision: records near the bounding box boundary have lat/lon values that round differently when stored as `float` vs `double`. This is expected and acceptable for a range query.

### 6.10 Phase 3 CPU Utilization

**Observed: ~110% (Activity Monitor)**

Lower than Phase 2 (170%) for two reasons. First, Phase 3 search operations complete so fast (16–44 ms) that the CPU is barely measured as active. Second, the SoA search is extremely cache-efficient — the CPU spends less time stalled on memory and more time in sequential compute, but the total wall time is so short that the activity monitor sampling period (typically 1 second) misses most of the work.

---

## 7. Cross-Phase Comparison

### 7.1 Load Performance

| Phase | Strategy | Avg Load (ms) | vs Phase 1 |
|---|---|---|---|
| Phase 1 | Serial `ifstream` + `getline` | 93,285 | baseline |
| Phase 2 | Parallel `ifstream` (T=4) | 37,286 | **2.5× faster** |
| Phase 3 | mmap + parallel (T=4) | 36,555 | **2.6× faster** |

Phase 3 load is only marginally faster than Phase 2 because the true bottleneck (StringPool stores — 300M atomic memcpy operations) is unchanged in both designs. mmap eliminates the `getline` allocation cost but cannot eliminate the per-field string storage cost.

### 7.2 Search Performance

| Operation | Phase 1 (serial) | Phase 2 (parallel AoS, T=8) | Phase 3 (SoA, T=8) | P1→P3 Speedup |
|---|---|---|---|---|
| searchByZip | 3,053 ms | 292 ms | 20 ms | **153× faster** |
| searchByDate | 2,741 ms | 60 ms | 17 ms | **161× faster** |
| searchByBBox | 2,835 ms | 74 ms | 41 ms | **69× faster** |

### 7.3 Memory Usage

| Phase | Peak RAM | Notes |
|---|---|---|
| Phase 1 | 11.30 GB | records_ + pools + page cache |
| Phase 2 | 11.54 GB | +240 MB from concurrent per-thread vectors during merge |
| Phase 3 | 11.30 GB | SoA adds 322 MB but mmap eliminates getline buffers; net same |

### 7.4 CPU Utilization

| Phase | CPU % | Explanation |
|---|---|---|
| Phase 1 | ~99.5% | Single core fully saturated |
| Phase 2 | ~170% | 4 threads load (I/O limited) + 8 threads search |
| Phase 3 | ~110% | Search completes so fast it barely registers in sampling |

---

## 8. Key Findings and Conclusions

### 8.1 Memory Layout Determines Search Performance More Than Thread Count

The most dramatic finding is that switching from AoS to SoA search arrays produced a 14× additional speedup on top of Phase 2's 8-thread parallelism (searchByZip: 292 ms → 20 ms). Thread count alone (Phase 1 → Phase 2) produced a 10× speedup. Memory layout (Phase 2 → Phase 3) produced an additional 14× speedup. The memory access pattern — not compute throughput — was the binding constraint for search.

### 8.2 The OS Swap Event Is the Real Memory Boundary

Phase 1 Run 9 (437,783 ms load — 4.7× normal) was caused by involuntary OS swapping when the machine's page cache was exhausted after 8 consecutive runs. The 12 GB dataset on a 16 GB machine leaves only 4 GB of headroom. After repeated loads, the OS fills physical RAM with page cache pages and begins swapping process memory to SSD. This event illustrates that operating within the "safe" RAM budget requires accounting for OS page cache — not just process RSS. Phase 3's mmap approach gives the OS better signals (via `MADV_SEQUENTIAL`) to evict pages proactively, reducing swap risk.

### 8.3 Profiling Intuition Can Be Wrong

The thread-local StringRegistry cache optimization was designed to eliminate mutex contention, which appeared to be the obvious bottleneck under 6-thread load. The measured result was neutral. The actual bottleneck — StringPool atomic operations plus 300M `memcpy` calls — was invisible to intuition but dominant in wall time. This underscores the importance of measurement-driven optimization.

### 8.4 mmap Benefit Is Architecture-Dependent

On macOS (Darwin), `read()` and sequential `ifstream` are aggressively optimized at the kernel level. The theoretical advantage of mmap (zero-copy, no userspace buffer) did not materialize as a measurable load speedup. On Linux with `MADV_SEQUENTIAL`, mmap typically outperforms `read()` for large sequential scans due to more aggressive page prefetching. This is a platform-specific result worth noting for cross-platform deployments.

### 8.5 `float` Precision Is Sufficient for Geographic Range Queries

Storing lat/lon as `float` (4 bytes) instead of `double` (8 bytes) halved the SoA array memory footprint for geographic data. The precision loss (~1 meter at boundaries) caused 186 out of 8,043,188 records to shift across the bounding box boundary — a 0.002% discrepancy that is negligible for range query semantics. The memory and cache efficiency gains (160 MB vs 320 MB for both arrays) outweigh this artifact.

### 8.6 Two-Pass Load Failed Empirically

The two-pass approach (pass 1: count newlines to pre-size arrays; pass 2: parse directly into pre-sized arrays) seemed theoretically attractive — no merge step, no per-thread local vectors. In practice it added 13 seconds to load time because pass 1 required a full extra scan of 12 GB of data before a single record was parsed. Single-pass with per-thread vectors and a parallel merge was faster in total, despite the merge overhead.

---

## 9. Summary Statistics

| Metric | Phase 1 | Phase 2 | Phase 3 |
|---|---|---|---|
| Load avg (ms) | 93,285 | 37,286 | 36,555 |
| searchByZip avg (ms) | 3,053 | 292 | 20 |
| searchByDate avg (ms) | 2,741 | 60 | 17 |
| searchByBBox avg (ms) | 2,835 | 74 | 41 |
| Peak RAM (GB) | 11.30 | 11.54 | 11.30 |
| CPU utilization | ~99.5% | ~170% | ~110% |
| Records loaded | 20,129,233 | 20,129,233 | 20,129,233 |
| Load threads | 1 | 4 | 4 |
| Search threads | 1 | 8 | 8 |

---

## 10. References

1. "What Every Programmer Should Know About Memory," Ulrich Drepper, Red Hat, 2007. (Cache line analysis, TLB behavior, NUMA effects)
2. Intel 64 and IA-32 Architectures Optimization Reference Manual — Chapter 2 (Cache hierarchy, prefetching)
3. OpenMP API Specification v5.1 — `schedule(static)`, `nowait`, `shared_mutex` behavior
4. POSIX `mmap(2)` and `madvise(2)` man pages (MAP_PRIVATE, MADV_SEQUENTIAL semantics)
5. NYC OpenData — 311 Service Requests dataset, https://opendata.cityofnewyork.us
6. C++17 Standard — `std::atomic`, `std::shared_mutex`, `std::string_view`
7. "False Sharing and Its Effect on Shared Memory Performance," S. Eggers & T. Jeremiassen, 1991 USENIX Symposium on Experiences with Distributed and Multiprocessor Systems
