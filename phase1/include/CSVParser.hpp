#pragma once
#include <string_view>

class CSVParser
{
public:
    static int parseLine(const char* line, int len,
        std::string_view* fields, int max_fields);
};