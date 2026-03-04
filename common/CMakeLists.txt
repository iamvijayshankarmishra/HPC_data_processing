add_library(common
    src/StringPool.cpp
    src/DateParser.cpp
)

target_include_directories(common PUBLIC include)

target_compile_options(common PRIVATE -O2 -Wall -Wextra)
