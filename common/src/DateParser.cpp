#include "DateParser.hpp"

uint32_t toEpoch(int y, int m, int d , int h, int min, int sec) {
    int ym1=y-1;
    int days = ym1*365 + ym1/4 - ym1/100 + ym1/400;

    days -= 719162;
    static const int month_days[] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    bool leap = (y%4 == 0 && y%100 != 0) || (y%400 == 0);

    for (int i=1; i<m; i++) {
        days += month_days[i];
        if (i == 2 && leap) {
            days++;
        }
    }
    days += d - 1;
    return uint32_t(days * 24 * 3600 + h * 3600 + min * 60 + sec);
}

uint32_t parseDateTime(const char* s, int len) {
    
    if (!s || len<8) {
        return 0;
    }
    int month = (s[0]-'0')*10 + (s[1]-'0');
    int day = (s[3]-'0')*10 + (s[4]-'0');
    int year = (s[6]-'0')*1000 + (s[7]-'0')*100 + (s[8]-'0')*10 + (s[9]-'0');
    int hour = (s[11]-'0')*10 + (s[12]-'0');
    int min = (s[14]-'0')*10 + (s[15]-'0');
    int sec = (s[17]-'0')*10 + (s[18]-'0');

    if (len>20) {
        bool is_pm = (s[20] == 'P');
        if ( is_pm && hour != 12) hour += 12;
        if (!is_pm && hour == 12) hour = 0;
    }
    return toEpoch(year, month, day, hour, min, sec);
}