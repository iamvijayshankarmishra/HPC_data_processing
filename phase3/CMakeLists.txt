find_package(OpenMP QUIET)

if(NOT OpenMP_CXX_FOUND)
    find_library(LIBOMP_LIB omp
        HINTS /opt/homebrew/opt/libomp/lib /usr/local/opt/libomp/lib)
    find_path(LIBOMP_INC omp.h
        HINTS /opt/homebrew/opt/libomp/include /usr/local/opt/libomp/include)
    if(LIBOMP_LIB AND LIBOMP_INC)
        add_library(OpenMP::OpenMP_CXX INTERFACE IMPORTED)
        set_target_properties(OpenMP::OpenMP_CXX PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LIBOMP_INC}"
            INTERFACE_LINK_LIBRARIES "${LIBOMP_LIB}"
            INTERFACE_COMPILE_OPTIONS "-Xpreprocessor;-fopenmp")
    else()
        message(FATAL_ERROR "OpenMP not found. Run: brew install libomp")
    endif()
endif()

add_executable(phase3
    src/main.cpp
    src/SoADataStore.cpp
    ../phase1/src/CSVParser.cpp
    benchmark/Benchmark.cpp
)

target_include_directories(phase3 PRIVATE
    include
    ../phase1/include
)

target_link_libraries(phase3 PRIVATE common OpenMP::OpenMP_CXX)
target_compile_options(phase3 PRIVATE -O3 -Wall -Wextra)
