add_executable(phase1
    src/main.cpp
    src/CSVParser.cpp
    src/DataStore.cpp
    benchmark/Benchmark.cpp
)

target_include_directories(phase1 PRIVATE include)
target_link_libraries(phase1 PRIVATE common)
target_compile_options(phase1 PRIVATE -O2 -Wall -Wextra)
