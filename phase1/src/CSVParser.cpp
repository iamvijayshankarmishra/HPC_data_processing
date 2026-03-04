#include "CSVParser.hpp"

int CSVParser::parseLine(const char* line, int len,
    std::string_view* fields, int max_fields)
{
    int count=0;
    int i=0;

    while(i<=len && count< max_fields) {
        bool quoted = (i<len && line[i]=='"');
        int start = quoted ? i+1 : i;
        int end = start;

        if (quoted) {
            i=start;
            while(i<len && line[i]!='"') i++;
            end = i;
            if (i<len) i++;
            if (i<len && line[i]==',') i++;
        } else {
            while(i<len && line[i]!=',') i++;
            end = i;
            if (i<len) i++;
        }
        fields[count++] = std::string_view(line+start, end-start);

    }
    return count;
}